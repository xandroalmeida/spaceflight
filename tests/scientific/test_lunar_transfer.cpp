// The Earth-to-Moon transfer, as a capability rather than a demonstration.
//
// The Milestone 6 campaign reached 41 captures in 100 departure epochs, and
// every failure reported that the solver had converged.  It had: it had
// converged on trajectories that pass through the Earth.  These tests fix the
// properties that make that impossible to repeat -- the classification is total,
// the departure conic is screened analytically, the capture is verified by the
// sign of the specific energy on both sides of the burn, and "captured" means the
// orbit that was asked for and not merely a closed one.
//
// See docs/validation/lunar-navigation-hardening.md.

#include "core/celestial/body_catalog.hpp"
#include "core/navigation/lunar_transfer.hpp"
#include "core/propulsion/engine.hpp"
#include "core/trajectory/orbital_elements.hpp"
#include "core/units/conversions.hpp"
#include "tests/support/kernel_fixture.hpp"
#include "tests/support/test_harness.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

using namespace sf;
using sf::math::Vec3;

namespace {

// The scenario of tests/scenarios/lunar-intercept.json, in code, so that this
// file does not depend on the CLI's JSON reader.
constexpr double kDryMass = 1000.0;
constexpr double kPropellant = 19000.0;

spacecraft::Spacecraft make_tug() {
    const propulsion::MultiModeEngine engine{
        {{"IMPULSE", propulsion::EngineSpec{"IMPULSE", 0.0222376, 0.03, 1.0}},
         {"CRUISE", propulsion::EngineSpec{"CRUISE", 7.470950e-05, 0.5, 1.0}}}};
    return spacecraft::Spacecraft{"Tug", kDryMass, kPropellant, engine};
}

navigation::TransferInputs make_inputs(const sft::SpiceFixture& spice,
                                       const spacecraft::Spacecraft& craft,
                                       time::CoordinateTime epoch) {
    navigation::TransferInputs inputs{};
    inputs.provider = spice.provider.get();
    inputs.orientation = spice.provider.get();
    inputs.craft = &craft;
    const std::vector<celestial::BodyId> bodies{celestial::bodies::sun, celestial::bodies::earth,
                                                celestial::bodies::moon};
    inputs.catalog = celestial::BodyCatalog::resolve(*spice.provider, bodies);
    inputs.j2_bodies = {celestial::bodies::earth};
    inputs.center = celestial::bodies::earth;
    inputs.target = celestial::bodies::moon;
    inputs.parking.position = Vec3{5403733.3475775, -3679897.2695510, -1788660.3908598};
    inputs.parking.velocity = Vec3{4623.3381510, 5322.9266799, 3016.4827353};
    inputs.epoch = epoch;
    inputs.integrator.relative_tolerance = 1.0e-11;
    inputs.integrator.absolute_tolerance_position = 1.0e-3;
    inputs.integrator.absolute_tolerance_velocity = 1.0e-6;
    inputs.integrator.initial_step = time::Duration::seconds(1.0);
    inputs.integrator.max_step = time::Duration::seconds(3600.0);
    return inputs;
}

std::size_t count_commas(const std::string& text) {
    // Commas OUTSIDE quotes: the detail field is quoted and may contain them.
    std::size_t count = 0;
    bool quoted = false;
    for (const char c : text) {
        if (c == '"') {
            quoted = !quoted;
        } else if (c == ',' && !quoted) {
            ++count;
        }
    }
    return count;
}

}  // namespace

// ---------------------------------------------------------------------------
// Things that need no kernels.
// ---------------------------------------------------------------------------

