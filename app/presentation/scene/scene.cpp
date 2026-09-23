#include "app/presentation/scene/scene.hpp"

#include "app/presentation/format.hpp"

namespace sf::app {

const std::string& MeshLibrary::add(const std::string& name, MeshData mesh) {
    auto [it, inserted] = meshes_.try_emplace(name, std::move(mesh));
    auto [name_it, _] = names_.try_emplace(name, name);
    (void)it;
    (void)inserted;
    return name_it->second;
}

const std::string& MeshLibrary::sphere(double radius, double height, int radial_segments, int rings) {
    const std::string key = fmt::format("sphere:%.6g:%.6g:%d:%d", radius, height, radial_segments, rings);
    if (const auto it = names_.find(key); it != names_.end()) {
        return it->second;
    }
    return add(key, mesh::sphere(radius, height, radial_segments, rings));
}

const std::string& MeshLibrary::cylinder(double top_radius, double bottom_radius, double height,
                                         int radial_segments, int rings, bool cap_top, bool cap_bottom) {
    const std::string key = fmt::format("cylinder:%.6g:%.6g:%.6g:%d:%d:%d:%d", top_radius, bottom_radius, height,
                                        radial_segments, rings, cap_top ? 1 : 0, cap_bottom ? 1 : 0);
    if (const auto it = names_.find(key); it != names_.end()) {
        return it->second;
    }
    return add(key, mesh::cylinder(top_radius, bottom_radius, height, radial_segments, rings, cap_top, cap_bottom));
}

const std::string& MeshLibrary::box(const Vec3& size) {
    const std::string key = fmt::format("box:%.6g:%.6g:%.6g", size.x, size.y, size.z);
    if (const auto it = names_.find(key); it != names_.end()) {
        return it->second;
    }
    return add(key, mesh::box(size));
}

const std::string& MeshLibrary::torus(double inner_radius, double outer_radius, int rings, int ring_segments) {
    const std::string key = fmt::format("torus:%.6g:%.6g:%d:%d", inner_radius, outer_radius, rings, ring_segments);
    if (const auto it = names_.find(key); it != names_.end()) {
        return it->second;
    }
    return add(key, mesh::torus(inner_radius, outer_radius, rings, ring_segments));
}

const std::string& MeshLibrary::quad(const Vec2& size) {
    const std::string key = fmt::format("quad:%.6g:%.6g", size.x, size.y);
    if (const auto it = names_.find(key); it != names_.end()) {
        return it->second;
    }
    return add(key, mesh::quad(size));
}

const MeshData* MeshLibrary::find(const std::string& name) const {
    const auto it = meshes_.find(name);
    return it != meshes_.end() ? &it->second : nullptr;
}

}  // namespace sf::app
