// The engine relations, checked against the derivation in
// docs/physics/propulsion-model.md rather than against themselves.

#include "core/propulsion/engine.hpp"
#include "core/spacecraft/spacecraft.hpp"
#include "core/units/constants.hpp"
#include "tests/support/test_harness.hpp"

#include <cmath>
#include <sstream>
#include <stdexcept>

using sf::propulsion::EngineSpec;
using sf::spacecraft::Spacecraft;

namespace {
// A deliberately modest engine: w = 1e-4 c is about 30 km/s, in the range of a
// serious electric thruster, so the Newtonian limit applies cleanly.
EngineSpec test_engine(double efficiency = 0.5) {
    return EngineSpec{"test", 1.0, 1.0e-4, efficiency};
}
}  // namespace

TEST(the_engine_refuses_a_model_that_is_not_physical) {
    // Faster-than-light exhaust is not a better engine; it is an invalid model.
    CHECK_THROWS_AS(EngineSpec("bad", 1.0, 1.5, 0.5), std::invalid_argument);
    CHECK_THROWS_AS(EngineSpec("bad", 1.0, 0.0, 0.5), std::invalid_argument);
    CHECK_THROWS_AS(EngineSpec("bad", 1.0, -0.1, 0.5), std::invalid_argument);

    CHECK_THROWS_AS(EngineSpec("bad", 1.0, 0.1, 1.5), std::invalid_argument);
    CHECK_THROWS_AS(EngineSpec("bad", 1.0, 0.1, 0.0), std::invalid_argument);

    CHECK_THROWS_AS(EngineSpec("bad", 0.0, 0.1, 0.5), std::invalid_argument);
    CHECK_THROWS_AS(EngineSpec("bad", -1.0, 0.1, 0.5), std::invalid_argument);

    // w = c exactly is legal: that is the photon rocket limit.
    CHECK_THROWS_AS(EngineSpec("bad", 1.0, 1.0000001, 1.0), std::invalid_argument);
    const EngineSpec photon{"photon", 1.0, 1.0, 1.0};
    CHECK_EQ(photon.exhaust_velocity(), sf::units::c);
}

TEST(thrust_is_eta_q_w_and_nothing_else) {
    const auto engine = test_engine(0.5);

    const double expected_full = 0.5 * 1.0 * 1.0e-4 * sf::units::c;
    CHECK_NEAR_REL(engine.max_thrust(), expected_full, 1.0e-15,
                   "F = eta*q*w recomputed from the same three numbers; a couple of ulp");

    // Linear in throttle BECAUSE the consumption is: this is the property that
    // `fuel -= arbitraryNumber` destroys (rule section 17).
    for (const double throttle : {0.0, 0.25, 0.5, 0.75, 1.0}) {
        CHECK_NEAR_REL(engine.thrust_at(throttle), throttle * expected_full,
                       throttle > 0.0 ? 1.0e-15 : 0.0,
                       "thrust must be exactly proportional to throttle, because mass flow is");
        CHECK_NEAR_REL(engine.mass_flow_at(throttle), throttle * 1.0,
                       throttle > 0.0 ? 1.0e-15 : 0.0, "q = throttle * q_max by definition");
    }

    // Out-of-range throttle is clamped, not extrapolated: 200% throttle is not a
    // physical state, and refusing here would put an exception in the inner loop.
    CHECK_EQ(engine.thrust_at(2.0), engine.max_thrust());
    CHECK_EQ(engine.thrust_at(-1.0), 0.0);
}

