#include "tools/orbit-cli/scenario.hpp"

#include "core/celestial/body_catalog.hpp"
#include "tools/orbit-cli/json.hpp"

#include <sstream>

namespace orbitcli {
namespace {

using sf::math::Vec3;

Vec3 read_vec3(const json::Value& value, const std::string& context) {
    const auto& arr = value.as_array(context);
    if (arr.size() != 3) {
        throw json::ParseError(context + ": expected 3 components, got " + std::to_string(arr.size()));
    }
    return Vec3{arr[0].as_number(context), arr[1].as_number(context), arr[2].as_number(context)};
}

sf::celestial::BodyId read_body(const std::string& name, const std::string& context) {
    const auto lookup = sf::celestial::body_from_name(name);
    if (!lookup.ok) {
        throw json::ParseError(context + ": unknown body \"" + name + "\"");
    }
    return lookup.id;
}

}  // namespace

Scenario Scenario::load(const std::string& path) {
    const json::Value root = json::parse_file(path);
    if (!root.is_object()) {
        throw json::ParseError(path + ": top level must be an object");
    }

    Scenario s{};
    s.name = root.string_or("name", "unnamed");
    s.epoch_text = root.require("epoch", path).as_string("epoch");
    s.duration_seconds = root.require("duration_s", path).as_number("duration_s");

    if (const json::Value* bodies = root.get("bodies"); bodies != nullptr) {
        for (const auto& entry : bodies->as_array("bodies")) {
            s.bodies.push_back(read_body(entry.as_string("bodies[]"), "bodies[]"));
        }
    } else {
        s.bodies = sf::celestial::BodyCatalog::default_solar_system_ids();
    }
    if (s.bodies.empty()) {
        throw json::ParseError(path + ": \"bodies\" must list at least one body");
    }

    const json::Value& craft = root.require("spacecraft", path);
    s.relative_to = read_body(craft.string_or("relative_to", "Earth"), "spacecraft.relative_to");
    s.position = read_vec3(craft.require("position_m", "spacecraft"), "spacecraft.position_m");
    s.velocity = read_vec3(craft.require("velocity_ms", "spacecraft"), "spacecraft.velocity_ms");
    s.mass = craft.number_or("mass_kg", 1000.0);
    if (s.mass <= 0.0) {
        throw json::ParseError("spacecraft.mass_kg must be > 0");
    }

    if (const json::Value* perturbations = root.get("perturbations"); perturbations != nullptr) {
        if (const json::Value* j2 = perturbations->get("j2_bodies"); j2 != nullptr) {
            for (const auto& entry : j2->as_array("perturbations.j2_bodies")) {
                s.j2_bodies.push_back(
                    read_body(entry.as_string("perturbations.j2_bodies[]"),
                              "perturbations.j2_bodies[]"));
            }
        }
    }

    if (const json::Value* integ = root.get("integrator"); integ != nullptr) {
        auto& cfg = s.integrator;
        cfg.relative_tolerance = integ->number_or("rtol", cfg.relative_tolerance);
        cfg.absolute_tolerance_position =
            integ->number_or("atol_position_m", cfg.absolute_tolerance_position);
        cfg.absolute_tolerance_velocity =
            integ->number_or("atol_velocity_ms", cfg.absolute_tolerance_velocity);
        cfg.min_step = sf::time::Duration::seconds(integ->number_or("min_step_s", cfg.min_step.seconds()));
        cfg.max_step = sf::time::Duration::seconds(integ->number_or("max_step_s", cfg.max_step.seconds()));
        cfg.initial_step =
            sf::time::Duration::seconds(integ->number_or("initial_step_s", cfg.initial_step.seconds()));
        cfg.max_steps = static_cast<std::size_t>(
            integ->number_or("max_steps", static_cast<double>(cfg.max_steps)));
    }

    if (const json::Value* out = root.get("output"); out != nullptr) {
        s.samples = static_cast<int>(out->number_or("samples", s.samples));
        s.csv_path = out->string_or("csv", "");
    }
    if (s.samples < 1) {
        s.samples = 1;
    }

    return s;
}

std::string Scenario::describe() const {
    std::ostringstream os;
    os << "scenario   : " << name << "\n"
       << "epoch      : " << epoch_text << "\n"
       << "duration   : " << duration_seconds << " s\n"
       << "bodies     : ";
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        os << (i > 0 ? ", " : "") << bodies[i].name();
    }
    os << "\n";
    os << "J2         : ";
    if (j2_bodies.empty()) {
        os << "(none -- point masses only)";
    } else {
        for (std::size_t i = 0; i < j2_bodies.size(); ++i) {
            os << (i > 0 ? ", " : "") << j2_bodies[i].name();
        }
    }
    os << "\n"
       << "initial    : relative to " << relative_to.name() << " (J2000 axes)\n"
       << "  position : " << position << " m\n"
       << "  velocity : " << velocity << " m/s\n"
       << "  mass     : " << mass << " kg\n"
       << "tolerances : rtol " << integrator.relative_tolerance
       << ", atol_r " << integrator.absolute_tolerance_position << " m"
       << ", atol_v " << integrator.absolute_tolerance_velocity << " m/s";
    return os.str();
}

}  // namespace orbitcli