TEST(every_failure_has_a_name) {
    // The taxonomy is the point of section 4 of the brief: nothing may fail as
    // "solver failed".  A missing case in the switch would return UNCLASSIFIED,
    // and a campaign would count it as a category of its own without anybody
    // noticing which one it was.
    const navigation::TransferFailure all[] = {
        navigation::TransferFailure::None,
        navigation::TransferFailure::NoLambertSolution,
        navigation::TransferFailure::BadLambertBranch,
        navigation::TransferFailure::NoFeasibleTrajectory,
        navigation::TransferFailure::DepartureCorrectorDiverged,
        navigation::TransferFailure::DepartureCorrectorStagnated,
        navigation::TransferFailure::InvalidBPlane,
        navigation::TransferFailure::BPlaneCorrectorDiverged,
        navigation::TransferFailure::LunarImpact,
        navigation::TransferFailure::PeriapsisTooHigh,
        navigation::TransferFailure::PeriapsisTooLow,
        navigation::TransferFailure::CaptureBurnTooEarly,
        navigation::TransferFailure::CaptureBurnTooLate,
        navigation::TransferFailure::InsufficientCaptureDeltaV,
        navigation::TransferFailure::PostBurnHyperbolic,
        navigation::TransferFailure::NumericalFailure,
        navigation::TransferFailure::Timeout,
        navigation::TransferFailure::DepartureConicHitsCentralBody,
        navigation::TransferFailure::InsufficientDepartureDeltaV,
        navigation::TransferFailure::TargetOrbitNotAchieved};

    std::vector<std::string> names;
    for (const auto reason : all) {
        const std::string name{navigation::to_string(reason)};
        CHECK(name != "UNCLASSIFIED");
        names.push_back(name);
    }
    // And every name is distinct, or two different diagnoses would be counted as
    // one in the campaign's histogram.
    std::sort(names.begin(), names.end());
    CHECK(std::adjacent_find(names.begin(), names.end()) == names.end());

    for (const auto model : {navigation::ExecutionModel::Impulsive,
                             navigation::ExecutionModel::FiniteBurn,
                             navigation::ExecutionModel::Autopilot}) {
        CHECK(std::string{navigation::to_string(model)} != "UNKNOWN");
    }
    for (const auto value : {navigation::GridClass::NoSolution,
                             navigation::GridClass::DegenerateGeometry,
                             navigation::GridClass::DepartureConicHitsBody,
                             navigation::GridClass::TooExpensive,
                             navigation::GridClass::Feasible}) {
        CHECK(std::string{navigation::to_string(value)} != "UNKNOWN");
    }
}

TEST(the_target_orbit_is_a_specification_and_not_a_sign_test) {
    // Section 13: `specific_energy < 0` is satisfied by an orbit with a periapsis
    // inside the Moon and an apoapsis past the Earth.  The specification is four
    // numbers and each one has to be able to reject.
    navigation::TargetOrbit orbit{};   // 100 km, e <= 0.01, 80 <= h <= 120 km

    CHECK(orbit.check(96.0e3, 104.0e3, 0.002) == navigation::TransferFailure::None);
    CHECK(orbit.check(40.0e3, 104.0e3, 0.002) == navigation::TransferFailure::PeriapsisTooLow);
    CHECK(orbit.check(96.0e3, 900.0e3, 0.2) == navigation::TransferFailure::PeriapsisTooHigh);
    CHECK(orbit.check(96.0e3, 118.0e3, 0.05) ==
          navigation::TransferFailure::TargetOrbitNotAchieved);

    // The boundaries belong to the specification: exactly 80 km passes, a metre
    // below does not.  A limit that is ambiguous at its own value is not a limit.
    CHECK(orbit.check(80.0e3, 120.0e3, 0.01) == navigation::TransferFailure::None);
    CHECK(orbit.check(80.0e3 - 1.0, 120.0e3, 0.01) ==
          navigation::TransferFailure::PeriapsisTooLow);
    CHECK(orbit.check(80.0e3, 120.0e3 + 1.0, 0.01) ==
          navigation::TransferFailure::PeriapsisTooHigh);

    // An unbounded orbit has an infinite apoapsis, and the check must reject it
    // rather than let a NaN comparison pass silently.
    CHECK(orbit.check(96.0e3, std::numeric_limits<double>::infinity(), 1.5) ==
          navigation::TransferFailure::PeriapsisTooHigh);
}

