#pragma once
// Homogeneous clipping follows the PrusaSlicer OpenAxis overlay integration.
#include <openaxis/diagnostics.hpp>
#include <QPainter>
#include <algorithm>
#include <array>
#include <cmath>

namespace meshlab_openaxis::overlay {
using Clip = std::array<double, 4>;
struct Viewport {
    double x, y, width, height, framebuffer_height, scale;
    bool valid() const { return width > 0 && height > 0 && scale > 0; }
    double top() const { return framebuffer_height - y - height; }
    std::array<double, 2> pixel(double px, double py) const { return {px / scale, py / scale}; }
    std::array<double, 2> project(const Clip &p) const {
        return pixel(x + (p[0] / p[3] + 1) * width / 2, top() + (1 - p[1] / p[3]) * height / 2);
    }
};
inline bool finite(const Clip &p) {
    return std::all_of(p.begin(), p.end(), [](double v) { return std::isfinite(v); });
}
inline bool visible(const Clip &p) {
    return finite(p) && p[3] > 1e-12 && std::abs(p[0]) <= p[3] && std::abs(p[1]) <= p[3] &&
           std::abs(p[2]) <= p[3];
}
// Clip before perspective division, including segments crossing the eye plane.
inline bool clip_segment(Clip &a, Clip &b) {
    if (!finite(a) || !finite(b))
        return false;
    double low = 0, high = 1;
    for (int axis = 0; axis < 3; ++axis)
        for (double sign : {-1., 1.}) {
            double pa = a[3] + sign * a[axis], pb = b[3] + sign * b[axis];
            if (pa < 0 && pb < 0)
                return false;
            if (pa < 0)
                low = std::max(low, pa / (pa - pb));
            if (pb < 0)
                high = std::min(high, pa / (pa - pb));
        }
    if (low > high)
        return false;
    auto original = a;
    for (int i = 0; i < 4; ++i) {
        double d = b[i] - original[i];
        a[i] = original[i] + low * d;
        b[i] = original[i] + high * d;
    }
    return a[3] > 1e-12 && b[3] > 1e-12;
}
} // namespace meshlab_openaxis::overlay

namespace meshlab_openaxis::overlay {
inline Clip transform(const double matrix[16], const Clip &p) {
    Clip result{};
    for (int row=0; row<4; ++row)
        for (int col=0; col<4; ++col) result[row] += matrix[col*4+row]*p[col];
    return result;
}
// Draw detached SDK evidence only. Positions are logical viewport pixels;
// QImage's device pixel ratio handles display scaling exactly once.
inline void draw(QPainter &painter, const openaxis::DiagnosticPresentation &frame,
                 const std::string &context, const double model[16], const double projection[16],
                 const QSize &size, const std::map<std::string, std::array<int,3>> &palette) {
    if (frame.context != context || size.isEmpty()) return;
    painter.save();
    painter.setClipRect(QRect(QPoint(),size));
    painter.setRenderHint(QPainter::Antialiasing);
    QFont font=painter.font(); font.setPixelSize(12); painter.setFont(font);
    auto color = [&](const std::string &tone, double opacity=1.) {
        const auto &rgb=palette.at(tone);
        QColor c(rgb[0],rgb[1],rgb[2]); c.setAlphaF(std::clamp(opacity,0.,1.)); return c;
    };
    auto clip = [&](openaxis::Vec3 p) { return transform(projection,transform(model,{p.x,p.y,p.z,1})); };
    const Viewport viewport{0,0,double(size.width()),double(size.height()),double(size.height()),1};
    auto pixel = [&](const Clip &p) { auto xy=viewport.project(p); return QPointF(xy[0],xy[1]); };
    auto text = [&](QPointF pos, const QString &label, QColor ink) {
        const QRectF box(pos,QSizeF(painter.fontMetrics().horizontalAdvance(label)+6,15));
        painter.fillRect(box.adjusted(-2,0,0,0),QColor(0,0,0,190));
        painter.setPen(ink); painter.drawText(pos+QPointF(0,painter.fontMetrics().ascent()),label);
    };
    for (const auto &s:frame.segments) {
        auto a=clip(s.start), b=clip(s.end);
        if (!clip_segment(a,b)) continue;
        painter.setPen(QPen(color(s.tone,s.opacity),s.width));
        painter.drawLine(pixel(a),pixel(b));
    }
    for (const auto &m:frame.markers) {
        if (!std::isfinite(m.point[0]) || !std::isfinite(m.point[1])) continue;
        QPointF pos(m.point[0],m.point[1]);
        if (!QRectF(QPointF(),QSizeF(size)).contains(pos)) continue;
        for (bool outline:{true,false}) {
            painter.setPen(QPen(outline?QColor(0,0,0,166):color(m.tone,.65),outline?4:2));
            painter.drawLine(pos-QPointF(9,0),pos+QPointF(9,0));
            painter.drawLine(pos-QPointF(0,9),pos+QPointF(0,9));
        }
        auto labelPos=pos+QPointF(12,-8);
        for (const auto &label:QString::fromStdString(m.label).split('\n')) {
            text(labelPos,label,color(m.tone,.65)); labelPos.ry()+=15;
        }
    }
    QPointF row(16,16);
    for (const auto &line:frame.lines) {
        text(row,QString::fromStdString(line.text),color(line.tone)); row.ry()+=15;
    }
    painter.restore();
}
}

