#pragma once
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <cmath>
#include <array>
namespace meshlab_openaxis {
inline void drawPivotDisc(const std::array<double,4> &p, double dpi) {
        GLint viewport[4], mode;
        glGetIntegerv(GL_VIEWPORT,viewport); glGetIntegerv(GL_MATRIX_MODE,&mode);
        if (viewport[2]<=0 || viewport[3]<=0) return;
        const double x=p[0]/p[3], y=p[1]/p[3], z=p[2]/p[3];
        const double sx=2*dpi/viewport[2], sy=2*dpi/viewport[3];
        glPushAttrib(GL_ALL_ATTRIB_BITS);
        auto *functions=QOpenGLContext::currentContext()->functions();
        GLint program; glGetIntegerv(GL_CURRENT_PROGRAM,&program);
        functions->glUseProgram(0);
        glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
        glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
        glDisable(GL_LIGHTING); glDisable(GL_TEXTURE_2D); glDisable(GL_CULL_FACE);
        glDisable(GL_ALPHA_TEST); glDisable(GL_STENCIL_TEST); glDisable(GL_FOG);
        for (int i=0;i<6;++i) glDisable(GL_CLIP_PLANE0+i);
        glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_DEPTH_TEST); glDepthMask(GL_FALSE);
        auto vertex=[&](double radius,int i) {
            double angle=2*3.141592653589793*i/48;
            glVertex3d(x+radius*sx*std::cos(angle),y+radius*sy*std::sin(angle),z);
        };
        for (bool occluded:{true,false}) {
            glDepthFunc(occluded?GL_GREATER:GL_LEQUAL);
            float alpha=occluded?.23f:1.f;
            glColor4f(0,1,0,alpha);
            glBegin(GL_TRIANGLE_FAN); glVertex3d(x,y,z);
            for (int i=0;i<=48;++i) vertex(4,i);
            glEnd();
            // A separate annulus avoids double blending under translucent green.
            glColor4f(0,0,0,alpha); glBegin(GL_TRIANGLE_STRIP);
            for (int i=0;i<=48;++i) { vertex(4,i); vertex(5.5,i); }
            glEnd();
        }
        glMatrixMode(GL_MODELVIEW); glPopMatrix();
        glMatrixMode(GL_PROJECTION); glPopMatrix(); glMatrixMode(mode);
        glPopAttrib();
        functions->glUseProgram(program);
}
}