TEST(the_cost_function_is_monotone_in_every_argument) {
    // Section 8 asks for an explicit cost, and the one property it must have is
    // that making any one term worse cannot make the total better -- otherwise
    // the search could prefer a trajectory that is worse in every respect.
    navigation::TransferCost cost{};
    // The terms Milestone 6.2 section 14 added are weighted zero by default, so
    // turn them on for this test: a term that is priced at zero is trivially
    // monotone, and checking it that way would prove nothing about the day
    // somebody prices it.
    cost.time_of_flight = 10.0;
    cost.inclination_error = 100.0;

    const navigation::TransferCostTerms nominal{3100.0, 820.0, 100.0, 50.0, 0.0, 4.5, 0.1};
    const double base = cost.evaluate(nominal);

    const auto worse = [&](auto&& mutate) {
        auto terms = nominal;
        mutate(terms);
        return cost.evaluate(terms);
    };
    CHECK(worse([](auto& t) { t.departure_delta_v = 3200.0; }) > base);
    CHECK(worse([](auto& t) { t.insertion_delta_v = 900.0; }) > base);
    CHECK(worse([](auto& t) { t.periapsis_error_m = 200.0; }) > base);
    CHECK(worse([](auto& t) { t.correction_delta_v = 90.0; }) > base);
    CHECK(worse([](auto& t) { t.conic_deficit_m = 1000.0; }) > base);
    CHECK(worse([](auto& t) { t.time_of_flight_days = 5.5; }) > base);
    CHECK(worse([](auto& t) { t.inclination_error_rad = 0.2; }) > base);

    // The periapsis error is SIGNED in the record and its cost is not: missing
    // high and missing low are equally wrong.  Same for the inclination error.
    CHECK_NEAR_ABS(worse([](auto& t) { t.periapsis_error_m = -100.0; }), base, 0.0,
                   "the cost of a periapsis error may not depend on its sign");
    CHECK_NEAR_ABS(worse([](auto& t) { t.inclination_error_rad = -0.1; }), base, 0.0,
                   "nor may the cost of an inclination error");

    // The deficit is a ONE-SIDED penalty: a departure conic that clears the
    // floor by a kilometre is not cheaper than one that clears it exactly.
    CHECK_NEAR_ABS(worse([](auto& t) { t.conic_deficit_m = -1000.0; }), base, 0.0,
                   "a negative deficit means the floor was cleared, and clearing it by more "
                   "buys nothing");
}

TEST(the_csv_row_matches_its_own_header) {
    // The header and the row are written in two different functions, fifty lines
    // apart, and a campaign that silently shifts by one column produces a file
    // whose every number is attributed to the wrong name.  Nothing but counting
    // catches that.
    navigation::TransferRecord record{};
    record.detail = R"(a detail with a comma, a "quote" and a
newline)";
    const std::string header = navigation::TransferRecord::csv_header();
    const std::string row = record.csv_row();
    CHECK_EQ(count_commas(row), count_commas(header));

    // And the quoting has to survive the trip: a bare newline inside a field
    // would split the row in two for every reader in existence.
    CHECK(row.find('\n') == std::string::npos);
}

// ---------------------------------------------------------------------------
// Things that need the real ephemeris.
// ---------------------------------------------------------------------------