TEST(the_photon_rocket_limit_comes_out_of_the_model_unprompted) {
    // At w = c and eta = 1 the model must give F = P/c, with P the power actually
    // converted.  Nothing in the code was written to make this happen; it is the
    // strongest sanity check we have on the derivation.
    const EngineSpec photon{"photon", 1.0e-6, 1.0, 1.0};

    const double thrust = photon.thrust_at(1.0);
    const double converted = photon.converted_power_at(1.0);

    std::ostringstream os;
    os << "photon rocket: q = 1e-6 kg/s gives " << thrust << " N from " << converted
       << " W, jet rest mass flow " << photon.jet_mass_flow_at(1.0) << " kg/s";
    INFO(os.str());

    CHECK_NEAR_ABS(photon.inverse_jet_gamma(), 0.0, 0.0,
                   "1/gamma_w = sqrt(1 - (w/c)^2) is exactly zero at w = c; storing the inverse is "
                   "what keeps the photon limit finite instead of an infinity");
    CHECK_NEAR_ABS(photon.jet_mass_flow_at(1.0), 0.0, 0.0,
                   "a photon rocket ejects no rest mass at all: mu = eta*q/gamma_w -> 0");
    CHECK_NEAR_REL(converted, photon.rest_energy_flux_at(1.0), 1.0e-15,
                   "with no rest mass leaving, all of it is converted");
    CHECK_NEAR_REL(thrust, converted / sf::units::c, 1.0e-15,
                   "F = P/c is the textbook photon rocket; here it falls out of F = eta*q*w and "
                   "the energy balance with w = c, eta = 1");

    // And the general thrust-to-gross-energy relation, away from the limit.
    const auto engine = test_engine(0.5);
    CHECK_NEAR_REL(engine.max_thrust() / engine.rest_energy_flux_at(1.0),
                   engine.efficiency() * engine.exhaust_velocity() / sf::units::c_squared, 1.0e-15,
                   "F/(q c^2) = eta*w/c^2 for any admissible engine, by construction");
}

TEST(the_energy_books_balance_and_expose_what_eta_really_costs) {
    // converted = jet kinetic + waste, identically, for every engine.
    for (const double fraction : {1.0e-5, 1.0e-2, 0.1, 0.5, 0.9, 1.0}) {
        for (const double eta : {0.1, 0.5, 0.9, 1.0}) {
            const EngineSpec e{"e", 1.0, fraction, eta};
            CHECK_NEAR_REL(e.converted_power_at(1.0),
                           e.jet_kinetic_power_at(1.0) + e.waste_power_at(1.0), 1.0e-12,
                           "(q - mu)c^2 = (gamma_w - 1)mu c^2 + (1 - eta)q c^2 is an identity of "
                           "the model, not an approximation; the bound is the rounding of the "
                           "cancellation for small w/c");
        }
    }

    // A chemical-class engine: w tiny, eta = 1.  The mass converted is the mass
    // DEFECT of the reaction, which is what physically happens, and it is tiny.
    const EngineSpec chemical{"chemical-class", 15.0, 3.0e-5, 1.0};
    const double converted_fraction =
        chemical.converted_power_at(1.0) / chemical.rest_energy_flux_at(1.0);

    std::ostringstream os;
    os << "chemical-class (w = 9000 m/s, eta = 1): v_eff = "
       << chemical.effective_exhaust_velocity() << " m/s, Isp = " << chemical.specific_impulse()
       << " s, converted fraction of rest mass " << converted_fraction << ", jet power "
       << chemical.jet_kinetic_power_at(1.0) << " W";
    INFO(os.str());

    CHECK_NEAR_REL(converted_fraction, 0.5 * std::pow(3.0e-5, 2.0), 1.0e-6,
                   "for w << c the converted fraction is 1 - 1/gamma_w = (w/c)^2/2 = 4.5e-10: the "
                   "mass defect of the reaction. That the model reproduces this without being "
                   "told is the reason to trust it in the other limit");
    CHECK_NEAR_REL(chemical.jet_kinetic_power_at(1.0),
                   0.5 * chemical.mass_flow_at(1.0) * std::pow(chemical.exhaust_velocity(), 2.0),
                   1.0e-6,
                   "and the jet power reduces to the Newtonian (1/2) q w^2");

    // The same eta on a relativistic engine is a different proposition entirely.
    const EngineSpec torch{"torch", 0.01, 0.1, 0.5};
    std::ostringstream torch_os;
    torch_os << "torch (w = 0.1c, eta = 0.5): converted " << torch.converted_power_at(1.0)
             << " W, of which jet " << torch.jet_kinetic_power_at(1.0) << " W and waste "
             << torch.waste_power_at(1.0) << " W";
    INFO(torch_os.str());

    CHECK(torch.waste_power_at(1.0) > 100.0 * torch.jet_kinetic_power_at(1.0));
    INFO("eta = 0.5 at w = 0.1c means annihilating half the propellant for nothing: the waste "
         "heat is ~200x the energy that reaches the jet. The model says so out loud");
}

