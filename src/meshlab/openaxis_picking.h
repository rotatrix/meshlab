#pragma once
#include <vcg/space/point3.h>
#include <wrap/gl/picking.h>
#include <QGLFramebufferObject>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <array>
#include <cmath>
#include <optional>

namespace meshlab_openaxis {
inline std::optional<std::array<int, 2>> depthPixel(double x, double y,
    double width, double height, const int viewport[4]) {
    if (width <= 0 || height <= 0 || x < 0 || y < 0 || x >= width || y >= height ||
        viewport[2] <= 0 || viewport[3] <= 0) return {};
    return std::array<int, 2>{viewport[0] + int(x*viewport[2]/width),
        viewport[1] + viewport[3] - 1 - int(y*viewport[3]/height)};
}
// MeshLab's own surface picker: one depth sample, unprojected through the
// current scene matrices. Works for every primitive that writes visible depth.
inline std::optional<vcg::Point3d> pickDepth(int x, int y) {
    vcg::Point3d hit;
    if (!vcg::Pick(x,y,hit)) return {};
    for (int i=0; i<3; ++i) if (!std::isfinite(hit[i])) return {};
    return hit;
}
template<class Draw> std::optional<vcg::Point3d> pickLayerDepth(int x, int y, Draw draw) {
    GLint viewport[4], framebuffer, matrixMode;
    glGetIntegerv(GL_VIEWPORT,viewport);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING,&framebuffer);
    glGetIntegerv(GL_MATRIX_MODE,&matrixMode);
    // Keep the full viewport projection: cropping to one pixel would clip point
    // centers whose rendered point size still covers the requested pixel.
    QGLFramebufferObject buffer(viewport[2],viewport[3],QGLFramebufferObject::Depth);
    if (!buffer.isValid() || !buffer.bind()) {
        QOpenGLContext::currentContext()->functions()->glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
        return {};
    }
    struct Restore {
        GLint framebuffer, matrixMode;
        ~Restore() {
            glMatrixMode(GL_MODELVIEW); glPopMatrix();
            glMatrixMode(GL_PROJECTION); glPopMatrix();
            glMatrixMode(matrixMode);
            glPopAttrib();
            QOpenGLContext::currentContext()->functions()->glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
        }
    } restore{framebuffer,matrixMode};
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glMatrixMode(GL_PROJECTION); glPushMatrix();
    glMatrixMode(GL_MODELVIEW); glPushMatrix();
    glViewport(0,0,viewport[2],viewport[3]);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glClearDepth(1);
    glClear(GL_DEPTH_BUFFER_BIT);
    draw();
    return pickDepth(x-viewport[0],y-viewport[1]);
}
}
