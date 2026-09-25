#include <vcg/complex/complex.h>
#include "openaxis_picking.h"
#include <chrono>
#include <iostream>

class Vertex;
class Face;
struct Types : vcg::UsedTypes<vcg::Use<Vertex>::AsVertexType, vcg::Use<Face>::AsFaceType> {};
class Vertex : public vcg::Vertex<Types, vcg::vertex::Coord3f, vcg::vertex::BitFlags> {};
class Face : public vcg::Face<Types, vcg::face::VertexRef, vcg::face::Normal3f, vcg::face::BitFlags> {};
class Mesh : public vcg::tri::TriMesh<std::vector<Vertex>, std::vector<Face>> {};

int main() {
    Mesh mesh;
    constexpr int side = 64;
    vcg::tri::Allocator<Mesh>::AddVertices(mesh, (side+1)*(side+1));
    vcg::tri::Allocator<Mesh>::AddFaces(mesh, side*side*2);
    for (int y=0; y<=side; ++y) for (int x=0; x<=side; ++x) {
        auto &v = mesh.vert[y*(side+1)+x];
        v.P() = vcg::Point3f(float(x),float(y),0);
        mesh.bbox.Add(v.P());
    }
    for (int y=0; y<side; ++y) for (int x=0; x<side; ++x) {
        const int a=y*(side+1)+x, b=a+1, c=a+side+1, d=c+1;
        auto &f = mesh.face[2*(y*side+x)], &g = mesh.face[2*(y*side+x)+1];
        f.V(0)=&mesh.vert[a]; f.V(1)=&mesh.vert[b]; f.V(2)=&mesh.vert[d];
        g.V(0)=&mesh.vert[a]; g.V(1)=&mesh.vert[d]; g.V(2)=&mesh.vert[c];
        f.N()=g.N()=vcg::Point3f(0,0,1);
    }
    mesh.face[2].SetS();
    meshlab_openaxis::MeshPicker<Mesh> picker(mesh);
    const auto start=std::chrono::steady_clock::now();
    for (int i=0; i<1000; ++i) {
        const float x=float(i%side)+.25f, y=float((i/side)%side)+.5f;
        const auto hit=picker.pick(mesh,{x,y,10},{0,0,-1});
        if (!hit || (*hit-vcg::Point3f(x,y,0)).Norm()>1e-4f) {
            std::cerr << "Pick failed at " << x << ',' << y << ": " << (hit ? "wrong position" : "no hit") << '\n';
            if (hit) std::cerr << hit->X() << ',' << hit->Y() << ',' << hit->Z() << '\n';
            return 1;
        }
    }
    if (picker.pick(mesh,{-1,-1,10},{0,0,-1}) || picker.pick(mesh,{1,1,10},{0,0,1})) return 2;
    // A rebuilt index must observe moved geometry, without changing selection flags.
    mesh.bbox.SetNull();
    for (auto &v : mesh.vert) { v.P().Z()=3; mesh.bbox.Add(v.P()); }
    meshlab_openaxis::MeshPicker<Mesh> rebuilt(mesh);
    const auto hit=rebuilt.pick(mesh,{1.25f,1.5f,10},{0,0,-1});
    if (!hit || std::abs(hit->Z()-3)>1e-4f) return 3;
    for (size_t i=0; i<mesh.face.size(); ++i) if (mesh.face[i].IsS() != (i==2)) return 4;
    mesh.face[0].SetD(); mesh.face[1].SetD(); mesh.fn-=2;
    meshlab_openaxis::MeshPicker<Mesh> deleted(mesh);
    if (deleted.pick(mesh,{.25f,.5f,10},{0,0,-1})) return 5;
    std::cout << "Upstream AABB tree: 8192 faces, 1000 repeated picks, misses, deletion and geometry rebuild passed in "
        << std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count() << " seconds\n";
}
