// What colour is a black body, and how much of it can an eye see?
//
// The relativity is already tested (test_relativistic_optics.cpp).  What this
// checks is the other half of "a Doppler-shifted black body is still a black
// body": that the T -> colour map is the published one, and that the band
// efficiency eta(T) behaves the way section 10 says it does.
//
// The reference is the CIE Planckian locus as published (CIE 15 / Wyszecki &
// Stiles), which was NOT used to build anything here: the implementation goes
// Planck -> Wyman's analytic CMF fit -> XYZ, and the locus is an independent
// tabulation of the same physical curve.
// See docs/physics/relativistic-rendering.md sections 9 and 10.

#include "core/render/blackbody.hpp"
#include "core/render/tone_response.hpp"
#include "tests/support/test_harness.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <utility>

using namespace sf;
using sf::render::blackbody_sample;

TEST(the_planckian_locus_matches_the_published_one) {
    // CIE 15 / Wyszecki & Stiles, Table I(3.11): chromaticity of a Planckian
    // radiator.  Values typed from the published table.
    struct Point {
        double t;
        double x;
        double y;
    };
    const Point locus[] = {
        {1000.0, 0.6528, 0.3444}, {1500.0, 0.5857, 0.3931}, {2000.0, 0.5267, 0.4133},
        {2500.0, 0.4770, 0.4137}, {3000.0, 0.4369, 0.4041}, {3500.0, 0.4053, 0.3907},
        {4000.0, 0.3805, 0.3768}, {4500.0, 0.3608, 0.3636}, {5000.0, 0.3451, 0.3516},
        {5500.0, 0.3325, 0.3411}, {6000.0, 0.3221, 0.3318}, {6500.0, 0.3135, 0.3237},
        {7000.0, 0.3064, 0.3166}, {8000.0, 0.2952, 0.3048}, {10000.0, 0.2807, 0.2884},
        {15000.0, 0.2637, 0.2673}, {20000.0, 0.2565, 0.2577},
    };

    double worst = 0.0;
    for (const auto& p : locus) {
        const auto sample = blackbody_sample(p.t);
        worst = std::max({worst, std::abs(sample.chromaticity_x - p.x),
                          std::abs(sample.chromaticity_y - p.y)});

        // Above 3000 K the fit is far better than at the cold end, and saying so
        // separately is what makes the loose bound below honest rather than
        // convenient.
        const double bound = p.t >= 3000.0 ? 1.5e-3 : 1.2e-2;
        std::ostringstream os;
        os << p.t << " K: x " << sample.chromaticity_x << " vs " << p.x << ", y "
           << sample.chromaticity_y << " vs " << p.y;
        INFO(os.str());

        CHECK_NEAR_ABS(sample.chromaticity_x, p.x, bound,
                       "Wyman, Sloan & Shirley (2013) state ~1% of peak for the analytic CMF "
                       "fit; propagated through the chromaticity ratio that is ~1e-3 where "
                       "the lobes overlap the Planck peak (T >= 3000 K, measured worst "
                       "1.1e-3) and ~1e-2 where the GAUSSIAN TAILS carry the integral "
                       "(1000 K, measured 1.07e-2). Two bounds because they are two "
                       "different error mechanisms");
        CHECK_NEAR_ABS(sample.chromaticity_y, p.y, bound,
                       "same fit, same two mechanisms; see the x bound above");
    }

    std::ostringstream os;
    os << "worst chromaticity error over the whole locus: " << worst;
    INFO(os.str());
}

TEST(the_chromaticity_converges_in_the_rayleigh_jeans_limit) {
    // This is what makes the bijective table index of section 9.2 legitimate: if
    // the colour kept moving as T -> infinity, losing resolution there would lose
    // information, and the index would have to be a clamped range instead.
    const auto hot = blackbody_sample(1.0e5);
    const auto hotter = blackbody_sample(1.0e6);
    const auto hottest = blackbody_sample(1.0e8);

    std::ostringstream os;
    os << "x: 1e5 K " << hot.chromaticity_x << ", 1e6 K " << hotter.chromaticity_x
       << ", 1e8 K " << hottest.chromaticity_x;
    INFO(os.str());

    // B_lambda -> 2ckT/lambda^4 inside the band, so the chromaticity of the
    // lambda^-4 spectrum is the limit.  Computed here from the same CMFs, which
    // makes this a statement about convergence and not about colorimetry.
    CHECK_NEAR_ABS(hottest.chromaticity_x, 0.2401, 1.0e-3,
                   "the Rayleigh-Jeans limit of the SAME CMF approximation: a lambda^-4 "
                   "spectrum weighted by Wyman's x_bar gives x = 0.2401. The bound is the "
                   "residual hc/lambda kT at 1e8 K, which is 2.6e-4");
    CHECK_NEAR_ABS(hottest.chromaticity_y, 0.2340, 1.0e-3, "same limit, y channel");

    // Monotone approach, so that the table's coarse end is coarse in a quantity
    // that has stopped moving.
    CHECK(std::abs(hotter.chromaticity_x - hottest.chromaticity_x) <
          std::abs(hot.chromaticity_x - hottest.chromaticity_x));
}