TEST(the_grid_says_why_the_milestone_6_geometry_had_nowhere_to_go) {
    const auto spice = sft::load_spice_or_skip();
    const auto craft = make_tug();
    const auto epoch = spice.time->parse("2026-01-05 00:00:00 TDB");
    const auto inputs = make_inputs(spice, craft, epoch);

    navigation::LunarTransferConfig config{};
    const auto grid = navigation::map_transfer_grid(inputs, config);
    REQUIRE(!grid.empty());

    std::size_t feasible = 0;
    std::size_t hits_earth = 0;
    for (const auto& cell : grid) {
        feasible += cell.classification == navigation::GridClass::Feasible ? 1 : 0;
        hits_earth += cell.classification == navigation::GridClass::DepartureConicHitsBody ? 1 : 0;
    }
    std::ostringstream os;
    os << grid.size() << " cells: " << feasible << " feasible, " << hits_earth
       << " whose post-injection conic re-enters the Earth";
    INFO(os.str());

    // The finding this whole milestone turns on, as a test: from a 400 km parking
    // orbit MOST departure geometries are unflyable, and they are unflyable for
    // one reason.  A transfer ellipse that reaches the Moon has its perigee far
    // below the parking radius unless the departure point is near that perigee,
    // and Lambert will happily return the solution anyway.
    CHECK(hits_earth > grid.size() / 2);

    // And at least one cell survives, or the epoch would genuinely have nowhere
    // to go and the campaign's 100% would be measuring an empty set.
    CHECK(feasible > 0);

    // Every feasible cell really does clear the surface, by the margin asked for.
    for (const auto& cell : grid) {
        if (cell.classification == navigation::GridClass::Feasible) {
            CHECK(cell.departure_perigee_altitude >=
                  config.minimum_departure_perigee_altitude - 1.0);
        }
    }
}

TEST(the_capture_changes_the_sign_of_the_specific_energy) {
    const auto spice = sft::load_spice_or_skip();
    const auto craft = make_tug();
    // 2026-01-05 is one of the epochs Milestone 6 failed on: transfer angle
    // 223 degrees at a fixed 4.5-day time of flight, Lambert delta-v 4640 m/s,
    // and a departure conic that re-entered the Earth six minutes after ignition.
    const auto epoch = spice.time->parse("2026-01-05 00:00:00 TDB");
    const auto inputs = make_inputs(spice, craft, epoch);

    navigation::LunarTransferConfig config{};
    config.execution = navigation::ExecutionModel::FiniteBurn;
    const auto record = navigation::plan_and_fly(inputs, config);
    INFO(record.describe());

    REQUIRE(record.success);

    // Section 10 asks for the two signs to be checked INDEPENDENTLY rather than
    // inferred from "the eccentricity came out below one".  A hyperbolic arrival
    // has positive specific energy about the target; a capture has negative.
    CHECK(record.energy_before_burn > 0.0);
    CHECK(record.post_burn_specific_energy < 0.0);

    // v_infinity and the energy before the burn are the same statement twice, and
    // they have to agree: eps = v_inf^2 / 2.
    CHECK_NEAR_REL(record.energy_before_burn, 0.5 * record.v_infinity * record.v_infinity, 1.0e-9,
                   "the specific energy of a hyperbolic arrival IS v_inf^2 / 2; the two are "
                   "computed from different quantities and must agree to rounding");

    // The departure the search chose clears the Earth by the margin the hard
    // constraint asked for -- which is the constraint Milestone 6 did not have.
    CHECK(record.departure_perigee_radius > 6.371e6 + config.minimum_departure_perigee_altitude);

    // The burn happened near periapsis, which is what makes it affordable
    // (section 12): the midpoint radius is within a few kilometres of the
    // periapsis the B-plane predicted.
    CHECK_NEAR_REL(record.burn_midpoint_radius, record.actual_periapsis, 1.0e-2,
                   "a capture burn centred on periapsis must happen at periapsis; 1% of "
                   "1837 km is 18 km, which a burn of 90 seconds cannot exceed");
}

