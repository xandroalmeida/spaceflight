#include "app/presentation/scene/mesh.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace sf::app::mesh {
namespace {

constexpr double kTau = 2.0 * std::numbers::pi;

void push(MeshData& m, const Vec3& p, const Vec3& n, const Vec2& uv, const Vec3& t, double sign = 1.0) {
    MeshVertex v{};
    v.position[0] = static_cast<float>(p.x);
    v.position[1] = static_cast<float>(p.y);
    v.position[2] = static_cast<float>(p.z);
    const Vec3 nn = n.normalized();
    v.normal[0] = static_cast<float>(nn.x);
    v.normal[1] = static_cast<float>(nn.y);
    v.normal[2] = static_cast<float>(nn.z);
    v.uv[0] = static_cast<float>(uv.x);
    v.uv[1] = static_cast<float>(uv.y);
    v.tangent[0] = static_cast<float>(t.x);
    v.tangent[1] = static_cast<float>(t.y);
    v.tangent[2] = static_cast<float>(t.z);
    v.tangent[3] = static_cast<float>(sign);
    m.vertices.push_back(v);
}

void index3(MeshData& m, std::uint32_t a, std::uint32_t b, std::uint32_t c) {
    m.indices.push_back(a);
    m.indices.push_back(b);
    m.indices.push_back(c);
}

}  // namespace

MeshData sphere(double radius, double height, int radial_segments, int rings) {
    MeshData m;
    std::uint32_t point = 0;
    std::uint32_t this_row = 0;
    std::uint32_t prev_row = 0;
    for (int j = 0; j <= rings + 1; ++j) {
        const double v = static_cast<double>(j) / (rings + 1);
        const double w = std::sin(std::numbers::pi * v);
        const double y = height * 0.5 * std::cos(std::numbers::pi * v);
        for (int i = 0; i <= radial_segments; ++i) {
            const double u = static_cast<double>(i) / radial_segments;
            const double x = std::sin(u * kTau);
            const double z = std::cos(u * kTau);
            const Vec3 p{x * radius * w, y, z * radius * w};
            // The direction from the centre, which is the normal of the sphere;
            // a non-uniform scale is the renderer's normal matrix's business.
            push(m, p, Vec3{x * w, std::cos(std::numbers::pi * v), z * w}, Vec2{u, v}, Vec3{z, 0.0, -x});
            ++point;
            if (i > 0 && j > 0) {
                index3(m, prev_row + static_cast<std::uint32_t>(i) - 1, prev_row + static_cast<std::uint32_t>(i),
                       this_row + static_cast<std::uint32_t>(i) - 1);
                index3(m, prev_row + static_cast<std::uint32_t>(i), this_row + static_cast<std::uint32_t>(i),
                       this_row + static_cast<std::uint32_t>(i) - 1);
            }
        }
        prev_row = this_row;
        this_row = point;
    }
    return m;
}

