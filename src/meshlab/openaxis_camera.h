#pragma once
#include <openaxis/geometry.hpp>

namespace meshlab_openaxis {
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