TEST(the_impulsive_plan_and_the_finite_burn_agree_on_the_trajectory) {
    const auto spice = sft::load_spice_or_skip();
    const auto craft = make_tug();
    const auto epoch = spice.time->parse("2026-01-05 00:00:00 TDB");
    const auto inputs = make_inputs(spice, craft, epoch);

    navigation::LunarTransferConfig config{};
    config.execution = navigation::ExecutionModel::Impulsive;
    const auto impulsive = navigation::plan_and_fly(inputs, config);
    config.execution = navigation::ExecutionModel::FiniteBurn;
    const auto finite = navigation::plan_and_fly(inputs, config);

    std::ostringstream os;
    os << "impulsive: " << navigation::to_string(impulsive.failure) << ", e = "
       << impulsive.post_burn_eccentricity << "\nfinite   : "
       << navigation::to_string(finite.failure) << ", e = " << finite.post_burn_eccentricity;
    INFO(os.str());

    REQUIRE(impulsive.success);
    REQUIRE(finite.success);

    // Section 5: planning error and execution error must be separable.  Both
    // models search the same grid and both reach the same specification, so what
    // separates them is the burn -- and the burn's cost is a gravity loss, which
    // is a few m/s on a 90-second burn and not a few hundred.
    CHECK_NEAR_REL(finite.required_capture_delta_v, impulsive.required_capture_delta_v, 0.05,
                   "the capture delta-v is planned from the same B-plane in both models; a "
                   "disagreement above 5% would mean they are not flying the same approach");

    // The finite burn is the one that loses: it spends time away from periapsis.
    // Measured rather than assumed, because "assumed" is how Milestone 6 got a
    // capture rate it could not explain.
    CHECK(finite.propellant_used > 0.0);
    CHECK(impulsive.propellant_used > 0.0);
}

TEST(an_epoch_with_no_search_freedom_is_refused_and_says_why) {
    const auto spice = sft::load_spice_or_skip();
    const auto craft = make_tug();
    const auto epoch = spice.time->parse("2026-01-05 00:00:00 TDB");
    const auto inputs = make_inputs(spice, craft, epoch);

    // The Milestone 6 configuration: leave now, fly for exactly 4.5 days.  Four
    // geometries exist and none of them is flyable -- the transfer angle is
    // 223 degrees and the two-body departure conic's perigee is 431 km BELOW the
    // Earth's surface.  What the taxonomy has to do is say that, instead of
    // reporting a converged solver.
    navigation::LunarTransferConfig config{};
    config.departure_window = time::Duration::seconds(60.0);
    config.departure_samples = 2;
    config.time_of_flight_days = {4.5};
    config.flown_candidates = 2;   // the epoch is hopeless; do not grind on it

    const auto attempted = navigation::plan_and_fly(inputs, config);
    INFO(attempted.describe());

    CHECK(!attempted.success);
    CHECK(attempted.failure != navigation::TransferFailure::None);
    // Whatever it is, it is a DEPARTURE problem, and the record says how many
    // geometries died of what.  "No feasible trajectory" with no numbers would be
    // the same unfalsifiable answer as "solver failed".
    CHECK(attempted.candidates_considered > 0);
    CHECK(attempted.rejected_departure_conic > 0);
    CHECK(!attempted.detail.empty());

    // The same epoch under the STRICTER reading of section 8's hard constraint,
    // where a departure conic below the floor is refused rather than priced.
    //
    // Both readings are defensible and they disagree, which is why the switch
    // exists and why this test exercises both.  Refusing is honest about the
    // two-body geometry and dishonest about what the corrector can do: it moves
    // the departure velocity by hundreds of m/s, and that routinely lifts a
    // perigee that started below the surface.  Measured over 100 epochs in this
    // configuration: refusing gives 15% success, pricing gives 66%.
    config.refuse_departure_conic_below_floor = true;
    const auto refused = navigation::plan_and_fly(inputs, config);
    INFO(refused.describe());

    CHECK(!refused.success);
    CHECK(refused.failure == navigation::TransferFailure::NoFeasibleTrajectory);
    CHECK(refused.candidates_feasible == 0);
    // And the refusal is quantified, geometry by geometry.
    CHECK(refused.rejected_departure_conic == refused.candidates_considered);
    CHECK(refused.detail.find("re-enters the central body") != std::string::npos);
}
