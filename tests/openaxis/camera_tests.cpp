#include "openaxis_camera.h"
#include <iostream>

int main() {
    using namespace openaxis;
    for (Vec3 center : {Vec3{}, Vec3{3,-7,2}})
        for (Vec3 t : {Vec3{}, Vec3{12,-3,5}})
            for (Vec3 r : {Vec3{}, Vec3{.7,-1.3,.2}, Vec3{3.141592653589793,0,0}})
                for (double scale : {.001, .5, 1., 1000.})
                    for (double distance : {8., 1.75 / std::tan(.523598775598299)}) {
                        const auto q = Quat::from_rotvec(r);
                        const auto eye = meshlab_openaxis::eye(q,t,center,scale,distance);
                        // Independently apply the native GL transform: camera must land at origin.
                        const auto cameraSpace = center + q.rotate(eye + t - center) * scale - Vec3{0,0,distance};
                        const auto roundtrip = meshlab_openaxis::translation(q,eye,center,scale,distance);
                        if (cameraSpace.length() > 1e-8 || (roundtrip-t).length() > 1e-8) {
                            std::cerr << "Trackball/camera mapping failed\n"; return 1;
                        }
                    }
    std::cout << "Camera mapping: 96 perspective/orthographic, scale, rotation and center cases passed\n";
}