TEST(the_cold_end_does_not_underflow) {
    // beta = 0.99 astern turns a 5800 K star into 411 K; push an order of
    // magnitude past that.  Without the log-space integral of section 9.3 the
    // Planck integrand is e^-2236 here and every one of these is 0/0.
    for (const double t : {1.0, 12.0, 45.0, 142.0, 411.0, 1297.0}) {
        const auto sample = blackbody_sample(t);
        std::ostringstream os;
        os << t << " K: ln eta " << sample.ln_band_efficiency << ", x "
           << sample.chromaticity_x;
        INFO(os.str());

        CHECK(std::isfinite(sample.ln_band_efficiency));
        CHECK(std::isfinite(sample.chromaticity_x) && std::isfinite(sample.chromaticity_y));
        CHECK(sample.ln_band_efficiency < 0.0);
    }

    // And the efficiency has to be monotone up to the peak: a colder body puts a
    // smaller FRACTION of its power in the visible band.
    double previous = blackbody_sample(1.0).ln_band_efficiency;
    for (double t = 10.0; t < 5000.0; t *= 1.3) {
        const double current = blackbody_sample(t).ln_band_efficiency;
        CHECK(current > previous);
        previous = current;
    }
}

TEST(the_band_efficiency_peaks_where_a_star_is_yellow) {
    // eta(T) is the fraction of a black body's power that lands in the photopic
    // band, so it must peak where the Planck peak sits in the middle of that
    // band.  Wien: lambda_max T = 2.898e-3 m K, and 555 nm (the photopic peak)
    // gives T = 5220 K.  The eta peak sits higher because eta divides by T^4.
    double best_t = 0.0;
    double best = -1.0e300;
    for (double t = 1000.0; t < 20000.0; t += 5.0) {
        const double e = blackbody_sample(t).ln_band_efficiency;
        if (e > best) {
            best = e;
            best_t = t;
        }
    }
    std::ostringstream os;
    os << "eta peaks at " << best_t << " K, ln eta = " << best;
    INFO(os.str());

    CHECK_NEAR_REL(best_t, 6500.0, 0.15,
                   "Wien puts the Planck peak at the photopic peak (555 nm) for T = 5220 K; "
                   "eta weights that by the whole y_bar lobe and divides by sigma T^4, "
                   "which moves the maximum up to about 6500 K. The 15% bound is the width "
                   "of the y_bar lobe expressed as a temperature, not a fitted number");

    // The Sun is close to that peak, which is the anthropic remark the number
    // invites and a good sanity check on the scale.
    const double solar = blackbody_sample(5772.0).ln_band_efficiency;
    CHECK(solar > best - 0.05);
}

TEST(the_table_agrees_with_the_exact_integral) {
    const auto table = render::build_planck_table(1024);
    CHECK_EQ(table.width, std::size_t{1024});
    CHECK_EQ(table.texels.size(), std::size_t{4096});

    // Every temperature the scene can actually produce: a 1439 K catalogue star
    // (the reddest in BSC5) astern at beta = 0.9 up to a 15882 K star ahead.
    double worst_rgb = 0.0;
    double worst_ln_eta = 0.0;
    for (double t = 300.0; t < 120000.0; t *= 1.05) {
        const auto exact = blackbody_sample(t);
        const auto rgb = table.sample_rgb(t);
        worst_rgb = std::max({worst_rgb, std::abs(rgb.r - exact.rgb.r),
                              std::abs(rgb.g - exact.rgb.g), std::abs(rgb.b - exact.rgb.b)});
        worst_ln_eta = std::max(
            worst_ln_eta, std::abs(table.sample_ln_band_efficiency(t) - exact.ln_band_efficiency));
    }

    std::ostringstream os;
    os << "1024-entry table vs the exact integral: worst RGB " << worst_rgb
       << ", worst ln eta " << worst_ln_eta;
    INFO(os.str());

    CHECK_NEAR_ABS(worst_rgb, 0.0, 2.0e-3,
                   "linear interpolation between texel centres, error ~ f''(u) du^2 / 8. "
                   "With 1024 samples of u = T/(T+6000) the step is 45 K at 5800 K and the "
                   "chromaticity moves ~1e-5 per texel there; the bound is set by the "
                   "coldest sampled point, where the colour moves fastest");
    CHECK_NEAR_ABS(worst_ln_eta, 0.0, 0.05,
                   "same interpolation on ln eta, which is the steeper function: near 300 K "
                   "ln eta ~ -A/T with A = 1.8e4, so the second derivative gives ~0.02 per "
                   "texel. 0.05 in a LOGARITHM is 5% of a flux that is already e^-60");
}

