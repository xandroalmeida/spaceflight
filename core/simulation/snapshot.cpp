#include "core/simulation/snapshot.hpp"

#include "core/units/constants.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace sf::simulation {

const CelestialBodySnapshot* SimulationSnapshot::find(celestial::BodyId id) const {
    const auto it = std::find_if(bodies.begin(), bodies.end(),
                                 [id](const CelestialBodySnapshot& b) { return b.id == id; });
    return it == bodies.end() ? nullptr : &*it;
}

std::string SimulationSnapshot::describe() const {
    std::ostringstream os;
    os << std::setprecision(10);
    os << "t = " << time.to_string() << "  (elapsed " << elapsed_coordinate.seconds() << " s, warp "
       << time_warp << "x)\n"
       << "spacecraft " << spacecraft.name << " relative to " << spacecraft.reference.name() << ":\n"
       << "  distance   " << spacecraft.distance_to_reference << " m  (altitude "
       << spacecraft.altitude << " m)\n"
       << "  speed      " << spacecraft.speed << " m/s\n"
       << "  |a|        " << spacecraft.acceleration.norm() << " m/s^2\n"
       << "  mass       " << spacecraft.mass << " kg  (propellant " << spacecraft.propellant
       << " kg, budget " << spacecraft.delta_v_budget << " m/s)\n"
       << "  apoapsis   " << spacecraft.elements.apoapsis_radius << " m\n"
       << "  periapsis  " << spacecraft.elements.periapsis_radius << " m\n"
       << "  ecc / inc  " << spacecraft.elements.eccentricity << " / "
       << spacecraft.elements.inclination * 180.0 / units::pi << " deg\n"
       << "  beta       " << spacecraft.beta << "   gamma - 1 "
       << spacecraft.lorentz_factor_minus_one << "\n"
       << "  proper dt  " << clock_difference.seconds() << " s\n"
       << "bodies: " << bodies.size();
    return os.str();
}

SnapshotBuilder::SnapshotBuilder(const ephemeris::EphemerisProvider& provider,
                                 const celestial::BodyCatalog& catalog,
                                 const gravity::ForceModel& forces, celestial::BodyId reference,
                                 time::CoordinateTime epoch, coordinates::ReferenceFrame frame)
    : provider_(provider),
      catalog_(catalog),
      forces_(forces),
      reference_(reference),
      epoch_(epoch),
      frame_(frame) {}

void SnapshotBuilder::set_propulsion(double dry_mass, double effective_exhaust_velocity) {
    dry_mass_ = dry_mass;
    effective_exhaust_velocity_ = effective_exhaust_velocity;
}

SimulationSnapshot SnapshotBuilder::build(const propagation::PropagationState& state,
                                          time::CoordinateTime t) const {
    SimulationSnapshot snapshot{};
    snapshot.time = t;
    snapshot.elapsed_coordinate = t - epoch_;
    snapshot.proper_time = state.proper_time;
    snapshot.clock_difference = snapshot.elapsed_coordinate - state.proper_time;
    snapshot.time_warp = time_warp_;

    snapshot.bodies.reserve(catalog_.size());
    for (const auto& body : catalog_.bodies()) {
        const auto body_state = provider_.state(body.id, t, frame_);
        snapshot.bodies.push_back(CelestialBodySnapshot{body.id, body.name,
                                                        body_state.state.position,
                                                        body_state.state.velocity, body.gm,
                                                        body.radius});
    }

    auto& craft = snapshot.spacecraft;
    craft.name = name_;
    craft.position = state.state.position;
    craft.velocity = state.state.velocity;
    craft.speed = state.state.velocity.norm();
    craft.mass = state.mass;
    craft.proper_time = state.proper_time;

    // One force evaluation per snapshot, not one per instrument.
    const auto force = forces_.evaluate(state, t);
    craft.acceleration = force.acceleration;
    // mass_flow_rate is -q; thrust = q * v_eff in the Newtonian limit.
    craft.thrust = effective_exhaust_velocity_ > 0.0
                       ? -force.mass_flow_rate * effective_exhaust_velocity_
                       : 0.0;

    if (dry_mass_ > 0.0) {
        craft.propellant = std::max(0.0, state.mass - dry_mass_);
        craft.delta_v_budget =
            craft.propellant > 0.0 && effective_exhaust_velocity_ > 0.0
                ? effective_exhaust_velocity_ * std::log(state.mass / dry_mass_)
                : 0.0;
    }

    craft.reference = reference_;
    const auto reference_state = provider_.state(reference_, t, frame_);
    craft.relative_position = state.state.position - reference_state.state.position;
    craft.relative_velocity = state.state.velocity - reference_state.state.velocity;
    craft.distance_to_reference = craft.relative_position.norm();

    if (const auto* body = snapshot.find(reference_); body != nullptr && body->radius > 0.0) {
        craft.altitude = craft.distance_to_reference - body->radius;
    } else {
        craft.altitude = craft.distance_to_reference;
    }

    const double gm = provider_.gravitational_parameter(reference_);
    if (craft.distance_to_reference > 0.0 && gm > 0.0) {
        craft.elements = trajectory::elements_from_state(
            coordinates::StateVector{craft.relative_position, craft.relative_velocity}, gm);
    }

    if (target_.has_value()) {
        const auto target_state = provider_.state(*target_, t, frame_);
        craft.target = target_;
        craft.target_distance = (state.state.position - target_state.state.position).norm();
        craft.target_relative_speed =
            (state.state.velocity - target_state.state.velocity).norm();
    }

    // Relativity readouts.  beta uses the speed in the integration frame, which is
    // the barycentric speed -- the only frame-independent statement available
    // until Milestone 4 defines the observer properly.
    craft.beta = craft.speed / units::c;
    if (craft.beta < 1.0) {
        const double s = std::sqrt(1.0 - craft.beta * craft.beta);
        craft.lorentz_factor = 1.0 / s;
        // gamma - 1 without ever forming the difference; see the header.
        craft.lorentz_factor_minus_one = (craft.beta * craft.beta) / (s * (1.0 + s));
    }

    return snapshot;
}

}  // namespace sf::simulation
