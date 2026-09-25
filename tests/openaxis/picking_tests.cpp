#include <QApplication>
#include <QGLPixelBuffer>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include "openaxis_picking.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

int main(int argc, char **argv) {
    QApplication app(argc,argv);
    QGLFormat format; format.setDepth(true);
    QGLPixelBuffer buffer(256,256,format);
    if (!buffer.isValid() || !buffer.makeCurrent()) {
        std::cerr << "No OpenGL test context available\n"; return 77;
    }
    glViewport(0,0,256,256);
    glEnable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(-1,1,-1,1,-2,2);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glClearDepth(1); glClear(GL_DEPTH_BUFFER_BIT);
    if (meshlab_openaxis::pickDepth(128,128)) return 1;
    glBegin(GL_TRIANGLES); glVertex3f(-1,-1,0); glVertex3f(1,-1,0); glVertex3f(0,1,0); glEnd();
    auto hit=meshlab_openaxis::pickDepth(128,128);
    if (!hit || hit->Norm()>1e-4) return 2;
    glClear(GL_DEPTH_BUFFER_BIT);
    glPointSize(9);
    glBegin(GL_POINTS); glVertex3f(0,0,.5f); glEnd();
    hit=meshlab_openaxis::pickDepth(128,128);
    if (!hit || std::abs(hit->Z()-.5)>1e-4 || meshlab_openaxis::pickDepth(16,16)) return 3;
    // Pick a point whose center is outside the sampled pixel but whose rendered
    // radius covers it. Selection-only depth must not overwrite the scene depth.
    hit=meshlab_openaxis::pickLayerDepth(128,128,[] {
        glPointSize(9);
        glBegin(GL_POINTS); glVertex3f(3.f/128.f,0,-.5f); glEnd();
    });
    if (!hit || std::abs(hit->Z()+.5)>1e-4) return 9;
    hit=meshlab_openaxis::pickDepth(128,128);
    if (!hit || std::abs(hit->Z()-.5)>1e-4) return 10;
    const int viewport[4]={10,20,800,600};
    auto pixel=meshlab_openaxis::depthPixel(100,50,400,300,viewport);
    if (!pixel || (*pixel)[0]!=210 || (*pixel)[1]!=519 || meshlab_openaxis::depthPixel(-1,0,400,300,viewport)) return 4;
    std::cout << "Native depth picking: triangle, point cloud, selection depth isolation, background and HiDPI mapping passed\n";
    if (argc>1) {
        // Optional regression input: the reported binary little-endian PLY with
        // float XYZ vertices and uchar/int triangle lists. Never modifies it.
        std::ifstream file(argv[1],std::ios::binary);
        std::string line; size_t vertices=0, faces=0;
        bool binary=false;
        while (std::getline(file,line)) {
            if (line=="format binary_little_endian 1.0") binary=true;
            std::istringstream fields(line); std::string a,b;
            fields>>a>>b;
            if(a=="element" && b=="vertex") fields>>vertices;
            if(a=="element" && b=="face") fields>>faces;
            if(line=="end_header") break;
        }
        if (!file || !binary || !vertices || !faces) return 5;
        std::vector<float> positions(vertices*3);
        file.read(reinterpret_cast<char*>(positions.data()),positions.size()*sizeof(float));
        std::vector<unsigned int> indices(faces*3);
        for (size_t i=0;i<faces;++i) {
            unsigned char count=0; file.read(reinterpret_cast<char*>(&count),1);
            if(count!=3) return 6;
            file.read(reinterpret_cast<char*>(indices.data()+i*3),3*sizeof(unsigned int));
        }
        if(!file) return 7;
        float low[3]={positions[0],positions[1],positions[2]}, high[3]={low[0],low[1],low[2]};
        for(size_t i=0;i<vertices;++i) for(int j=0;j<3;++j) {
            low[j]=std::min(low[j],positions[i*3+j]); high[j]=std::max(high[j],positions[i*3+j]);
        }
        float extent=std::max({high[0]-low[0],high[1]-low[1],high[2]-low[2]})*.55f;
        glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(-extent,extent,-extent,extent,-extent*2,extent*2);
        glMatrixMode(GL_MODELVIEW); glLoadIdentity();
        glTranslatef(-(low[0]+high[0])/2,-(low[1]+high[1])/2,-(low[2]+high[2])/2);
        auto *gl=QOpenGLContext::currentContext()->functions();
        GLuint buffers[2]; gl->glGenBuffers(2,buffers);
        gl->glBindBuffer(GL_ARRAY_BUFFER,buffers[0]);
        gl->glBufferData(GL_ARRAY_BUFFER,positions.size()*sizeof(float),positions.data(),GL_STATIC_DRAW);
        gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,buffers[1]);
        gl->glBufferData(GL_ELEMENT_ARRAY_BUFFER,indices.size()*sizeof(unsigned int),indices.data(),GL_STATIC_DRAW);
        glEnableClientState(GL_VERTEX_ARRAY); glVertexPointer(3,GL_FLOAT,0,nullptr);
        for (bool points : {false,true}) {
            glClear(GL_DEPTH_BUFFER_BIT);
            const auto start=std::chrono::steady_clock::now();
            if(points) glDrawArrays(GL_POINTS,0,GLsizei(vertices));
            else glDrawElements(GL_TRIANGLES,GLsizei(indices.size()),GL_UNSIGNED_INT,nullptr);
            glFinish();
            const auto rendered=std::chrono::steady_clock::now();
            int hits=0;
            for(int y=32;y<256;y+=32) for(int x=32;x<256;x+=32)
                if(meshlab_openaxis::pickDepth(x,y)) ++hits;
            const auto finished=std::chrono::steady_clock::now();
            std::cout << (points ? "Engine points" : "Engine triangles") << ": " << vertices << " vertices, " << faces
                << " faces; draw " << std::chrono::duration<double,std::milli>(rendered-start).count()
                << " ms; 49 depth picks " << std::chrono::duration<double,std::milli>(finished-rendered).count()
                << " ms; hits " << hits << '\n';
            if(!hits || glGetError()!=GL_NO_ERROR) return 8;
        }
        glDisableClientState(GL_VERTEX_ARRAY);
        gl->glDeleteBuffers(2,buffers);
    }
}
