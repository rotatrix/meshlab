#include <openaxis/navigation.hpp>
#include <openaxis/connection_manager.hpp>
#include <openaxis/logging.hpp>
#include "openaxis_overlay.h"
#include "glarea.h"
#include "mainwindow.h"
#include "openaxis_controller.h"
#include "openaxis_camera.h"
#include "openaxis_picking.h"
#include "openaxis_pivot.h"
#include "openaxis_scheduler.h"
#include "openaxis_focus.h"
#include <QApplication>
#include <QClipboard>
#include <QImage>
#include <QCursor>
#include <QDialog>
#include <QFile>
#include <QHBoxLayout>
#include <QGLFramebufferObject>
#include <QLabel>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QShortcut>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>
#include <climits>
#include <limits>

namespace {

template<class T> openaxis::Vec3 vector(const vcg::Point3<T> &p) { return {p[0], p[1], p[2]}; }
vcg::Point3f point(openaxis::Vec3 p) { return {float(p.x), float(p.y), float(p.z)}; }
openaxis::Quat rotation(const vcg::Quaternionf &q) { return {q[0], q[1], q[2], q[3]}; }
// MeshDocument does not export staticMetaObject as DLL data on Windows, so
// use Qt's string-based signal connection at this existing library boundary.
class SceneObserver final : public QObject {
    Q_OBJECT
public:
    std::function<void()> changed;
public slots:
    void notify() { if (changed) changed(); }
};
openaxis::OpenAxisClientOptions options(openaxis::Scheduler *scheduler) {
    openaxis::DiagnosticLog::configure("meshlab");
    openaxis::OpenAxisClientOptions result;
    result.client_name = "MeshLab";
    result.target = {{"pid", openaxis::current_process_id()}};
    result.scheduler = scheduler;
    return result;
}
}

struct OpenAxisController::Impl final : openaxis::NavigationAdapter {
    // One protocol endpoint per process. Viewports retain only their native
    // camera/rendering state; the shared adapter binds a gesture to one of them.
    struct Hub final : QObject, openaxis::NavigationAdapter {
        meshlab_openaxis::QtScheduler scheduler;
        openaxis::OpenAxisClient client;
        openaxis::NavigationDiagnostics collector;
        openaxis::NavigationSession session;
        openaxis::OpenAxisConnectionManager connection;
        QTimer timer;
        std::vector<Impl *> views;
        Impl *current = nullptr;
        std::uint64_t epoch = 0;
        bool refreshing = false, announcedFocus = false;
        Hub();
        ~Hub();
        void refresh();
        void remove(Impl *);
        bool eventFilter(QObject *, QEvent *) override;
        openaxis::NavigationContext capture_context() override;
        bool is_current(const openaxis::NavigationContext &) override;
        std::unique_ptr<openaxis::NavigationCapture> begin_query(const openaxis::NavigationContext &) override;
        openaxis::WriteResult apply_pose(const openaxis::NavigationContext &, const openaxis::NavigationPose &,
                                        const openaxis::Value &, std::optional<openaxis::Vec3>) override;
        void show_pivot(const openaxis::NavigationContext &, std::optional<openaxis::Vec3>) override;
    };
    static std::shared_ptr<Hub> sharedHub() {
        static std::weak_ptr<Hub> shared;
        auto result=shared.lock();
        if (!result) { result=std::make_shared<Hub>(); shared=result; }
        return result;
    }
    GLArea &view;
    std::shared_ptr<Hub> hub;
    meshlab_openaxis::QtScheduler &scheduler;
    SceneObserver sceneObserver;
    openaxis::OpenAxisClient &client;
    openaxis::NavigationDiagnostics &collector;
    openaxis::NavigationSession &session;
    openaxis::OpenAxisConnectionManager &connection;
    bool focused = false;
    QPointer<QDialog> diagnostics;
    QPointer<QLabel> statusLabel;
    bool matricesValid = false;
    double sceneModel[16]{}, sceneProjection[16]{};
    std::optional<double> overlayExpiry;
    std::string lastContext;
    openaxis::Value lastPose;
    std::optional<openaxis::Vec3> pivot;
    std::uint64_t revision = 0;
    bool pickPending = false, pickSelection = false;
    QPointF pickPixel;
    openaxis::Value pickResult;

