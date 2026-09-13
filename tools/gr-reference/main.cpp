// Emits a trajectory for cross-validation against an independent code (§29).
//
// A single point mass fixed at the origin and a test particle: the simplest
// problem on which two relativistic integrators can be asked the same question.
// Writes CSV to stdout so the comparison is done by whatever tool has the other
// code, without this binary having to know anything about it.
//
// See tools/validation/README.md.

#include "core/celestial/body_catalog.hpp"
#include "core/gravity/force_model.hpp"
#include "core/gravity/point_mass_gravity.hpp"
#include "core/gravity/weak_field_metric.hpp"
#include "core/propagation/dormand_prince_54.hpp"
#include "core/units/constants.hpp"
#include "tests/support/analytic_ephemeris.hpp"

#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

using namespace sf;
using math::Vec3;
using propagation::Kinematics;

namespace {

// Nothing acts on the particle: under GeneralRelativistic gravity lives in the
// metric, and under Newtonian it comes from PointMassGravity.
class NoForce final : public gravity::ForceModel {
public:
    [[nodiscard]] gravity::ForceResult evaluate(const propagation::PropagationState&,
                                                time::CoordinateTime) const override {
        return {};
    }
    [[nodiscard]] std::string_view name() const override { return "NoForce"; }
};

[[noreturn]] void usage(int code) {
    std::fputs(
        "usage: gr-reference --gm GM --x X --vy VY --duration SECONDS --samples N\n"
        "                   [--mode geodesic|newtonian] [--rtol TOL]\n"
        "\n"
        "  Propagates a test particle around a point mass fixed at the origin and\n"
        "  writes 't,x,y,z,vx,vy,vz' to stdout, one row per sample, SI units, in an\n"
        "  inertial frame centred on the mass.\n"
        "\n"
        "  --mode geodesic   the weak-field metric (docs/physics/relativistic-gravity.md)\n"
        "  --mode newtonian  the same integrator with the Newtonian force, for a baseline\n",
        code == 0 ? stdout : stderr);
    std::exit(code);
}

double number(const char* text, const char* flag) {
    char* end = nullptr;
    const double value = std::strtod(text, &end);
    if (end == text || *end != '\0') {
        std::fprintf(stderr, "gr-reference: %s expects a number, got '%s'\n", flag, text);
        std::exit(2);
    }
    return value;
}

}  // namespace

int main(int argc, char** argv) {
    double gm = 0.0;
    double x0 = 0.0;
    double vy0 = 0.0;
    double duration = 0.0;
    long samples = 0;
    double rtol = 1.0e-13;
    bool geodesic = true;

    for (int i = 1; i < argc; ++i) {
        const std::string_view flag{argv[i]};
        const auto value = [&]() -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "gr-reference: %s needs a value\n", argv[i]);
                std::exit(2);
            }
            return argv[++i];
        };
        if (flag == "--gm") {
            gm = number(value(), "--gm");
        } else if (flag == "--x") {
            x0 = number(value(), "--x");
        } else if (flag == "--vy") {
            vy0 = number(value(), "--vy");
        } else if (flag == "--duration") {
            duration = number(value(), "--duration");
        } else if (flag == "--samples") {
            samples = static_cast<long>(number(value(), "--samples"));
        } else if (flag == "--rtol") {
            rtol = number(value(), "--rtol");
        } else if (flag == "--mode") {
            const std::string_view mode{value()};
            if (mode == "geodesic") {
                geodesic = true;
            } else if (mode == "newtonian") {
                geodesic = false;
            } else {
                std::fprintf(stderr, "gr-reference: unknown mode '%s'\n", mode.data());
                return 2;
            }
        } else if (flag == "--help" || flag == "-h") {
            usage(0);
        } else {
            std::fprintf(stderr, "gr-reference: unknown flag '%s'\n", argv[i]);
            usage(2);
        }
    }

    if (!(gm > 0.0) || !(x0 > 0.0) || !(duration > 0.0) || samples < 1) {
        std::fputs("gr-reference: --gm, --x, --duration and --samples are required\n", stderr);
        usage(2);
    }

    const auto body = celestial::bodies::sun;
    const sft::FixedPointMassProvider provider{body, gm};
    const std::vector<celestial::BodyId> ids{body};
    const auto catalog = celestial::BodyCatalog::resolve(provider, ids);
    const auto frame = coordinates::ReferenceFrame::ssb_j2000();

    const gravity::WeakFieldMetric metric{provider, catalog, frame};
    const gravity::PointMassGravity newtonian{provider, catalog, frame};
    const NoForce nothing;

    propagation::IntegratorConfig config{};
    config.kinematics = geodesic ? Kinematics::GeneralRelativistic : Kinematics::Newtonian;
    config.relative_tolerance = rtol;
    config.absolute_tolerance_position = 1.0e-4;
    config.absolute_tolerance_velocity = 1.0e-8;
    config.initial_step = time::Duration::seconds(10.0);
    config.max_step = time::Duration::seconds(duration / static_cast<double>(samples));

    const gravity::ForceModel& force =
        geodesic ? static_cast<const gravity::ForceModel&>(nothing)
                 : static_cast<const gravity::ForceModel&>(newtonian);
    propagation::DormandPrince54Propagator propagator{force, config};
    if (geodesic) {
        propagator.set_metric(&metric);
    }

    propagation::PropagationState state{};
    state.mass = 1.0;
    state.state.position = Vec3{x0, 0.0, 0.0};
    state.state.velocity = Vec3{0.0, vy0, 0.0};

    const auto t0 = time::CoordinateTime::j2000();
    auto t = t0;

    std::printf("t,x,y,z,vx,vy,vz\n");
    const auto emit = [&](double elapsed, const propagation::PropagationState& s) {
        std::printf("%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g\n", elapsed, s.state.position.x,
                    s.state.position.y, s.state.position.z, s.state.velocity.x,
                    s.state.velocity.y, s.state.velocity.z);
    };
    emit(0.0, state);

    const double step = duration / static_cast<double>(samples);
    for (long i = 1; i <= samples; ++i) {
        const auto target = t0 + time::Duration::seconds(step * static_cast<double>(i));
        const auto result = propagator.propagate(state, t, target);
        if (!result.ok()) {
            std::fprintf(stderr, "gr-reference: propagation failed at sample %ld: %s\n", i,
                         result.message.c_str());
            return 1;
        }
        state = result.state;
        t = result.time;
        emit(step * static_cast<double>(i), state);
    }
    return 0;
}
