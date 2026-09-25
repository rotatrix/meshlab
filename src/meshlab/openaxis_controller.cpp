#include <openaxis/navigation.hpp>
#include <openaxis/connection_manager.hpp>
#include <openaxis/logging.hpp>
#include "glarea.h"
#include "openaxis_controller.h"
#include "openaxis_camera.h"
#include "openaxis_picking.h"
#include <QApplication>
#include <QCursor>
#include <QPainter>
#include <QShortcut>
#include <QTimer>
#include <cmath>
#include <climits>
#include <limits>

namespace {

template<class T> openaxis::Vec3 vector(const vcg::Point3<T> &p) { return {p[0], p[1], p[2]}; }
vcg::Point3f point(openaxis::Vec3 p) { return {float(p.x), float(p.y), float(p.z)}; }
openaxis::Quat rotation(const vcg::Quaternionf &q) { return {q[0], q[1], q[2], q[3]}; }
class QtScheduler final : public QObject, public openaxis::Scheduler {
public:
    std::function<void()> beforeDispatch;
    void post(Callback callback) override {
        QMetaObject::invokeMethod(this, [this, callback = std::move(callback)] {
            if (beforeDispatch) beforeDispatch();
            callback();
        }, Qt::QueuedConnection);
    }
    void post_at(double deadline, Callback callback) override {
        post([this, deadline, callback = std::move(callback)] {
            const auto delay = int(std::clamp(std::ceil((deadline - openaxis::diagnostic_time()) * 1000), 0., double(INT_MAX)));
            QTimer::singleShot(delay, this, [this, callback] {
                if (beforeDispatch) beforeDispatch();
                callback();
            });
        });
    }
};
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
    GLArea &view;
    QtScheduler scheduler;
    SceneObserver sceneObserver;
    openaxis::OpenAxisClient client;
    openaxis::NavigationSession session;
    openaxis::OpenAxisConnectionManager connection;
    QTimer timer;
    bool focused = false, diagnostics = false;
    std::string lastContext;
    openaxis::Value lastPose;
    std::optional<openaxis::Vec3> pivot;
    std::uint64_t revision = 0;
    std::map<int, std::unique_ptr<meshlab_openaxis::MeshPicker<CMeshO>>> pickers;