    explicit Impl(GLArea &v) : view(v), hub(sharedHub()), scheduler(hub->scheduler),
        client(hub->client), collector(hub->collector), session(hub->session), connection(hub->connection) {
        hub->views.push_back(this);
        sceneObserver.changed = [this] {
            ++revision;
            if (hub->current==this) session.cancel("scene_changed");
            pivot.reset(); view.update();
        };
        QObject::connect(view.md(), SIGNAL(meshSetChanged()), &sceneObserver, SLOT(notify()));
        QObject::connect(view.md(), SIGNAL(meshDocumentModified()), &sceneObserver, SLOT(notify()));
        QObject::connect(view.md(), SIGNAL(documentUpdated()), &sceneObserver, SLOT(notify()));
        QObject::connect(view.md(), SIGNAL(currentMeshChanged(int)), &sceneObserver, SLOT(notify()));
        auto *shortcut = new QShortcut(QKeySequence("Ctrl+Shift+O"), &view);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        QObject::connect(shortcut, &QShortcut::activated, &sceneObserver, [this] { toggleDiagnostics(); });
    }
    ~Impl() override {
        hub->remove(this);
        delete diagnostics.data();
    }
    QString logPath() const {
        return QString::fromStdString(openaxis::DiagnosticLog::configure("meshlab")->path().u8string());
    }
    QString statusText() const {
        const auto s = connection.status();
        QString result = QString("Connection: %1 (shared by all viewports)\nEndpoint: %2\nViewport focus: %3 | Gesture: %4")
            .arg(QString::fromStdString(s.state), QString::fromStdString(client.url()),
                 focused ? "active" : "inactive", hub->current==this && session.active() ? "active" : "idle");
        if (!s.error.empty()) result += "\nLast connection error: " + QString::fromStdString(s.error);
        if (s.retry_at) result += QString("\nAutomatic retry in %1 s").arg(std::max(0., *s.retry_at-openaxis::diagnostic_time()), 0, 'f', 1);
        result += "\nLog: " + logPath();
        return result;
    }
    void updateDiagnostics() {
        if (diagnostics && diagnostics->isVisible()) statusLabel->setText(statusText());
    }
    void reconnect() {
        session.cancel("manual_reconnect");
        pivot.reset();
        connection.stop();
        connection.start();
    }
    void toggleDiagnostics() {
        if (diagnostics) {
            diagnostics->setVisible(!diagnostics->isVisible());
        } else {
            diagnostics = new QDialog(&view, Qt::Tool);
            diagnostics->setWindowTitle("OpenAxis Diagnostics");
            diagnostics->setModal(false);
            diagnostics->resize(520, 150);
            auto *layout = new QVBoxLayout(diagnostics);
            statusLabel = new QLabel(diagnostics);
            statusLabel->setTextFormat(Qt::PlainText);
            statusLabel->setWordWrap(true);
            statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
            layout->addWidget(statusLabel);
            auto *buttons = new QHBoxLayout;
            layout->addLayout(buttons);
            auto addButton = [&](const QString &label, auto callback) {
                auto *button = new QPushButton(label, diagnostics);
                buttons->addWidget(button);
                QObject::connect(button, &QPushButton::clicked, &sceneObserver, callback);
            };
            addButton("Reconnect", [this] { reconnect(); });
            addButton("Copy diagnostics", [this] {
                QString text = "MeshLab OpenAxis diagnostics\n" + statusText() + "\n\n";
                for (const auto &line : collector.presentation().lines) text += QString::fromStdString(line.text) + "\n";
                QFile log(logPath());
                if (log.open(QIODevice::ReadOnly)) {
                    log.seek(std::max<qint64>(0, log.size()-65536));
                    text += "\n\nSDK log (last 64 KiB):\n" + QString::fromUtf8(log.readAll());
                }
                QApplication::clipboard()->setText(text);
            });

            addButton("Close", [this] { diagnostics->close(); });
            QObject::connect(diagnostics, &QDialog::finished, &sceneObserver, [this] {
                if (hub->current==this) collector.set_enabled(false);
                view.update();
            });
            diagnostics->show();
        }
        hub->refresh();
        updateDiagnostics();
        view.update();
    }
    bool available() const {
        return view.isVisible() && view.isCurrent() && view.isValid() && view.isEnabled() &&
            view.width() > 0 && view.height() > 0 && !view.md()->isBusy() &&
            !view._isRaster && !view.takeSnapTile && !view.currentEditor;
    }
    bool active() const {
        return hub->current==this && eligible();
    }
    bool eligible() const {
        const auto *window=view.mw();
        return meshlab_openaxis::ownsNavigationFocus(&view,window,window ? window->GLA() : nullptr,available());
    }
    std::string key() const {
        std::string result = std::to_string(hub->epoch) + "/" +
            std::to_string(reinterpret_cast<std::uintptr_t>(&view)) + "/" +
            std::to_string(reinterpret_cast<std::uintptr_t>(view.md())) + "/" +
            std::to_string(revision) + "/" + std::to_string(view.width()) + "/" +
            std::to_string(view.height()) + "/" + std::to_string(view.devicePixelRatioF());
        for (auto it = view.meshVisibilityMap.cbegin(); it != view.meshVisibilityMap.cend(); ++it)
            result += "/" + std::to_string(it.key()) + (it.value() ? ":1" : ":0");
        return result;
    }
    std::optional<openaxis::Pose> read() const {
        const auto &tb = view.trackball;
        const double scale = tb.track.sca;
        if (!available() || !std::isfinite(scale) || scale <= 0) return {};
        const auto q = rotation(tb.track.rot);
        const double distance = view.fov <= 5 ? 8. : view.getCameraDistance();
        openaxis::Pose p{meshlab_openaxis::eye(q, vector(tb.track.tra), vector(tb.center), scale, distance), q.inverse().rotvec()};
        if (view.fov <= 5) p.ortho_extent = 2 * view.viewRatio() / scale;
        else p.fov = vcg::math::ToRad(double(view.fov));
        return p;
    }
    void refresh() {
        hub->refresh();
    }
    void refreshCurrent() {
        collector.set_enabled(diagnostics && diagnostics->isVisible());
        if (overlayExpiry && openaxis::diagnostic_time() >= *overlayExpiry) {
            overlayExpiry.reset(); view.update();
        }
        const bool now = active();
        const auto context = key();
        if (context != lastContext || (focused && !now)) {
            session.cancel("viewport_changed"); pivot.reset(); lastContext = context;
        }
        focused = now;
        if (auto p = read()) {
            const auto value = openaxis::pose_value(*p);
            if (!lastPose.is_null() && value != lastPose && focused) session.native_camera_changed();
            lastPose = value;
        }
        collector.set_context(context);
        updateDiagnostics();
    }
    openaxis::NavigationContext capture_context() override { return active() ? openaxis::NavigationContext(key()) : openaxis::NavigationContext{}; }
    bool is_current(const openaxis::NavigationContext &c) override {
        auto k = std::any_cast<std::string>(&c);
        return k && active() && *k == key();
    }
    struct Capture final : openaxis::NavigationCapture {
        std::weak_ptr<Hub> hub; openaxis::NavigationContext context; std::optional<openaxis::Pose> initial;
        Capture(Impl &o, openaxis::NavigationContext c) : hub(o.hub), context(std::move(c)), initial(o.read()) {}
        std::optional<openaxis::Pose> initial_observation() override { return initial; }
        openaxis::Value resolve(const std::string &name) override {
            auto owner=hub.lock();
            if (!owner || !owner->is_current(context)) return nullptr;
            if (name == "camera.pose") return initial ? openaxis::pose_value(*initial) : openaxis::Value{};
            return owner->current->fact(name);
        }
    };
    std::unique_ptr<openaxis::NavigationCapture> begin_query(const openaxis::NavigationContext &c) override {
        return is_current(c) ? std::make_unique<Capture>(*this, c) : nullptr;
    }
    openaxis::WriteResult apply_pose(const openaxis::NavigationContext &c, const openaxis::NavigationPose &p,
                           const openaxis::Value &, std::optional<openaxis::Vec3>) override {
        if (!is_current(c)) return {};
        for (double x : {p.t.x,p.t.y,p.t.z,p.r.x,p.r.y,p.r.z,p.fov,p.ortho_extent})
            if (!std::isfinite(x)) return {};
        auto &tb = view.trackball;
        double scale = tb.track.sca;
        if (p.ortho_extent > 0) {
            scale = 2 * view.viewRatio() / p.ortho_extent;
            if (scale < 1e-10 || scale > 1e10) return {};
            view.fov = 5;
        } else if (p.fov > 0) view.fov = float(std::clamp(vcg::math::ToDeg(p.fov), 5.01, 90.));
        else return {};
        const auto q = openaxis::Quat::from_rotvec(p.r).inverse();
        const auto t = meshlab_openaxis::translation(q, p.t, vector(tb.center), scale,
            view.fov <= 5 ? 8. : view.getCameraDistance());
        tb.track.sca = float(scale);
        tb.track.rot = vcg::Quaternionf(float(q.w),float(q.x),float(q.y),float(q.z));
        tb.track.tra = point(t);
        auto realized = read();
        if (realized) lastPose = openaxis::pose_value(*realized);
        view.update();
        return {bool(realized), realized};
    }
    void show_pivot(const openaxis::NavigationContext &c, std::optional<openaxis::Vec3> p) override {
        if (!p || is_current(c)) { pivot = p; view.update(); }
    }
    openaxis::Value fact(const std::string &name) {
        if (name == "document.id") return std::to_string(reinterpret_cast<std::uintptr_t>(view.md()));
        if (name == "world.orientation") return {{"forward", {0,0,-1}}, {"up", {0,1,0}}, {"handedness", "right"}};
        if (name == "viewport.aspect") return double(view.width()) / view.height();
        if (name == "camera.view_target") return openaxis::vector_value(vector(view.trackball.center - view.trackball.track.tra));
        const auto cursor = view.mapFromGlobal(QCursor::pos());
        const bool inside = view.rect().contains(cursor);
        if (name == "viewport.cursor" && inside)
            return {{"x", 2. * cursor.x() / view.width() - 1}, {"y", 1 - 2. * cursor.y() / view.height()}};
        if (name == "model.bounds" || name == "selection.bounds") {
            vcg::Box3<Scalarm> bounds;
            for (auto &mesh : view.md()->meshIterator()) {
                if (!view.meshVisibilityMap.value(mesh.id(), false) || (name == "selection.bounds" && &mesh != view.mm())) continue;
                bounds.Add(mesh.cm.Tr, mesh.cm.bbox);
            }
            if (!bounds.IsNull()) return {{"min", openaxis::vector_value(vector(bounds.min))}, {"max", openaxis::vector_value(vector(bounds.max))}};
        }
        const bool center = name == "pick.viewport_center" || name == "pick.viewport_center.selection";
        const bool cursorPick = name == "pick.cursor" || name == "pick.cursor.selection";
        if ((center || cursorPick) && (center || inside)) {
            pickPixel = QPointF(center ? view.width()*.5 : cursor.x(), center ? view.height()*.5 : cursor.y());
            pickSelection = name.find(".selection") != std::string::npos;
            pickResult = {{"markerPosition", {pickPixel.x(),pickPixel.y()}}};
            pickPending = true;
            // Repaint synchronously to sample the current camera's depth while
            // the native scene matrices are installed. No geometry index or CPU
            // face traversal; the same path also picks rendered point clouds.
            view.repaint();
            pickPending = false;
            return pickResult;
        }
        // A missing surface hit lets the server select its configured fallback.
        return nullptr;
    }
    void sampleDepth() {
        // Capture native matrices before the trackball and document overlays.
        // This also reprojects diagnostics correctly during native mouse motion.
        matricesValid = available();
        if (matricesValid) {
            glGetDoublev(GL_MODELVIEW_MATRIX,sceneModel);
            glGetDoublev(GL_PROJECTION_MATRIX,sceneProjection);
        }
        samplePick();
        drawPivot();
    }
    void samplePick() {
        if (!pickPending || !available()) return;
        pickPending = false;
        GLint viewport[4]; glGetIntegerv(GL_VIEWPORT, viewport);
        const auto pixel = meshlab_openaxis::depthPixel(pickPixel.x(),pickPixel.y(),view.width(),view.height(),viewport);
        if (!pixel) return;
        std::optional<vcg::Point3d> hit;
        if (!pickSelection) {
            hit = meshlab_openaxis::pickDepth((*pixel)[0],(*pixel)[1]);
        } else if (auto *mesh = view.mm()) {
            if (!view.meshVisibilityMap.value(mesh->id(),false)) return;
            hit = meshlab_openaxis::pickLayerDepth((*pixel)[0],(*pixel)[1], [&] {
                auto *shared = view.mvc()->sharedDataContext();
                shared->setMeshTransformationMatrix(mesh->id(),mesh->cm.Tr);
                shared->draw(mesh->id(),view.context());
            });
            if (hit) {
                vcg::Box3<Scalarm> bounds; bounds.Add(mesh->cm.Tr,mesh->cm.bbox);
                if (!bounds.IsNull()) pickResult["bounds"] = {{"min",openaxis::vector_value(vector(bounds.min))},
                                                             {"max",openaxis::vector_value(vector(bounds.max))}};
            }
        }
        if (hit) pickResult["point"] = openaxis::vector_value(vector(*hit));
    }
    void drawPivot() {
        if (!pivot || !matricesValid) return;
        using namespace meshlab_openaxis::overlay;
        auto p=transform(sceneProjection,transform(sceneModel,{pivot->x,pivot->y,pivot->z,1}));
        if (!visible(p)) return;
        meshlab_openaxis::drawPivotDisc(p,view.devicePixelRatioF());
    }
    void paint(QPainter &painter) {
        if (!available()) return;
        painter.save();
        painter.setClipRect(view.rect());
        if (diagnostics && diagnostics->isVisible() && matricesValid) {
            const auto frame=collector.presentation();
            overlayExpiry=frame.expires_at;
            // Rasterize text on the CPU: MeshLab's native GL state can leave
            // QPainter's GL glyph rendering blank. Composite one finished image.
            const auto dpi=view.devicePixelRatioF();
            QImage image(QSize(int(std::ceil(view.width()*dpi)),int(std::ceil(view.height()*dpi))),QImage::Format_ARGB32_Premultiplied);
            image.setDevicePixelRatio(dpi); image.fill(Qt::transparent);
            QPainter raster(&image);
            meshlab_openaxis::overlay::draw(raster,frame,key(),sceneModel,sceneProjection,view.size(),openaxis::diagnostic_colors());
            raster.end();
            painter.drawImage(QPointF(),image);
        }
        painter.restore();
    }
};