TEST(the_rocket_equation_round_trips) {
    const auto engine = test_engine(0.5);
    const double v_eff = engine.effective_exhaust_velocity();

    CHECK_NEAR_REL(v_eff, 0.5 * 1.0e-4 * sf::units::c, 1.0e-15, "v_eff = eta*w by definition");
    CHECK_NEAR_REL(engine.specific_impulse(), v_eff / 9.80665, 1.0e-15,
                   "Isp = v_eff/g0 with g0 = 9.80665 m/s^2 exact by definition");

    const double m0 = 2000.0;
    const double delta_v = 1500.0;

    const double propellant = engine.propellant_for_delta_v(m0, delta_v);
    const double recovered = engine.delta_v_for_mass_ratio(m0, m0 - propellant);

    CHECK_NEAR_REL(recovered, delta_v, 1.0e-12,
                   "the two directions of Tsiolkovsky are inverse functions; the residual is the "
                   "rounding of an exp followed by a log");

    const double duration = engine.burn_duration_for_delta_v(m0, delta_v, 0.5);
    CHECK_NEAR_REL(duration, propellant / engine.mass_flow_at(0.5), 1.0e-15,
                   "burn time is propellant divided by flow; half throttle doubles it");

    CHECK_THROWS_AS(engine.delta_v_for_mass_ratio(1000.0, 2000.0), std::invalid_argument);
    CHECK_THROWS_AS(engine.propellant_for_delta_v(1000.0, -5.0), std::invalid_argument);
}

TEST(a_spacecraft_reports_its_budget_from_the_mass_it_carries) {
    const Spacecraft craft{"probe", 1000.0, 1000.0, test_engine(0.5)};

    CHECK_EQ(craft.initial_mass(), 2000.0);
    CHECK_EQ(craft.propellant_at(2000.0), 1000.0);
    CHECK_EQ(craft.propellant_at(1500.0), 500.0);
    CHECK(craft.has_propellant(1000.0001));

    // Below dry mass the reading is clamped at zero rather than negative: the
    // mission runner cuts the burn at exhaustion, so this should never be seen,
    // and if it is, a negative propellant mass would be the less useful answer.
    CHECK_EQ(craft.propellant_at(900.0), 0.0);
    CHECK(!craft.has_propellant(1000.0));

    const double budget = craft.delta_v_budget(2000.0);
    CHECK_NEAR_REL(budget, craft.engine().effective_exhaust_velocity() * std::log(2.0), 1.0e-14,
                   "burning all propellant halves the mass, so the budget is v_eff*ln(2)");
    CHECK_EQ(craft.delta_v_budget(1000.0), 0.0);

    CHECK_THROWS_AS(Spacecraft("bad", 0.0, 100.0, test_engine()), std::invalid_argument);
    CHECK_THROWS_AS(Spacecraft("bad", 100.0, -1.0, test_engine()), std::invalid_argument);
}

TEST(engine_and_spacecraft_load_from_configuration) {
    const auto root = sf::config::json::parse(R"({
        "spacecraft": {
            "name": "Tug",             // comments are allowed (ADR-0007)
            "dry_mass_kg": 1200.0,
            "propellant_mass_kg": 800.0,
            "engine": {
                "name": "Ion Mk II",
                "max_mass_flow_kg_s": 0.002,
                "exhaust_velocity_fraction_c": 0.0001,
                "efficiency": 0.6
            }
        }
    })");

    const auto craft = Spacecraft::from_json(root.require("spacecraft", "test"), "spacecraft");
    CHECK_EQ(craft.name(), std::string{"Tug"});
    CHECK_EQ(craft.initial_mass(), 2000.0);
    CHECK_EQ(craft.engine().name(), std::string{"Ion Mk II"});
    CHECK_NEAR_REL(craft.engine().efficiency(), 0.6, 0.0, "read verbatim from the file");

    // A missing required field must name itself.
    const auto incomplete = sf::config::json::parse(R"({"dry_mass_kg": 100.0})");
    CHECK_THROWS_AS(Spacecraft::from_json(incomplete, "spacecraft"), sf::config::json::ParseError);
}


