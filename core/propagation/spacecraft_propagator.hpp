#pragma once

// Propagation interface and its configuration/reporting types.
//
// The timestep is chosen by error control, never by the frame rate and never by
// the time warp factor (rule section 12).  SimulationClock asks for "advance to
// time T"; how many internal steps that takes is the propagator's business.

#include "core/propagation/propagation_state.hpp"
#include "core/time/coordinate_time.hpp"
#include "core/time/duration.hpp"

#include <cstddef>
#include <functional>
#include <string>

namespace sf::propagation {

// Which equations of motion the integrator uses.
//
// The choice is deliberate and per-propagation, not global: the Newtonian path
// is what every Solar System scenario wants, and the relativistic one is only
// valid in flat spacetime (docs/physics/relativistic-propulsion.md section 8).
enum class Kinematics {
    Newtonian,            // integrates v; dtau/dt = 1
    SpecialRelativistic,  // integrates u = gamma*v; dtau/dt = 1/gamma; flat spacetime
    // Integrates u = dx/dtau along a geodesic of the weak-field metric. Gravity
    // is carried by the metric, not by a force, so the force model must contain
    // only thrust. See docs/physics/relativistic-gravity.md.
    WeakFieldStaticMetric
};

// True for the modes whose velocity slot holds u rather than v.
[[nodiscard]] constexpr bool carries_proper_velocity(Kinematics kinematics) {
    return kinematics != Kinematics::Newtonian;
}

struct IntegratorConfig {
    // Error per step is measured against  atol + rtol*|y|  componentwise, with
    // separate absolute floors for position and velocity because they have
    // different units and wildly different magnitudes.
    double relative_tolerance{1.0e-10};
    double absolute_tolerance_position{1.0e-3};   // [m]
    double absolute_tolerance_velocity{1.0e-6};   // [m/s]
    // Proper time is not generally an exact linear state: in both relativistic
    // modes dτ/dt varies with velocity, and in the metric mode with position.
    // It therefore needs its own error floor instead of being omitted from the
    // embedded estimate.
    double absolute_tolerance_proper_time{1.0e-9}; // [s]
    // Mass is error-controlled too: its error feeds straight back into the
    // acceleration through a = F/m while an engine is burning.
    double absolute_tolerance_mass{1.0e-6};       // [kg]
    // Attitude tolerances. The quaternion is dimensionless and of order 1; the
    // angular velocity is in rad/s and orbital rates are ~1e-3, so the floors
    // differ by orders of magnitude and cannot share one number.
    double absolute_tolerance_orientation{1.0e-10};
    double absolute_tolerance_angular_velocity{1.0e-10};   // [rad/s]

    // Runtime validity envelope. `minimum_mass` is normally the dry mass when
    // the caller knows the vehicle; zero still guarantees positive total mass.
    double minimum_mass{0.0};                 // [kg]
    double quaternion_norm_tolerance{1.0e-6};

    time::Duration min_step{time::Duration::seconds(1.0e-6)};
    time::Duration max_step{time::Duration::days(1.0)};
    time::Duration initial_step{time::Duration::seconds(60.0)};

    std::size_t max_steps{10'000'000};

    double safety_factor{0.9};
    double min_shrink_factor{0.2};
    double max_growth_factor{5.0};

    Kinematics kinematics{Kinematics::Newtonian};

    // SpecialRelativistic is flat-spacetime physics. Adding a Newtonian
    // gravitational acceleration to it mixes a valid approximation with an
    // invalid one, and the error is silent. The propagator refuses unless this
    // is set deliberately -- at which point the caller owns the claim that the
    // field is weak and the speeds moderate.
    //
    // WeakFieldStaticMetric refuses too, and there the refusal is not negotiable in
    // the same way: gravity is already in the metric, so a gravitational
    // ForceModel would count it twice.
    bool allow_gravity_with_relativistic_kinematics{false};

    // PI step controller (Gustafsson).  beta = 0 reduces it to the classical
    // "elementary" controller; 0.04 is the standard value for DOPRI5 and damps
    // the step size oscillation seen on eccentric orbits.
    double pi_beta{0.04};

    // Stop the propagation when the trajectory enters a body's radius.  Right for
    // flying a mission; wrong for TARGETING one, where the corrector needs a
    // smooth map from departure velocity to arrival position and a trajectory
    // that clips the target is a perfectly good intermediate iterate.  The point
    // mass model stays valid inside the radius; it just stops being physical.
    bool stop_inside_body{true};
};

struct IntegratorStats {
    std::size_t accepted_steps{0};
    std::size_t rejected_steps{0};
    std::size_t force_evaluations{0};
    double min_step_seconds{0.0};
    double max_step_seconds{0.0};
    double mean_step_seconds{0.0};
    double max_error_estimate{0.0};  // scaled, 1.0 == exactly at tolerance
    double wall_time_seconds{0.0};

    // Largest |‖q‖ - 1| seen just before renormalising. Measured rather than
    // assumed: if it grows, the step is too long, and that is information a
    // silent projection would destroy (docs/physics/attitude.md section 5).
    double max_quaternion_drift{0.0};

    [[nodiscard]] std::string to_string() const;
};

enum class PropagationStatus {
    Success,
    OutOfPropellant,      // the tank ran dry mid-burn (reported, not an error)
    MinimumStepReached,   // error control demanded a step below min_step
    MaxStepsExceeded,
    NonFiniteState,       // NaN/Inf appeared: reported, never swept under a clamp
    InvariantViolation,   // finite, but outside a physical/state invariant
    InsideBody,           // trajectory entered a body's radius
    UnsupportedRegime     // relativistic kinematics asked to carry a gravity field
};

std::string to_string(PropagationStatus status);

struct PropagationResult {
    PropagationState state{};
    time::CoordinateTime time{};
    PropagationStatus status{PropagationStatus::Success};
    IntegratorStats stats{};
    std::string message;

    [[nodiscard]] bool ok() const { return status == PropagationStatus::Success; }
};

// Reported after every accepted step (and, if requested, rejected ones), for
// logging, trajectory sampling and diagnostics.
struct StepInfo {
    time::CoordinateTime time{};
    PropagationState state{};
    double step_seconds{0.0};
    double error_estimate{0.0};
    bool accepted{true};
};

using StepObserver = std::function<void(const StepInfo&)>;

class SpacecraftPropagator {
public:
    SpacecraftPropagator() = default;
    virtual ~SpacecraftPropagator() = default;
    SpacecraftPropagator(const SpacecraftPropagator&) = delete;
    SpacecraftPropagator& operator=(const SpacecraftPropagator&) = delete;

    // Integrates from `from` to `to`.  Backwards propagation (to < from) is
    // supported and is how "where did it come from?" questions get answered.
    virtual PropagationResult propagate(const PropagationState& initial,
                                        time::CoordinateTime from,
                                        time::CoordinateTime to) = 0;
};

}  // namespace sf::propagation