    explicit Impl(GLArea &v) : view(v), client(options(&scheduler)),
        session(client, *this, nullptr, [this] {
            openaxis::NavigationOptions o;
            o.scheduler = &scheduler;
            o.observation = [this](const openaxis::NavigationContext &c) { return is_current(c) ? read() : std::nullopt; };
            return o;
        }()), connection(client, {[this] {
            openaxis::ConnectionMetadata m;
            m.tags = {"app.meshlab", "workspace.modeling"};
            m.capabilities = {"navigation"}; m.focused = focused;
            return m;
        }}) {
        scheduler.beforeDispatch = [this] { refresh(); };
        QObject::connect(&timer, &QTimer::timeout, &scheduler, [this] { refresh(); });
        timer.start(100);
        sceneObserver.changed = [this] {
            ++revision; pickers.clear(); session.cancel("scene_changed"); pivot.reset();
        };
        QObject::connect(view.md(), SIGNAL(meshSetChanged()), &sceneObserver, SLOT(notify()));
        QObject::connect(view.md(), SIGNAL(meshDocumentModified()), &sceneObserver, SLOT(notify()));
        QObject::connect(view.md(), SIGNAL(documentUpdated()), &sceneObserver, SLOT(notify()));
        QObject::connect(view.md(), SIGNAL(currentMeshChanged(int)), &sceneObserver, SLOT(notify()));
        auto *shortcut = new QShortcut(QKeySequence("Ctrl+Shift+O"), &view);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        QObject::connect(shortcut, &QShortcut::activated, &scheduler, [this] { diagnostics = !diagnostics; view.update(); });
        connection.on_state = [this](const openaxis::ConnectionStatus &) { if (diagnostics) view.update(); };
        connection.start();
    }
    ~Impl() override {
        timer.stop(); scheduler.beforeDispatch = {};
        connection.stop(); session.close();
    }
    bool available() const {
        return view.isVisible() && view.isCurrent() && view.isValid() && view.isEnabled() &&
            view.width() > 0 && view.height() > 0 && !view.md()->isBusy() &&
            !view._isRaster && !view.takeSnapTile && !view.currentEditor;
    }
    bool active() const {
        return available() && view.window()->isActiveWindow() &&
            !QApplication::activeModalWidget() && !QApplication::activePopupWidget();
    }
    std::string key() const {
        std::string result = std::to_string(revision) + "/" + std::to_string(view.width()) + "/" +
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
        const bool now = active();
        const auto context = key();
        if (context != lastContext || (focused && !now)) {
            session.cancel("viewport_changed"); pivot.reset(); lastContext = context;
        }
        if (focused != now) { focused = now; connection.refresh_metadata(); }
        if (auto p = read()) {
            const auto value = openaxis::pose_value(*p);
            if (!lastPose.is_null() && value != lastPose && focused) session.native_camera_changed();
            lastPose = value;
        }
    }
    openaxis::NavigationContext capture_context() override { return active() ? openaxis::NavigationContext(key()) : openaxis::NavigationContext{}; }
    bool is_current(const openaxis::NavigationContext &c) override {
        auto k = std::any_cast<std::string>(&c);
        return k && active() && *k == key();
    }
    struct Capture final : openaxis::NavigationCapture {
        Impl &owner; openaxis::NavigationContext context; std::optional<openaxis::Pose> initial;
        Capture(Impl &o, openaxis::NavigationContext c) : owner(o), context(std::move(c)), initial(o.read()) {}
        std::optional<openaxis::Pose> initial_observation() override { return initial; }
        openaxis::Value resolve(const std::string &name) override {
            if (!owner.is_current(context)) return nullptr;
            if (name == "camera.pose") return initial ? openaxis::pose_value(*initial) : openaxis::Value{};
            return owner.fact(name);
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
            const auto camera = read();
            if (!camera) return nullptr;
            const double px = center ? view.width()*.5 : cursor.x();
            const double py = center ? view.height()*.5 : cursor.y();
            const double x = (2*px/view.width()-1) * double(view.width())/view.height();
            const double y = 1-2*py/view.height();
            const auto q = openaxis::Quat::from_rotvec(camera->r);
            openaxis::Vec3 origin = camera->t, direction;
            if (camera->ortho_extent > 0) {
                origin = origin + q.rotate({x*camera->ortho_extent/2, y*camera->ortho_extent/2, 0});
                direction = q.rotate({0,0,-1});
            } else direction = q.rotate({x*std::tan(camera->fov/2), y*std::tan(camera->fov/2), -1}).normalized();
            double closest = std::numeric_limits<double>::infinity();
            openaxis::Value result = {{"markerPosition", {px,py}}};
            const bool selected = name.find(".selection") != std::string::npos;
            for (auto &mesh : view.md()->meshIterator()) {
                if (!view.meshVisibilityMap.value(mesh.id(), false) || (selected && &mesh != view.mm())) continue;
                if (mesh.cm.fn == 0 || mesh.cm.bbox.IsNull() || std::abs(mesh.cm.Tr.Determinant()) < 1e-20) continue;
                auto &picker = pickers[mesh.id()];
                if (!picker) picker = std::make_unique<meshlab_openaxis::MeshPicker<CMeshO>>(mesh.cm);
                const auto inverse = vcg::Inverse(mesh.cm.Tr);
                const Point3m localOrigin = inverse * Point3m(origin.x, origin.y, origin.z);
                Point3m localDirection;
                for (int i = 0; i < 3; ++i)
                    localDirection[i] = inverse[i][0]*direction.x + inverse[i][1]*direction.y + inverse[i][2]*direction.z;
                const auto hit = picker->pick(mesh.cm, localOrigin, localDirection);
                if (!hit) continue;
                const auto world = vector(mesh.cm.Tr * *hit);
                const double distance = (world - origin).dot(direction);
                if (distance < 0 || distance >= closest) continue;
                closest = distance;
                result["point"] = openaxis::vector_value(world);
                vcg::Box3<Scalarm> bounds; bounds.Add(mesh.cm.Tr,mesh.cm.bbox);
                result["bounds"] = {{"min", openaxis::vector_value(vector(bounds.min))}, {"max", openaxis::vector_value(vector(bounds.max))}};
            }
            return result;
        }
        // A missing surface hit lets the server select its configured fallback.
        return nullptr;
    }
    void paint(QPainter &painter) {
        if (!available()) return;
        painter.save();
        if (pivot) {
            const auto camera = read();
            if (camera) {
                const auto local = openaxis::Quat::from_rotvec(camera->r).inverse().rotate(*pivot - camera->t);
                if (local.z < 0) {
                    const double h = camera->ortho_extent > 0 ? camera->ortho_extent / 2 : -local.z * std::tan(camera->fov / 2);
                    const QPointF pixel(view.width()/2. + local.x / h * view.height()/2., view.height()/2. - local.y / h * view.height()/2.);
                    painter.setPen(QPen(Qt::black, 1.5)); painter.setBrush(Qt::green);
                    painter.drawEllipse(pixel, 4., 4.);
                }
            }
        }
        if (diagnostics) {
            const QString text = QString("OpenAxis: %1 | Focus: %2 | Gesture: %3")
                .arg(QString::fromStdString(connection.status().state)).arg(focused ? "yes" : "no").arg(session.active() ? "active" : "idle");
            painter.fillRect(QRect(8, 8, painter.fontMetrics().horizontalAdvance(text) + 16, 28), QColor(0,0,0,190));
            painter.setPen(Qt::white); painter.drawText(16, 27, text);
        }
        painter.restore();
    }
};

OpenAxisController::OpenAxisController(GLArea &v) : impl(std::make_unique<Impl>(v)) {}
OpenAxisController::~OpenAxisController() = default;
void OpenAxisController::refresh() { impl->refresh(); }
void OpenAxisController::paint(QPainter &p) { impl->paint(p); }

#include "openaxis_controller.moc"