TEST(engine_modes_are_operating_points_of_one_power_plant) {
    using sf::propulsion::MultiModeEngine;

    // The two modes of the Mk III: 16.7x the exhaust velocity, 298x less mass
    // flow, the SAME converted power. That is not a coincidence -- it is the
    // constraint that makes them modes rather than two engines.
    const EngineSpec impulse{"IMPULSE", 0.0222376, 0.03, 1.0};
    const EngineSpec cruise{"CRUISE", 7.470950e-05, 0.5, 1.0};

    std::ostringstream os;
    os << "IMPULSE: " << impulse.max_thrust() << " N, Isp " << impulse.specific_impulse()
       << " s, " << impulse.converted_power_at(1.0) << " W\n          CRUISE:  "
       << cruise.max_thrust() << " N, Isp " << cruise.specific_impulse() << " s, "
       << cruise.converted_power_at(1.0) << " W";
    INFO(os.str());

    CHECK_NEAR_REL(cruise.converted_power_at(1.0), impulse.converted_power_at(1.0), 1.0e-5,
                   "P = q c^2 (1 - eta/gamma_w) is what the two modes share. The tolerance is the "
                   "rounding of the tabulated mass flows, which are quoted to seven figures");

    MultiModeEngine engine{{{"IMPULSE", impulse}, {"CRUISE", cruise}}};
    CHECK_EQ(engine.size(), std::size_t{2});
    CHECK_EQ(engine.current_mode(), std::string{"IMPULSE"});
    CHECK_NEAR_REL(engine.current().max_thrust(), 2.0e5, 1.0e-4, "200 kN in impulse mode");

    CHECK(engine.select("CRUISE"));
    CHECK_EQ(engine.current_mode(), std::string{"CRUISE"});
    CHECK_NEAR_REL(engine.current().max_thrust(), 11198.7, 1.0e-4,
                   "and 18x less thrust in cruise, which is the price of 16.7x the exhaust "
                   "velocity at fixed power");
    CHECK(!engine.select("WARP"));

    engine.cycle();
    CHECK_EQ(engine.current_mode(), std::string{"IMPULSE"});
    INFO(engine.describe());
}

TEST(modes_that_do_not_share_a_power_plant_are_refused) {
    using sf::propulsion::MultiModeEngine;

    // Same exhaust velocity, ten times the flow: ten times the power. That is a
    // bigger engine, not another setting, and pretending otherwise is how a
    // "mode" quietly becomes free energy.
    const EngineSpec honest{"A", 0.01, 0.1, 1.0};
    const EngineSpec cheating{"B", 0.1, 0.1, 1.0};

    CHECK_THROWS_AS(MultiModeEngine({{"A", honest}, {"B", cheating}}), std::invalid_argument);
    CHECK_THROWS_AS(MultiModeEngine({{"A", honest}, {"A", honest}}), std::invalid_argument);
    CHECK_THROWS_AS(MultiModeEngine(std::vector<MultiModeEngine::Mode>{}), std::invalid_argument);

    // A single-mode engine is the degenerate case and stays legal.
    const MultiModeEngine one{honest};
    CHECK_EQ(one.size(), std::size_t{1});
    CHECK_EQ(one.current_mode(), std::string{"A"});
}

TEST(a_two_mode_ship_reports_a_different_budget_in_each_mode) {
    using sf::propulsion::MultiModeEngine;

    const MultiModeEngine engine{{{"IMPULSE", EngineSpec{"IMPULSE", 0.0222376, 0.03, 1.0}},
                                  {"CRUISE", EngineSpec{"CRUISE", 7.470950e-05, 0.5, 1.0}}}};
    Spacecraft ship{"torch", 1000.0, 19000.0, engine};

    const double ratio = std::log(20.0);
    const double impulse_budget = ship.delta_v_budget(ship.initial_mass());
    ship.select_mode("CRUISE");
    const double cruise_budget = ship.delta_v_budget(ship.initial_mass());

    std::ostringstream os;
    os << "20:1 mass ratio: IMPULSE gives " << impulse_budget / sf::units::c << " c of budget, "
       << "CRUISE gives " << cruise_budget / sf::units::c << " c";
    INFO(os.str());

    CHECK_NEAR_REL(impulse_budget, 0.03 * sf::units::c * ratio, 1.0e-12,
                   "v_eff ln(m0/m1) with v_eff = 0.03c. Note this is the NEWTONIAN budget: at "
                   "0.09c it is still a fair statement of what the propellant buys");
    CHECK_NEAR_REL(cruise_budget, 0.5 * sf::units::c * ratio, 1.0e-12, "and the same in cruise");

    // Read as rapidity, the cruise budget is what actually matters: beta = tanh.
    CHECK_NEAR_REL(std::tanh(0.5 * ratio), 0.904762, 1.0e-5,
                   "0.5 * ln(20) = 1.4979 of rapidity is beta = 0.9048. The Newtonian budget of "
                   "1.5c is meaningless as a speed and perfectly meaningful as rapidity -- which "
                   "is why the relativistic rocket equation is written in it");
}
