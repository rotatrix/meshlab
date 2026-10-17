#pragma once
#include <vcg/space/box3.h>

namespace meshlab_openaxis {
// Match MeshLab's native selection buffers: face and vertex flags are
// independent, and deleted elements never participate. Coordinates include Tr.
template<class Mesh> auto selectionBounds(const Mesh &mesh) {
    vcg::Box3<typename Mesh::ScalarType> bounds;
    for (const auto &v : mesh.vert)
        if (!v.IsD() && v.IsS()) bounds.Add(mesh.Tr * v.cP());
    for (const auto &f : mesh.face)
        if (!f.IsD() && f.IsS())
            for (int i=0; i<3; ++i) bounds.Add(mesh.Tr * f.cP(i));
    return bounds;
}

// The native selection overlay disables depth writes and biases depth, so it
// cannot supply surface hits. Draw the same flags into the isolated pick FBO.
// Caller preserves GL state and supplies the native scene matrices/point size.
template<class Mesh> void drawSelectionDepth(const Mesh &mesh) {
    const auto vertex = [&](const auto &p) {
        const auto world = mesh.Tr * p;
        glVertex3d(world[0],world[1],world[2]);
    };
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
    glDepthRange(0,1);
    glDepthFunc(GL_LESS);
    glBegin(GL_TRIANGLES);
    for (const auto &f : mesh.face)
        if (!f.IsD() && f.IsS())
            for (int i=0; i<3; ++i) vertex(f.cP(i));
    glEnd();
    glBegin(GL_POINTS);
    for (const auto &v : mesh.vert)
        if (!v.IsD() && v.IsS()) vertex(v.cP());
    glEnd();
}
}
