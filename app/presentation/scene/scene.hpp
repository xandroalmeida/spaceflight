#pragma once

// What the near field is made of: parts, materials and lights, as data.
//
// The renderer reads these and draws them; nothing here knows there is a GPU.
// A part is a mesh (by name, from the MeshLibrary), a transform in its parent's
// frame and a material.

#include "app/presentation/geometry.hpp"
#include "app/presentation/palette.hpp"
#include "app/presentation/scene/mesh.hpp"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace sf::app {

enum class Blend { Opaque, Alpha, Additive, Plume };
enum class Cull { Back, None };

// A physically based material in the metallic-roughness form, plus the few
// switches the scene uses. Colours are sRGB-encoded; the renderer linearises.
struct Material {
    Colour albedo{1.0F, 1.0F, 1.0F, 1.0F};
    std::string albedo_texture;     // relative to the asset directory; empty = none
    Vec2 uv_scale{1.0, 1.0};
    double roughness{1.0};
    double metallic{0.0};
    Colour emission{0.0F, 0.0F, 0.0F, 1.0F};
    double emission_energy{0.0};
    Blend blend{Blend::Opaque};
    Cull cull{Cull::Back};
    bool unshaded{false};
    // A texture the renderer owns and draws every frame: "display:<n>" is a
    // cockpit display, "caption:<n>" a control's label.
    std::string dynamic_texture;
    bool casts_shadow{true};
    // Blend::Plume only: what the exhaust shader draws (shaders/plume.frag).
    // Colours are sRGB-encoded like the rest; `near` is at the nozzle, `far` at
    // the tail.
    struct PlumeLook {
        Colour near{1.0F, 1.0F, 1.0F, 1.0F};
        Colour far{1.0F, 1.0F, 1.0F, 1.0F};
        double energy{1.0};
        double edge_power{1.5};    // > 1 concentrates the light on the axis
        double decay{2.0};         // e-folds of brightness from nozzle to tail; 0 = none
        double tail_fade{0.6};     // where the far end starts fading to nothing; >= 1 = never
        double turbulence{0.2};    // amplitude of the streaks carried by the flow
        double streaks{6.0};       // streak frequency along the jet
        double flow_speed{3.0};    // how fast they travel, in plume lengths per second
        double time{0.0};
    } plume;
};

struct Part {
    std::string mesh;       // key into the MeshLibrary
    Transform3 transform{};
    Material material{};
    bool visible{true};
};

struct PointLight {
    Vec3 position{};        // body frame, metres
    Colour colour{1.0F, 1.0F, 1.0F, 1.0F};
    double energy{1.0};
    double range{5.0};
};

// Every mesh the scene uses, built once and named by what it is.
class MeshLibrary {
public:
    // The name is derived from the parameters, so two parts asking for the same
    // cylinder share one mesh.
    const std::string& sphere(double radius, double height, int radial_segments, int rings);
    const std::string& cylinder(double top_radius, double bottom_radius, double height, int radial_segments,
                                int rings, bool cap_top, bool cap_bottom);
    const std::string& box(const Vec3& size);
    const std::string& torus(double inner_radius, double outer_radius, int rings, int ring_segments);
    const std::string& quad(const Vec2& size);
    const std::string& add(const std::string& name, MeshData mesh);

    [[nodiscard]] const MeshData* find(const std::string& name) const;
    [[nodiscard]] const std::map<std::string, MeshData>& all() const { return meshes_; }

private:
    const std::string& intern(const std::string& key, MeshData (*make)(const MeshLibrary&), MeshData mesh);
    std::map<std::string, MeshData> meshes_;
    std::map<std::string, std::string> names_;
};

}  // namespace sf::app