TEST(the_beaming_exponent_falls_from_four_to_one) {
    // Section 10.3.  The bolometric law is D^4; the band-limited one approaches D
    // because eta ~ T^-3 in the Rayleigh-Jeans limit.  Measuring the logarithmic
    // slope is the way to see the crossover without quoting any single number.
    // Against the EXACT integral, not the table.  This is a claim about physics,
    // and at beta = 0.99999 the shifted temperature is 2.6e6 K, where the
    // bijective index u = T/(T+6000) puts 2.8e5 K inside one texel: the table is
    // built so that the CHROMATICITY has converged there, and ln eta has not --
    // it goes on falling as -3 ln T.  That coarseness is measured separately, over
    // the range the scene can actually produce.
    constexpr double kStar = 5800.0;

    auto ln_beaming_exact = [](double rest_t, double d) {
        return 4.0 * std::log(d) + blackbody_sample(rest_t * d).ln_band_efficiency -
               blackbody_sample(rest_t).ln_band_efficiency;
    };
    auto slope_at = [&](double beta) {
        const double gamma = 1.0 / std::sqrt(1.0 - beta * beta);
        const double d = gamma * (1.0 + beta);
        const double h = 1.0e-4;
        return (ln_beaming_exact(kStar, d * (1.0 + h)) - ln_beaming_exact(kStar, d)) /
               std::log1p(h);
    };

    for (const double beta : {0.0896, 0.5, 0.9048, 0.99, 0.99999}) {
        std::ostringstream os;
        os << "beta " << beta << ": effective exponent " << slope_at(beta);
        INFO(os.str());
    }

    CHECK_NEAR_ABS(slope_at(0.0896), 4.16, 0.15,
                   "at D = 1.094 the spectrum barely moves and the law is still nearly "
                   "bolometric; above 4 because eta is still RISING towards its peak at "
                   "6500 K, which adds to the D^4");
    CHECK_NEAR_ABS(slope_at(0.99999), 1.0, 0.02,
                   "the exact Rayleigh-Jeans limit: Y ~ T while the bolometric goes as T^4, "
                   "so eta ~ T^-3 and D^4 . D^-3 = D. The bound is the residual "
                   "hc/lambda kT = 5.5e-3 at T' = 2.6e6 K");
    CHECK(slope_at(0.5) < slope_at(0.0896));
    CHECK(slope_at(0.9048) < slope_at(0.5));
    CHECK(slope_at(0.99) < slope_at(0.9048));
}

TEST(the_visible_sky_ahead_is_much_dimmer_than_the_bolometric_number) {
    // The headline of section 10.2, and the reason the band matters: at
    // beta = 0.9048 the forward sky is 400x brighter bolometrically and 51x in
    // the visible, while the aft sky is 7600x DARKER than D^4 suggests.
    const auto table = render::build_planck_table(4096);
    constexpr double kStar = 5800.0;
    constexpr double kBeta = 0.9048;
    const double gamma = 1.0 / std::sqrt(1.0 - kBeta * kBeta);

    const double forward = std::exp(render::ln_band_limited_beaming(table, kStar, gamma * (1.0 + kBeta)));
    const double aft = std::exp(render::ln_band_limited_beaming(table, kStar, gamma * (1.0 - kBeta)));

    std::ostringstream os;
    os << "beta 0.9048: forward visible " << forward << "x (bolometric 400x), aft " << aft
       << "x (bolometric 2.5e-3x), contrast " << forward / aft;
    INFO(os.str());

    CHECK_NEAR_REL(forward, 51.5, 0.03,
                   "computed from the same integral in docs/physics/relativistic-rendering.md "
                   "section 10.2; the 3% bound covers the difference between the 4096-entry "
                   "table and the direct integral, measured at 0.2%");
    CHECK_NEAR_REL(aft, 3.3e-7, 0.05,
                   "same table row: eta(1297 K)/eta(5800 K) = 1.32e-4 against D^4 = 2.5e-3. "
                   "Looser than the forward bound because ln eta is steeper at 1297 K");

    // And the contrast is a thousand times the bolometric one, which is the
    // claim that the band-limited treatment makes the effect MORE extreme.
    CHECK(forward / aft > 1.0e8);
}

