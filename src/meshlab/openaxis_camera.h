#pragma once
#include <openaxis/geometry.hpp>

namespace meshlab_openaxis {
// Two-sided Moller-Trumbore intersection, in world coordinates.
inline std::optional<double> intersect(openaxis::Vec3 origin, openaxis::Vec3 direction,
    openaxis::Vec3 a, openaxis::Vec3 b, openaxis::Vec3 c) {
    const auto e1 = b-a, e2 = c-a;
    const auto h = direction.cross(e2);
    const double det = e1.dot(h);
    if (std::abs(det) <= 1e-12 * e1.length() * e2.length()) return {};
    const auto s = origin-a;
    const double u = s.dot(h)/det;
    if (u < 0 || u > 1) return {};
    const auto q = s.cross(e1);
    const double v = direction.dot(q)/det;
    if (v < 0 || u+v > 1) return {};
    const double t = e2.dot(q)/det;
    return t > 0 ? std::optional<double>(t) : std::nullopt;
}
// MeshLab renders T(-distance Z) T(center) scale rotation T(translation-center).
inline openaxis::Vec3 eye(openaxis::Quat rotation, openaxis::Vec3 translation,
                         openaxis::Vec3 center, double scale, double distance) {
    return center - translation + rotation.inverse().rotate(
        (openaxis::Vec3{0, 0, distance} - center) * (1 / scale));
}
inline openaxis::Vec3 translation(openaxis::Quat rotation, openaxis::Vec3 eye,
                                 openaxis::Vec3 center, double scale, double distance) {
    return center - eye + rotation.inverse().rotate(
        (openaxis::Vec3{0, 0, distance} - center) * (1 / scale));
}
}
