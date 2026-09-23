#pragma once

// Meshes built on the CPU, in the conventions of the primitives the scene was
// designed with (they were Godot's, and every "the cylinder grows along +y"
// comment in the ship and the cockpit depends on them):
//
//   * a cylinder or cone stands along +Y, top at +height/2;
//   * a sphere's pole is +Y, texture u goes round from +Z (u = 0) through +X
//     (u = 0.25), v runs down from the north pole;
//   * a quad lies in XY, facing +Z;
//   * a torus lies in XZ, around Y;
//   * FRONT faces wind CLOCKWISE as seen by the viewer.
//
// Pure data: nothing here touches a GPU, so the tests can measure a mesh (the
// plume test checks its bounding box, which is what the renderer draws).

#include "app/presentation/geometry.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace sf::app {

struct MeshVertex {
    float position[3];
    float normal[3];
    float uv[2];
    float tangent[4];   // xyz, and the bitangent sign in w
};

struct MeshData {
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
};

struct Aabb {
    Vec3 min{};
    Vec3 max{};
    [[nodiscard]] Vec3 size() const { return max - min; }
};

namespace mesh {

MeshData sphere(double radius, double height, int radial_segments, int rings);
MeshData cylinder(double top_radius, double bottom_radius, double height, int radial_segments, int rings,
                  bool cap_top, bool cap_bottom);
MeshData box(const Vec3& size);
MeshData torus(double inner_radius, double outer_radius, int rings, int ring_segments);
MeshData quad(const Vec2& size);
// Triangles as given (three vertices each), with the normals and uvs supplied.
MeshData triangles(const std::vector<Vec3>& positions, const std::vector<Vec3>& normals,
                   const std::vector<Vec2>& uvs);
// Every instance of `part` placed by `transforms`, merged into one mesh.
MeshData instanced(const MeshData& part, const std::vector<Transform3>& transforms);

[[nodiscard]] Aabb bounds(const MeshData& mesh);
[[nodiscard]] Aabb transformed_bounds(const MeshData& mesh, const Transform3& transform);

}  // namespace mesh
}  // namespace sf::app