TEST(the_response_curve_compresses_instead_of_cutting) {
    // Rule 13, stated as a property rather than as a comment.  The claim is NOT
    // "the result never reaches 1" -- in a double it eventually does, and that is
    // rounding, not a branch.  The claim is that the curve never DISCARDS an
    // ordering it was given: no input is mapped to the same output as a brighter
    // one until the two are closer together than a double can tell.
    double previous = -1.0;
    for (double ln_l = -400.0; ln_l < 400.0; ln_l += 0.5) {
        const double r = render::detector_response_from_ln(ln_l);
        CHECK(r >= 0.0 && r <= 1.0);
        CHECK(r >= previous);
        previous = r;
    }

    // Where the double gives up, and why it does not matter.  1/(1+x) rounds to
    // exactly 1 once x < eps/2 = 1.1e-16, i.e. once the luminance exceeds the half
    // saturation by 9e15.
    const double saturation_ln = std::log(render::kDefaultHalfSaturation) + 36.74;
    CHECK(render::detector_response_from_ln(saturation_ln - 1.0) < 1.0);

    std::ostringstream os;
    os << "the response reaches exactly 1.0 at ln L = " << saturation_ln << ", i.e. L = "
       << std::exp(saturation_ln) << " -- "
       << -2.5 * std::log10(std::exp(saturation_ln)) << " in magnitude";
    INFO(os.str());

    CHECK_NEAR_ABS(-2.5 * std::log10(std::exp(saturation_ln)), -37.89, 0.05,
                   "the saturation point expressed where it belongs, on the magnitude scale. "
                   "Nothing this simulator can produce comes near it: the Sun seen from "
                   "Earth is V = -26.7, eleven magnitudes short, and a magnitude 2 star "
                   "beamed by D^4 = 400 at beta = 0.9048 reaches only -4.5. The 0.05 bound "
                   "is the rounding of the 36.74 in the line above");

    // Across the range that IS reachable, the ordering survives nine decades.
    const double bright = render::detector_response_from_ln(std::log(1.0e9));
    const double brighter = render::detector_response_from_ln(std::log(1.0e10));
    CHECK(brighter > bright);
    CHECK(brighter < 1.0);

    // The two forms agree wherever the linear one can be evaluated at all.
    for (const double l : {1.0e-6, 1.0e-3, 0.158, 1.0, 400.0, 1.0e6}) {
        CHECK_NEAR_REL(render::detector_response_from_ln(std::log(l)),
                       render::detector_response(l), 1.0e-12,
                       "the same function through exp/log instead of directly; the bound is "
                       "a few ulps of the round trip");
    }

    // And the exposure table of section 10.4.
    CHECK_NEAR_ABS(render::detector_response(render::flux_from_magnitude(2.0)), 0.5, 1.0e-12,
                   "the default half saturation IS the flux of a magnitude 2 star, so this "
                   "is L/(L+L) = 1/2 exactly; the bound is the ulp of a pow() round trip");
    for (const auto& [magnitude, expected] : {std::pair{-1.46, 0.960}, std::pair{0.0, 0.863},
                                              std::pair{4.0, 0.137}, std::pair{6.0, 0.0245}}) {
        CHECK_NEAR_ABS(render::detector_response(render::flux_from_magnitude(magnitude)),
                       expected, 5.0e-4,
                       "the exposure table of section 10.4, recomputed; the bound is the "
                       "three decimals the table is printed to");
    }
}

TEST(a_steady_jet_beams_one_power_of_d_less_than_a_moving_blob) {
    // A steady flow and a moving body differ by the apparent-length factor D:
    // D^3 eta(DT)/eta(T) against D^4 eta(DT)/eta(T). The table-based blob and the
    // sample-based jet must agree on everything but that one power.
    const auto table = render::build_planck_table();
    for (const double t : {3000.0, 12000.0, 30000.0}) {
        for (const double d : {0.16, 0.31, 1.0, 3.2, 6.2}) {
            const double jet = render::ln_band_limited_steady_jet(t, d);
            const double blob = render::ln_band_limited_beaming(table, t, d);
            CHECK_NEAR_ABS(jet, blob - std::log(d), std::max(1.0e-3, 1.0e-4 * std::abs(blob)),
                           "one power of D apart; the bound is the table's interpolation of "
                           "ln eta -- 1e-3 absolute, or 1e-4 relative where ln eta runs to -39 "
                           "at 480 K -- three orders below what the eye resolves");
        }
        CHECK_NEAR_ABS(render::ln_band_limited_steady_jet(t, 1.0), 0.0, 1.0e-15,
                       "D = 1 changes nothing, exactly");
    }
}