OpenAxisController::Impl::Hub::Hub() : client(options(&scheduler)),
    session(client,*this,&collector,[this] {
        openaxis::NavigationOptions o;
        o.scheduler=&scheduler;
        o.observation=[this](const openaxis::NavigationContext &c) {
            return is_current(c) ? current->read() : std::nullopt;
        };
        return o;
    }()), connection(client,{[this] {
        openaxis::ConnectionMetadata m;
        m.tags={"app.meshlab","workspace.modeling"};
        m.capabilities={"navigation"};
        m.focused=current && current->eligible();
        return m;
    }}) {
    scheduler.beforeDispatch=[this] { refresh(); };
    QObject::connect(&timer,&QTimer::timeout,this,[this] { refresh(); });
    timer.start(100);
    qApp->installEventFilter(this);
    QObject::connect(qApp,&QGuiApplication::applicationStateChanged,this,[this] { refresh(); });
    connection.on_state=[this](const openaxis::ConnectionStatus &) {
        for (auto *v:views) v->updateDiagnostics();
    };
    collector.on_changed=[this] {
        if (current) current->view.update();
    };
    connection.start();
}
OpenAxisController::Impl::Hub::~Hub() {
    qApp->removeEventFilter(this);
    timer.stop(); scheduler.beforeDispatch={}; collector.on_changed={};
    connection.stop(); session.close();
}
bool OpenAxisController::Impl::Hub::eventFilter(QObject *,QEvent *event) {
    switch (event->type()) {
    case QEvent::WindowActivate: case QEvent::WindowDeactivate:
    case QEvent::FocusIn: case QEvent::FocusOut: case QEvent::MouseButtonPress:
    case QEvent::Show: case QEvent::Hide:
        // Native handlers first update MeshLab's active document/current pane.
        QTimer::singleShot(0,this,[this] { refresh(); });
        break;
    default: break;
    }
    return false;
}
void OpenAxisController::Impl::Hub::remove(Impl *v) {
    if (current==v) {
        session.cancel("viewport_closed");
        ++epoch;
        current=nullptr;
        collector.clear();
    }
    views.erase(std::remove(views.begin(),views.end(),v),views.end());
    refresh();
}
void OpenAxisController::Impl::Hub::refresh() {
    if (refreshing) return;
    refreshing=true;
    struct Guard { bool &flag; ~Guard() { flag=false; } } guard{refreshing};
    auto *window=qobject_cast<MainWindow *>(QApplication::activeWindow());
    // The diagnostics tool temporarily owns keyboard focus but remains bound
    // to its document. It must not create another transport or move the camera.
    if (!window) for (auto *v:views)
        if (v->diagnostics && QApplication::activeWindow()==v->diagnostics)
            window=v->view.mw();
    Impl *next=current;
    if (window) {
        next=nullptr;
        for (auto *v:views)
            if (v->view.mw()==window && window->GLA()==&v->view) { next=v; break; }
    }
    if (next!=current) {
        session.cancel("active_viewport_changed");
        ++epoch;
        if (current) { current->focused=false; current->pivot.reset(); current->view.update(); }
        collector.clear();
        current=next;
        if (current) { current->lastPose=nullptr; current->lastContext.clear(); current->view.update(); }
    }
    if (current) current->refreshCurrent();
    const bool focus=current && current->eligible();
    if (announcedFocus!=focus) {
        // Publish loss before accepting any further queued work. The metadata
        // provider also reads actual state when a reconnect handshake completes.
        announcedFocus=focus;
        try { connection.refresh_metadata(); }
        catch (...) { connection.stop(); connection.start(); }
    }
    for (auto *v:views) v->updateDiagnostics();
}
openaxis::NavigationContext OpenAxisController::Impl::Hub::capture_context() {
    return current ? current->capture_context() : openaxis::NavigationContext{};
}
bool OpenAxisController::Impl::Hub::is_current(const openaxis::NavigationContext &c) {
    return current && current->is_current(c);
}
std::unique_ptr<openaxis::NavigationCapture> OpenAxisController::Impl::Hub::begin_query(const openaxis::NavigationContext &c) {
    return is_current(c) ? current->begin_query(c) : nullptr;
}
openaxis::WriteResult OpenAxisController::Impl::Hub::apply_pose(const openaxis::NavigationContext &c,
    const openaxis::NavigationPose &p,const openaxis::Value &navigation,std::optional<openaxis::Vec3> pivot) {
    return is_current(c) ? current->apply_pose(c,p,navigation,pivot) : openaxis::WriteResult{};
}
void OpenAxisController::Impl::Hub::show_pivot(const openaxis::NavigationContext &c,std::optional<openaxis::Vec3> pivot) {
    if (current && (!pivot || is_current(c))) current->show_pivot(c,pivot);
}

OpenAxisController::OpenAxisController(GLArea &v) : impl(std::make_unique<Impl>(v)) {}
OpenAxisController::~OpenAxisController() = default;
void OpenAxisController::refresh() { impl->refresh(); }
void OpenAxisController::paint(QPainter &p) { impl->paint(p); }
void OpenAxisController::sampleDepth() { impl->sampleDepth(); }

#include "openaxis_controller.moc"