MeshData cylinder(double top_radius, double bottom_radius, double height, int radial_segments, int rings,
                  bool cap_top, bool cap_bottom) {
    MeshData m;
    std::uint32_t point = 0;
    std::uint32_t this_row = 0;
    std::uint32_t prev_row = 0;
    const double side_normal_y = (bottom_radius - top_radius) / height;
    for (int j = 0; j <= rings + 1; ++j) {
        const double v = static_cast<double>(j) / (rings + 1);
        const double radius = top_radius + (bottom_radius - top_radius) * v;
        const double y = height * 0.5 - height * v;
        for (int i = 0; i <= radial_segments; ++i) {
            const double u = static_cast<double>(i) / radial_segments;
            const double x = std::sin(u * kTau);
            const double z = std::cos(u * kTau);
            push(m, Vec3{x * radius, y, z * radius}, Vec3{x, side_normal_y, z}, Vec2{u, v * 0.5}, Vec3{z, 0.0, -x});
            ++point;
            if (i > 0 && j > 0) {
                index3(m, prev_row + static_cast<std::uint32_t>(i) - 1, prev_row + static_cast<std::uint32_t>(i),
                       this_row + static_cast<std::uint32_t>(i) - 1);
                index3(m, prev_row + static_cast<std::uint32_t>(i), this_row + static_cast<std::uint32_t>(i),
                       this_row + static_cast<std::uint32_t>(i) - 1);
            }
        }
        prev_row = this_row;
        this_row = point;
    }

    if (cap_top && top_radius > 0.0) {
        const double y = height * 0.5;
        this_row = point;
        push(m, Vec3{0.0, y, 0.0}, Vec3{0.0, 1.0, 0.0}, Vec2{0.25, 0.75}, Vec3{1.0, 0.0, 0.0});
        ++point;
        for (int i = 0; i <= radial_segments; ++i) {
            const double r = static_cast<double>(i) / radial_segments;
            const double x = std::sin(r * kTau);
            const double z = std::cos(r * kTau);
            push(m, Vec3{x * top_radius, y, z * top_radius}, Vec3{0.0, 1.0, 0.0},
                 Vec2{(x + 1.0) * 0.25, 0.5 + (z + 1.0) * 0.25}, Vec3{1.0, 0.0, 0.0});
            ++point;
            if (i > 0) {
                index3(m, this_row, point - 1, point - 2);
            }
        }
    }

    if (cap_bottom && bottom_radius > 0.0) {
        const double y = -height * 0.5;
        this_row = point;
        push(m, Vec3{0.0, y, 0.0}, Vec3{0.0, -1.0, 0.0}, Vec2{0.75, 0.75}, Vec3{-1.0, 0.0, 0.0});
        ++point;
        for (int i = 0; i <= radial_segments; ++i) {
            const double r = static_cast<double>(i) / radial_segments;
            const double x = std::sin(r * kTau);
            const double z = std::cos(r * kTau);
            push(m, Vec3{x * bottom_radius, y, z * bottom_radius}, Vec3{0.0, -1.0, 0.0},
                 Vec2{0.5 + (x + 1.0) * 0.25, 1.0 - (z + 1.0) * 0.25}, Vec3{-1.0, 0.0, 0.0});
            ++point;
            if (i > 0) {
                index3(m, this_row, point - 2, point - 1);
            }
        }
    }
    return m;
}

MeshData box(const Vec3& size) {
    MeshData m;
    const Vec3 h = size * 0.5;
    // Six faces, each a quad seen from outside with its corners listed
    // top-left, top-right, bottom-right, bottom-left, and wound clockwise.
    struct Face {
        Vec3 normal;
        Vec3 right;
        Vec3 up;
    };
    const Face faces[] = {
        {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}},  {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
        {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}},  {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
        {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},   {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},
    };
    for (const auto& f : faces) {
        const auto base = static_cast<std::uint32_t>(m.vertices.size());
        const Vec3 centre{f.normal.x * h.x, f.normal.y * h.y, f.normal.z * h.z};
        const Vec3 r{f.right.x * h.x, f.right.y * h.y, f.right.z * h.z};
        const Vec3 u{f.up.x * h.x, f.up.y * h.y, f.up.z * h.z};
        push(m, centre - r + u, f.normal, Vec2{0.0, 0.0}, f.right);
        push(m, centre + r + u, f.normal, Vec2{1.0, 0.0}, f.right);
        push(m, centre + r - u, f.normal, Vec2{1.0, 1.0}, f.right);
        push(m, centre - r - u, f.normal, Vec2{0.0, 1.0}, f.right);
        index3(m, base + 0, base + 1, base + 2);
        index3(m, base + 0, base + 2, base + 3);
    }
    return m;
}

MeshData torus(double inner_radius, double outer_radius, int rings, int ring_segments) {
    MeshData m;
    const double min_radius = inner_radius;
    const double radius = (outer_radius - inner_radius) * 0.5;
    for (int i = 0; i <= rings; ++i) {
        const auto prev_row = static_cast<std::uint32_t>((i - 1) * (ring_segments + 1));
        const auto this_row = static_cast<std::uint32_t>(i * (ring_segments + 1));
        const double inci = static_cast<double>(i) / rings;
        const double angi = inci * kTau;
        const Vec2 normali{-std::sin(angi), -std::cos(angi)};   // (x, z)
        for (int j = 0; j <= ring_segments; ++j) {
            const double incj = static_cast<double>(j) / ring_segments;
            const double angj = incj * kTau;
            const Vec2 normalj{-std::cos(angj), std::sin(angj)};
            const Vec2 normalk = normalj * radius + Vec2{min_radius + radius, 0.0};
            push(m, Vec3{normali.x * normalk.x, normalk.y, normali.y * normalk.x},
                 Vec3{normali.x * normalj.x, normalj.y, normali.y * normalj.x}, Vec2{inci, incj},
                 Vec3{-std::cos(angi), 0.0, std::sin(angi)});
            if (i > 0 && j > 0) {
                const auto J = static_cast<std::uint32_t>(j);
                index3(m, this_row + J - 1, prev_row + J, prev_row + J - 1);
                index3(m, this_row + J - 1, this_row + J, prev_row + J);
            }
        }
    }
    return m;
}

