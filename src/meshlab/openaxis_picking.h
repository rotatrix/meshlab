#pragma once
#include <vcg/complex/algorithms/closest.h>
#include <vcg/space/index/aabb_binary_tree/aabb_binary_tree.h>
#include <optional>
#include <vector>

namespace meshlab_openaxis {
// The upstream AABB tree stores face pointers. Rebuild after geometry/topology edits;
// camera movement and mesh transforms do not invalidate this local-space index.
template<class Mesh> class MeshPicker {
    using Scalar = typename Mesh::ScalarType;
    using Point = typename Mesh::CoordType;
    vcg::AABBBinaryTreeIndex<typename Mesh::FaceType, Scalar> tree;
public:
    explicit MeshPicker(Mesh &mesh) {
        std::vector<typename Mesh::FaceType *> faces;
        faces.reserve(mesh.fn);
        for (auto &face : mesh.face) if (!face.IsD()) faces.push_back(&face);
        tree.Set(faces.begin(), faces.end());
    }
    std::optional<Point> pick(Mesh &mesh, Point origin, Point direction) {
        if (direction.SquaredNorm() == 0) return {};
        direction.Normalize();
        const Scalar maxDistance = (origin - mesh.bbox.Center()).Norm() + mesh.bbox.Diag();
        Scalar distance = 0;
        // Unlike the grid, the tree does not require face marks. Avoid enabling
        // optional MeshLab face attributes or modifying mesh/selection state.
        vcg::tri::EmptyTMark<Mesh> marker;
        vcg::RayTriangleIntersectionFunctor<true> intersection;
        const auto *face = tree.DoRay(intersection, marker, vcg::Ray3<Scalar>(origin, direction), maxDistance, distance);
        if (!face || distance < 0) return {};
        return origin + direction * distance;
    }
};
}