MeshData quad(const Vec2& size) {
    MeshData m;
    const double w = size.x * 0.5;
    const double h = size.y * 0.5;
    const Vec3 n{0.0, 0.0, 1.0};
    const Vec3 t{1.0, 0.0, 0.0};
    push(m, Vec3{-w, h, 0.0}, n, Vec2{0.0, 0.0}, t);
    push(m, Vec3{w, h, 0.0}, n, Vec2{1.0, 0.0}, t);
    push(m, Vec3{w, -h, 0.0}, n, Vec2{1.0, 1.0}, t);
    push(m, Vec3{-w, -h, 0.0}, n, Vec2{0.0, 1.0}, t);
    index3(m, 0, 1, 2);
    index3(m, 0, 2, 3);
    return m;
}

MeshData triangles(const std::vector<Vec3>& positions, const std::vector<Vec3>& normals,
                   const std::vector<Vec2>& uvs) {
    MeshData m;
    for (std::size_t i = 0; i < positions.size(); ++i) {
        const Vec3 n = i < normals.size() ? normals[i] : Vec3{0.0, 0.0, 1.0};
        Vec3 t = cross(Vec3{0.0, 1.0, 0.0}, n);
        if (t.norm() < 1.0e-6) {
            t = Vec3{1.0, 0.0, 0.0};
        }
        push(m, positions[i], n, i < uvs.size() ? uvs[i] : Vec2{}, t.normalized());
        m.indices.push_back(static_cast<std::uint32_t>(i));
    }
    return m;
}

MeshData instanced(const MeshData& part, const std::vector<Transform3>& transforms) {
    MeshData m;
    for (const auto& t : transforms) {
        const auto base = static_cast<std::uint32_t>(m.vertices.size());
        const Basis normal_basis = t.basis.inverse().transposed();
        for (const auto& v : part.vertices) {
            MeshVertex out = v;
            const Vec3 p = t * widen(v.position);
            const Vec3 n = (normal_basis * widen(v.normal)).normalized();
            const Vec3 tg = (t.basis * widen(v.tangent)).normalized();
            out.position[0] = static_cast<float>(p.x);
            out.position[1] = static_cast<float>(p.y);
            out.position[2] = static_cast<float>(p.z);
            out.normal[0] = static_cast<float>(n.x);
            out.normal[1] = static_cast<float>(n.y);
            out.normal[2] = static_cast<float>(n.z);
            out.tangent[0] = static_cast<float>(tg.x);
            out.tangent[1] = static_cast<float>(tg.y);
            out.tangent[2] = static_cast<float>(tg.z);
            m.vertices.push_back(out);
        }
        for (const auto index : part.indices) {
            m.indices.push_back(base + index);
        }
    }
    return m;
}

Aabb bounds(const MeshData& mesh) { return transformed_bounds(mesh, Transform3{}); }

Aabb transformed_bounds(const MeshData& mesh, const Transform3& transform) {
    constexpr double inf = std::numeric_limits<double>::infinity();
    Aabb box{{inf, inf, inf}, {-inf, -inf, -inf}};
    for (const auto& v : mesh.vertices) {
        const Vec3 p = transform * widen(v.position);
        box.min = Vec3{std::min(box.min.x, p.x), std::min(box.min.y, p.y), std::min(box.min.z, p.z)};
        box.max = Vec3{std::max(box.max.x, p.x), std::max(box.max.y, p.y), std::max(box.max.z, p.z)};
    }
    return box;
}

}  // namespace sf::app::mesh
