// The starfield validation harness (Milestone 6.1, Part B), on the SDL_GPU
// renderer (ADR-0009).
//
// Milestone 6 could not judge aberration, Doppler or beaming because the star
// field was invisible, and "invisible" is the one symptom every possible cause
// shares: a wrong projection, an empty buffer, a culled mesh and a black colour
// all produce the same screenshot. So this harness does not look at the sky. It
// walks the pipeline of docs/validation/starfield-debug.md section 2 one stage at
// a time, and every stage answers a question that has a NUMBER for an answer:
//
//   axes        do six stars on the six axes land where the camera says they are
//   scale       does the answer change with the radius the sky is drawn at
//   clip        is every star in front of the near plane and inside the far one
//   depth       do the stars survive the depth buffer at every near plane
//   ladder      do 12, 100 and 8786 stars all reach the framebuffer
//   magnitude   is the rendered brightness monotone in V
//   effects     baseline, +aberration, +Doppler, +beaming, all
//   aberration  GPU angle vs CPU angle at beta = 0.1, 0.5, 0.9, 0.99
//   doppler     GPU chromaticity vs the shifted Planck colour
//   beaming     GPU intensity ratio vs the band-limited D^4
//   snapshots   reference images at 0c, 0.5c, 0.9c, 0.99c
//
// The oracle is never a human impression and never a stored screenshot: it is
// core/, reached through app::StarSky::apparent_direction(), expected_response()
// and expected_colour(). The snapshots exist to catch a REGRESSION, and they are
// compared with a robust metric, not pixel for pixel (section 26 of the
// milestone prompt).
//
// What is measured is the renderer the game runs -- gfx::Renderer::render_sky()
// is the world pass and the composite of a game frame with nothing else in it --
// read back from the GPU.
//
//   spaceflight_starfield_validation OUT_DIR [--seed-references]
//
// Exit 0: every stage passed. 1: read the log. 77: no GPU device to run on.

#include "app/gfx/gpu.hpp"
#include "app/gfx/renderer.hpp"
#include "app/presentation/geometry.hpp"
#include "app/session/star_sky.hpp"

#include <SDL3/SDL.h>
#include <stb_image.h>
#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

namespace {

using sf::math::Vec3;
namespace fs = std::filesystem;

constexpr double kPi = std::numbers::pi;
constexpr double deg(double radians) { return radians * 180.0 / kPi; }
constexpr double rad(double degrees) { return degrees * kPi / 180.0; }

constexpr std::uint32_t WIDTH = 1024;
constexpr std::uint32_t HEIGHT = 640;

// The sky radius the production scene uses (app::StarfieldView::SKY_RADIUS). A
// star field is DIRECTIONAL -- the radius should be arithmetically irrelevant --
// and the `scale` stage is what turns that sentence into a measurement.
constexpr double PRODUCTION_SKY_RADIUS = 1.9e5;
constexpr double PRODUCTION_CAMERA_FAR = 2.0e5;
constexpr double PRODUCTION_CAMERA_NEAR = 0.05;

// Where a star has to land to count as landing there. One pixel is the
// rasteriser's own quantum and the centroid of a 3-pixel sprite cannot do better
// than about a third of one, so 1.5 px is tight without being a coin toss.
constexpr double POSITION_TOLERANCE_PX = 1.5;

// Aberration is checked in ANGLE, because that is what the physics predicts. The
// budget is the angle one pixel subtends at the centre of the frame, doubled.
constexpr double ANGLE_TOLERANCE_SCALE = 2.0;

// Chromaticity: agreement in the ratio of the channels, to 4%, not in the
// absolute value -- the target holds 8 bits per channel.
constexpr double CHROMATICITY_TOLERANCE = 0.04;

// Intensity ratios span four decades; 12%, because an 8-bit channel at a value of
// 30 already carries 3% of quantisation on its own.
constexpr double INTENSITY_RATIO_TOLERANCE = 0.12;

// The faintest byte a synthetic star is looked for at. Two, not one: one is a
// single quantisation step and rounding alone can produce it.
constexpr int FAINT_THRESHOLD_BYTE = 2;

// What the framebuffer can hold. A star whose LINEAR response would encode below
// one byte is not dim in the image, it is absent from it. The sensor's floor, not
// a tolerance: a red-shifted star that goes out is the physics working.
constexpr double DETECTOR_FLOOR = 0.0004;

// How closely a measured pixel has to reproduce what core/ says the detector
// response is: the quantisation of an 8-bit channel at the dim end, and nothing
// else.
constexpr double PHOTOMETRY_TOLERANCE = 0.03;

[[gnu::format(printf, 1, 2)]] std::string format(const char* fmt, ...) {
    char buffer[2048];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof buffer, fmt, args);
    va_end(args);
    return buffer;
}

// "0.5" -> "0p5", the way the file names were spelt.
std::string beta_tag(double beta) {
    std::string text = format("%g", beta);
    if (text.find('.') == std::string::npos) {
        text += ".0";
    }
    std::replace(text.begin(), text.end(), '.', 'p');
    return text;
}

// IEC 61966-2-1 sRGB, inverted, tabulated over the 256 values a byte can hold.
const std::array<float, 256>& srgb_decode() {
    static const std::array<float, 256> table = [] {
        std::array<float, 256> t{};
        for (int i = 0; i < 256; ++i) {
            const double c = i / 255.0;
            t[static_cast<std::size_t>(i)] =
                static_cast<float>(c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4));
        }
        return t;
    }();
    return table;
}

struct Pixel2 {
    double x{0.0};
    double y{0.0};
};
double distance(Pixel2 a, Pixel2 b) { return std::hypot(a.x - b.x, a.y - b.y); }

// The framebuffer, decoded to linear: the shader wrote a linear value and the
// composite encoded it, and comparing the stored byte with what core/ computed
// would be comparing a number with its own transfer function applied.
struct Frame {
    int w{0};
    int h{0};
    std::vector<double> r, g, b, v;
};

Frame decode(const sf::gfx::Pixels& pixels) {
    Frame f;
    f.w = pixels.width;
    f.h = pixels.height;
    const auto n = static_cast<std::size_t>(f.w * f.h);
    f.r.resize(n);
    f.g.resize(n);
    f.b.resize(n);
    f.v.resize(n);
    const auto& table = srgb_decode();
    for (std::size_t i = 0; i < n; ++i) {
        f.r[i] = static_cast<double>(table[pixels.rgba[i * 4]]);
        f.g[i] = static_cast<double>(table[pixels.rgba[i * 4 + 1]]);
        f.b[i] = static_cast<double>(table[pixels.rgba[i * 4 + 2]]);
        f.v[i] = std::max(f.r[i], std::max(f.g[i], f.b[i]));
    }
    return f;
}

struct Blob {
    Pixel2 centre;
    std::array<double, 3> peak{};
    double peak_value{0.0};
    int pixels{0};
    double energy{0.0};
};

// Connected lit pixels, grouped into blobs, each with its intensity-weighted
// centroid and its peak channel values. Weighted rather than the brightest
// pixel: a three-pixel sprite has no unique brightest pixel, and taking the first
// would quantise every measured position onto the grid -- exactly the error the
// aberration stage is trying to measure.
std::vector<Blob> find_blobs(const Frame& f, int threshold_byte = 8) {
    const double threshold = static_cast<double>(srgb_decode()[static_cast<std::size_t>(std::max(1, threshold_byte))]) * 0.999;
    const auto n = static_cast<std::size_t>(f.w * f.h);
    std::vector<std::uint8_t> seen(n, 0);
    std::vector<Blob> out;
    std::vector<std::size_t> stack;
    for (std::size_t seed = 0; seed < n; ++seed) {
        if (seen[seed] != 0 || f.v[seed] <= threshold) {
            continue;
        }
        stack.assign(1, seed);
        seen[seed] = 1;
        double sum_w = 0.0;
        double sum_x = 0.0;
        double sum_y = 0.0;
        Blob blob{};
        while (!stack.empty()) {
            const std::size_t k = stack.back();
            stack.pop_back();
            const int kx = static_cast<int>(k % static_cast<std::size_t>(f.w));
            const int ky = static_cast<int>(k / static_cast<std::size_t>(f.w));
            const double value = f.v[k];
            sum_w += value;
            sum_x += kx * value;
            sum_y += ky * value;
            blob.pixels += 1;
            if (value > blob.peak_value) {
                blob.peak_value = value;
                blob.peak = {f.r[k], f.g[k], f.b[k]};
            }
            for (int dy = -1; dy <= 1; ++dy) {
                const int ny = ky + dy;
                if (ny < 0 || ny >= f.h) {
                    continue;
                }
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = kx + dx;
                    if (nx < 0 || nx >= f.w) {
                        continue;
                    }
                    const auto nk = static_cast<std::size_t>(ny * f.w + nx);
                    if (seen[nk] != 0 || f.v[nk] <= threshold) {
                        continue;
                    }
                    seen[nk] = 1;
                    stack.push_back(nk);
                }
            }
        }
        if (sum_w > 0.0) {
            blob.centre = Pixel2{sum_x / sum_w, sum_y / sum_w};
            blob.energy = sum_w;
            out.push_back(blob);
        }
    }
    return out;
}

const Blob* blob_near(const std::vector<Blob>& blobs, Pixel2 at, double radius) {
    const Blob* best = nullptr;
    double best_d = radius;
    for (const auto& b : blobs) {
        const double d = distance(b.centre, at);
        if (d < best_d) {
            best_d = d;
            best = &b;
        }
    }
    return best;
}

// The harness's camera: the renderer's SkyView, and the same projection
// arithmetic the renderer's perspective() and view_of() do, in double.
struct Camera {
    sf::app::Basis basis{};
    double fov_deg{75.0};
    double near{PRODUCTION_CAMERA_NEAR};
    double far{PRODUCTION_CAMERA_FAR};

    [[nodiscard]] double aspect() const { return static_cast<double>(WIDTH) / static_cast<double>(HEIGHT); }
    [[nodiscard]] double focal() const { return 1.0 / std::tan(rad(fov_deg) * 0.5); }
    [[nodiscard]] Vec3 to_eye(const Vec3& world) const {
        return Vec3{dot(basis.x, world), dot(basis.y, world), dot(basis.z, world)};
    }
    // Godot's is_position_behind(): not in front of the near plane.
    [[nodiscard]] bool behind(const Vec3& world) const { return -to_eye(world).z < near; }
    // Continuous viewport coordinates, origin top-left, y down.
    [[nodiscard]] Pixel2 project(const Vec3& world) const {
        const Vec3 eye = to_eye(world);
        const double x_ndc = eye.x / -eye.z * focal() / aspect();
        const double y_ndc = eye.y / -eye.z * focal();
        return Pixel2{(x_ndc + 1.0) * 0.5 * WIDTH, (1.0 - y_ndc) * 0.5 * HEIGHT};
    }
    // The direction a viewport point looks along.
    [[nodiscard]] Vec3 unproject(Pixel2 p) const {
        const double x_ndc = 2.0 * p.x / WIDTH - 1.0;
        const double y_ndc = 1.0 - 2.0 * p.y / HEIGHT;
        const Vec3 local{x_ndc * aspect() / focal(), y_ndc / focal(), -1.0};
        return (basis * local).normalized();
    }
    [[nodiscard]] sf::gfx::SkyView view() const { return sf::gfx::SkyView{basis, fov_deg, near, far}; }
};

// Viewport coordinates to image INDICES: a projection answers in continuous
// coordinates, a blob centroid is a weighted mean of integer indices, where pixel
// 0 has its centre at 0.5.
Pixel2 to_image(Pixel2 p) { return Pixel2{p.x - 0.5, p.y - 0.5}; }
Pixel2 from_image(Pixel2 p) { return Pixel2{p.x + 0.5, p.y + 0.5}; }

struct Measurement {
    Pixel2 predicted;
    const Blob* blob{nullptr};
    double expected{0.0};
    bool behind{false};
    bool in_frame{false};
    bool below_floor{false};
    double measured{-1.0};
    std::array<double, 3> colour{};
    double off{-1.0};
};

class Harness {
public:
    Harness(sf::gfx::Gpu& gpu, sf::gfx::Renderer& renderer, fs::path out, bool seed)
        : gpu_(gpu), renderer_(renderer), out_(std::move(out)), seed_(seed) {
        sky_.set_magnitude_limit(7.96);
    }

    int run(const std::string& adapter) {
        log("starfield validation harness");
        log(format("SDL_GPU %s, %s", gpu_.driver(), adapter.c_str()));
        log(format("viewport (%u, %u)", WIDTH, HEIGHT));
        log("");
        stage_axes();
        stage_scale();
        stage_clip();
        stage_depth();
        stage_ladder();
        stage_magnitude();
        stage_effects();
        stage_aberration();
        stage_doppler();
        stage_beaming();
        stage_snapshots();
        return finish();
    }

private:
    // ---- framebuffer ------------------------------------------------------

    Frame capture(const std::string& name) {
        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(gpu_.device());
        renderer_.render_sky(cmd, sky_, camera_.view(), WIDTH, HEIGHT);
        SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
        SDL_WaitForGPUFences(gpu_.device(), true, &fence, 1);
        SDL_ReleaseGPUFence(gpu_.device(), fence);
        const auto pixels = gpu_.download(renderer_.frame_texture(), WIDTH, HEIGHT);
        std::vector<std::uint8_t> rgb(static_cast<std::size_t>(pixels.width * pixels.height * 3));
        for (std::size_t i = 0, j = 0; i < pixels.rgba.size(); i += 4, j += 3) {
            rgb[j] = pixels.rgba[i];
            rgb[j + 1] = pixels.rgba[i + 1];
            rgb[j + 2] = pixels.rgba[i + 2];
        }
        const auto path = (out_ / (name + ".png")).string();
        stbi_write_png(path.c_str(), pixels.width, pixels.height, 3, rgb.data(), pixels.width * 3);
        return decode(pixels);
    }

    double pixel_angle() const { return rad(camera_.fov_deg) / static_cast<double>(HEIGHT); }

    Measurement measure(int index, const std::vector<Blob>& blobs, double radius) const {
        Measurement m;
        const Vec3 world = sky_.apparent_direction(index) * sky_radius_;
        m.behind = camera_.behind(world);
        m.expected = sky_.expected_response(index);
        m.below_floor = m.expected < DETECTOR_FLOOR;
        if (!m.behind) {
            m.predicted = to_image(camera_.project(world));
            m.blob = blob_near(blobs, m.predicted, radius);
            m.in_frame = m.predicted.x >= 0.0 && m.predicted.y >= 0.0 && m.predicted.x < WIDTH &&
                         m.predicted.y < HEIGHT;
        }
        if (m.blob != nullptr) {
            m.measured = m.blob->peak_value;
            m.colour = m.blob->peak;
            m.off = distance(m.blob->centre, m.predicted);
        }
        return m;
    }

    static std::string why_missing(const Measurement& m) {
        if (m.behind) {
            return "behind the camera";
        }
        if (!m.in_frame) {
            return format("outside the frame at (%.0f, %.0f)", m.predicted.x, m.predicted.y);
        }
        if (m.below_floor) {
            return format("below the sensor floor (response %.6f < %.6f)", m.expected, DETECTOR_FLOOR);
        }
        return format("NOT FOUND at (%.0f, %.0f) -- above the floor and missing", m.predicted.x, m.predicted.y);
    }

    // ---- scene arrangement --------------------------------------------------

    void load_synthetic(const std::vector<Vec3>& dirs, const std::vector<double>& temps,
                        const std::vector<double>& mags) {
        if (!sky_.load_synthetic(dirs, temps, mags)) {
            fail("load_synthetic failed: " + sky_.last_error());
        }
    }

    void load_real() {
        if (!sky_.load_catalogue("")) {
            fail("load_catalogue failed: " + sky_.last_error());
        }
    }

    // `debug` bypasses the photometry; `flat` bypasses the sprite SHAPING. Two
    // different questions, two different switches (starfield-debug.md s.3).
    void set_debug(bool on, double point_size = 3.0, bool flat = false) {
        renderer_.starfield.debug = on;
        renderer_.starfield.debug_point_size = point_size;
        renderer_.starfield.flat_sprite = flat || on;
    }

    void upload(const Vec3& beta, bool aberration = true, bool doppler = true, bool beaming = true) {
        sky_.update(beta, sky_radius_, aberration, doppler, beaming);
    }

    void look_along(const Vec3& direction, Vec3 up = Vec3{0.0, 1.0, 0.0}) {
        const Vec3 d = direction.normalized();
        if (std::abs(dot(d, up.normalized())) > 0.99) {
            up = std::abs(dot(d, Vec3{1.0, 0.0, 0.0})) < 0.9 ? Vec3{1.0, 0.0, 0.0} : Vec3{0.0, 0.0, -1.0};
        }
        camera_.basis = sf::app::looking_at(d, up);
    }

    // Point the camera so that every star in the catalogue is inside the frame,
    // and widen the field until it is. At beta = 0.99 aberration piles the sky
    // into a cone eight degrees wide; a beaming test spreads its stars over 105.
    void frame_all_stars(Vec3 up = Vec3{0.0, 1.0, 0.0}, double margin = 1.3, double minimum_fov = 10.0,
                         double maximum_fov = 120.0) {
        const int count = sky_.star_count();
        if (count == 0) {
            return;
        }
        Vec3 mean{};
        for (int i = 0; i < count; ++i) {
            mean = mean + sky_.apparent_direction(i);
        }
        if (mean.norm() < 1.0e-6) {
            mean = sky_.apparent_direction(0);
        }
        mean = mean.normalized();
        double widest = 0.0;
        for (int i = 0; i < count; ++i) {
            widest = std::max(widest, sf::math::angle_between(mean, sky_.apparent_direction(i)));
        }
        // The VERTICAL field, which on a landscape frame is the smaller one.
        camera_.fov_deg = std::clamp(deg(widest) * 2.0 * margin, minimum_fov, maximum_fov);
        look_along(mean, up);
    }

    static std::vector<Vec3> spiral(int n) {
        // A deterministic spiral on the sphere: the same n always gives the same
        // directions, so a regression is a regression and not a reseed.
        std::vector<Vec3> dirs;
        for (int k = 0; k < n; ++k) {
            const double z = 1.0 - 2.0 * (k + 0.5) / n;
            const double r = std::sqrt(std::max(0.0, 1.0 - z * z));
            const double phi = k * kPi * (3.0 - std::sqrt(5.0));
            dirs.push_back(Vec3{r * std::cos(phi), r * std::sin(phi), z});
        }
        return dirs;
    }

    // ---- stage 1: six stars on six axes (prompt 19, 20) --------------------

    static constexpr std::array<const char*, 6> AXIS_NAMES = {"star_right(+x)",    "star_left(-x)", "star_forward(+y)",
                                                              "star_backward(-y)", "star_up(+z)",   "star_down(-z)"};
    static std::vector<Vec3> axis_directions() {
        return {Vec3{1, 0, 0}, Vec3{-1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, -1, 0}, Vec3{0, 0, 1}, Vec3{0, 0, -1}};
    }
    void axis_catalogue() {
        load_synthetic(axis_directions(), std::vector<double>(6, 5800.0), std::vector<double>(6, 0.0));
    }

    void stage_axes() {
        heading("1. six stars on six axes  (prompt 19)");
        sky_radius_ = 1000.0;
        camera_.near = 0.01;
        camera_.far = 1.0e5;
        axis_catalogue();
        set_debug(true, 5.0);
        upload(Vec3{});
        const auto dirs = axis_directions();
        for (std::size_t i = 0; i < dirs.size(); ++i) {
            look_along(dirs[i]);
            std::string short_name = AXIS_NAMES[i];
            short_name = short_name.substr(0, short_name.find('('));
            const auto frame = capture(format("01_axis_%zu_%s", i, short_name.c_str()));
            const auto blobs = find_blobs(frame);
            // The centre of a w x h image in INDEX coordinates.
            const Pixel2 centre{(WIDTH - 1) * 0.5, (HEIGHT - 1) * 0.5};
            if (blobs.empty()) {
                fail(format("%s: nothing rendered while looking straight at it", AXIS_NAMES[i]));
                log(format("  %-20s NOTHING RENDERED", AXIS_NAMES[i]));
                continue;
            }
            const Blob* on_axis = blob_near(blobs, centre, 40.0);
            if (on_axis == nullptr) {
                fail(format("%s: %zu blobs rendered, none at the centre of the frame", AXIS_NAMES[i], blobs.size()));
                log(format("  %-20s %zu blobs, none centred", AXIS_NAMES[i], blobs.size()));
                continue;
            }
            const double offset = distance(on_axis->centre, centre);
            const bool ok = offset <= POSITION_TOLERANCE_PX;
            if (!ok) {
                fail(format("%s: %.2f px off the optical axis", AXIS_NAMES[i], offset));
            }
            log(format("  %-20s %zu blobs, centred to %.2f px, peak %.3f   %s", AXIS_NAMES[i], blobs.size(), offset,
                       on_axis->peak_value, ok ? "ok" : "FAIL"));
        }
        set_debug(false);
        log("");
    }

    // ---- stage 2: the radius must not matter (prompt 20) -------------------

    void stage_scale() {
        heading("2. sky radius is arithmetically irrelevant  (prompt 20)");
        axis_catalogue();
        set_debug(true, 5.0);
        const std::array<double, 3> radii = {1.0, 1000.0, PRODUCTION_SKY_RADIUS};
        std::vector<std::vector<Pixel2>> measured;
        for (std::size_t i = 0; i < radii.size(); ++i) {
            sky_radius_ = radii[i];
            // The far plane has to contain the sphere and the near plane has to
            // keep its proportion to it: what this asks is whether the radius
            // matters once the CONFIGURATION around it is held in proportion.
            camera_.far = sky_radius_ * 1.1;
            camera_.near = sky_radius_ * (PRODUCTION_CAMERA_NEAR / PRODUCTION_SKY_RADIUS);
            upload(Vec3{});
            // A direction that hits no axis star dead on, so a projection error
            // shows up as a DIFFERENT offset rather than as nothing.
            look_along(Vec3{1.0, 1.0, 1.0});
            const auto blobs = find_blobs(capture(format("02_scale_%zu", i)));
            std::vector<Pixel2> centres;
            for (const auto& b : blobs) {
                centres.push_back(b.centre);
            }
            std::sort(centres.begin(), centres.end(),
                      [](Pixel2 a, Pixel2 b) { return a.x * 10000.0 + a.y < b.x * 10000.0 + b.y; });
            measured.push_back(centres);
            log(format("  radius %10g : near %.8g far %.8g, %zu stars rendered", sky_radius_, camera_.near,
                       camera_.far, blobs.size()));
        }
        const auto& reference = measured[0];
        for (std::size_t k = 1; k < measured.size(); ++k) {
            const auto& row = measured[k];
            double worst = 0.0;
            for (std::size_t j = 0; j < std::min(reference.size(), row.size()); ++j) {
                worst = std::max(worst, distance(reference[j], row[j]));
            }
            const bool ok = worst <= POSITION_TOLERANCE_PX && row.size() == reference.size() && !row.empty();
            if (!ok) {
                fail(format("sky radius %g moved the stars by %.2f px (%zu vs %zu visible)", radii[k], worst,
                            row.size(), reference.size()));
            }
            log(format("  radius %10g : %zu stars, worst move vs radius %g is %.3f px   %s", radii[k], row.size(),
                       radii[0], worst, ok ? "ok" : "FAIL"));
        }
        set_debug(false);
        log("");
    }

    // ---- stage 3: clip space (prompt 20) ------------------------------------

    void stage_clip() {
        heading("3. clip space  (prompt 20)");
        sky_radius_ = PRODUCTION_SKY_RADIUS;
        camera_.near = PRODUCTION_CAMERA_NEAR;
        camera_.far = PRODUCTION_CAMERA_FAR;
        load_real();
        upload(Vec3{});
        look_along(Vec3{0.3, 0.8, 0.5});

        // The renderer's projection (reverse-Z): z_clip = near/(far-near) z_eye +
        // near far/(far-near), w_clip = -z_eye.
        const double a = camera_.near / (camera_.far - camera_.near);
        const double b = camera_.near * camera_.far / (camera_.far - camera_.near);
        const int count = sky_.star_count();
        int behind = 0;
        int beyond_far = 0;
        int inside_near = 0;
        double min_w = std::numeric_limits<double>::infinity();
        double max_w = -min_w;
        double min_z = min_w;
        double max_z = -min_w;
        for (int i = 0; i < count; ++i) {
            const Vec3 eye = camera_.to_eye(sky_.apparent_direction(i) * sky_radius_);
            const double w = -eye.z;
            if (w <= 0.0) {
                ++behind;
                continue;
            }
            min_w = std::min(min_w, w);
            max_w = std::max(max_w, w);
            const double ndc_z = (a * eye.z + b) / w;
            min_z = std::min(min_z, ndc_z);
            max_z = std::max(max_z, ndc_z);
            if (w > camera_.far) {
                ++beyond_far;
            }
            if (w < camera_.near) {
                ++inside_near;
            }
        }
        log(format("  stars                 : %d", count));
        log(format("  w <= 0 (behind camera): %d   -- expected, half the sky is behind", behind));
        log(format("  closer than near=%g : %d", camera_.near, inside_near));
        log(format("  farther than far=%g : %d", camera_.far, beyond_far));
        log(format("  w in front of camera  : [%.8g, %.8g]", min_w, max_w));
        // Reverse-Z: 1 at the near plane, 0 at the far one.
        log(format("  ndc z (reverse)       : [%.6e, %.6e]", min_z, max_z));
        const bool ok = beyond_far == 0 && inside_near == 0;
        if (!ok) {
            fail(format("clip space: %d stars past the far plane, %d inside the near plane", beyond_far, inside_near));
        }
        log(format("  every visible star inside [near, far]: %s", ok ? "yes" : "NO"));
        log("");
    }

    // ---- stage 3b: depth headroom (prompt 20) -------------------------------
    //
    // The second defect Milestone 6 could not see past the first: on Godot's
    // 24-bit depth buffer a star on the sky sphere landed at near/SKY_RADIUS, and
    // at near = 0.01 that was below one quantisation step -- a third of the sky
    // rounded to the far plane's zero and was dropped by the depth test.
    //
    // This renderer's world depth is a 32-bit FLOAT with reverse-Z, whose steps
    // are relative, not absolute: near/SKY_RADIUS = 5e-8 is a normal float with
    // 23 bits of mantissa below it. So the Milestone 6 near plane is no longer a
    // control that must fail; it is a rung that must pass, like the rest, and the
    // ladder goes two decades further down to show the headroom is real.
    void stage_depth() {
        heading("3b. depth headroom against the near plane  (prompt 20)");
        sky_radius_ = PRODUCTION_SKY_RADIUS;
        camera_.far = PRODUCTION_CAMERA_FAR;
        camera_.fov_deg = 90.0;
        constexpr int STARS = 400;
        load_synthetic(spiral(STARS), std::vector<double>(STARS, 5800.0), std::vector<double>(STARS, 0.0));
        set_debug(true, 3.0);
        upload(Vec3{});
        look_along(Vec3{0.3, 0.8, 0.5});
        const std::array<double, 6> ladder = {1.0e-4, 1.0e-3, 0.01, 0.02, PRODUCTION_CAMERA_NEAR, 0.5};
        for (std::size_t i = 0; i < ladder.size(); ++i) {
            camera_.near = ladder[i];
            const auto blobs = find_blobs(capture(format("03b_depth_near_%zu", i)));
            int in_frame = 0;
            int found = 0;
            for (int k = 0; k < sky_.star_count(); ++k) {
                const auto m = measure(k, blobs, 5.0);
                if (!m.in_frame) {
                    continue;
                }
                ++in_frame;
                if (m.blob != nullptr) {
                    ++found;
                }
            }
            const bool complete = in_frame > 0 && found == in_frame;
            if (!complete) {
                fail(format("depth: at near %g, only %d of %d in-frame stars rendered", camera_.near, found, in_frame));
            }
            log(format("  near %-7g : depth %.4e,  %3d in frame, %3d rendered   %s", camera_.near,
                       camera_.near / sky_radius_, in_frame, found,
                       complete ? "complete" : format("LOSES %d", in_frame - found).c_str()));
        }
        camera_.near = PRODUCTION_CAMERA_NEAR;
        set_debug(false);
        log("  the world depth is D32_FLOAT, reverse-Z: the sky sphere sits at near/190000");
        log("");
    }

    // ---- stage 4: 6 -> 12 -> 100 -> 8786 (prompt 19) ------------------------

    void stage_ladder() {
        heading("4. star count ladder  (prompt 19)");
        sky_radius_ = PRODUCTION_SKY_RADIUS;
        camera_.near = PRODUCTION_CAMERA_NEAR;
        camera_.far = PRODUCTION_CAMERA_FAR;
        const std::array<int, 4> ladder = {6, 12, 100, -1};   // -1 = the whole BSC5 catalogue
        for (std::size_t i = 0; i < ladder.size(); ++i) {
            const int n = ladder[i];
            if (n < 0) {
                load_real();
            } else {
                load_synthetic(spiral(n), std::vector<double>(static_cast<std::size_t>(n), 5800.0),
                               std::vector<double>(static_cast<std::size_t>(n), 1.0));
            }
            set_debug(true, 3.0);
            upload(Vec3{});
            camera_.fov_deg = 90.0;
            look_along(Vec3{0.3, 0.8, 0.5});
            const std::string label =
                n < 0 ? format("BSC5 (%d stars)", sky_.star_count()) : format("%d synthetic", n);
            const auto blobs = find_blobs(capture(format("04_ladder_%zu", i)));
            int lit = 0;
            for (const auto& b : blobs) {
                lit += b.pixels;
            }
            // What means something is how many of the stars the PROJECTION puts
            // inside the frame were found there.
            int expected_in_frame = 0;
            int found = 0;
            for (int k = 0; k < sky_.star_count(); ++k) {
                const auto m = measure(k, blobs, 6.0);
                if (m.in_frame) {
                    ++expected_in_frame;
                    if (m.blob != nullptr) {
                        ++found;
                    }
                }
            }
            // A synthetic set cannot put two stars on the same pixels, so every one
            // has to be found. The real catalogue has genuine close pairs whose
            // joint centroid can sit further from either star than the search
            // radius -- the blob detector's limit, not the renderer's -- so it is
            // asked for 99.5%.
            const int required =
                n >= 0 ? expected_in_frame : static_cast<int>(std::ceil(expected_in_frame * 0.995));
            const bool ok = expected_in_frame > 0 && found >= required && !blobs.empty();
            if (!ok) {
                fail(format("%s: %d of %d stars inside the frame reached the framebuffer (needed %d)", label.c_str(),
                            found, expected_in_frame, required));
            }
            log(format("  %-22s loaded %5d, %4d in frame, %4d found, %4zu blobs, %6d lit px   %s", label.c_str(),
                       sky_.star_count(), expected_in_frame, found, blobs.size(), lit, ok ? "ok" : "FAIL"));
        }
        set_debug(false);
        log("");
    }

    // ---- stage 5: magnitude (prompt 21) -------------------------------------

    void stage_magnitude() {
        heading("5. magnitude is monotone  (prompt 21)");
        camera_.fov_deg = 75.0;
        sky_radius_ = 1000.0;
        camera_.near = 0.01;
        camera_.far = 1.0e5;
        const std::array<double, 6> ladder = {-1.0, 0.0, 1.0, 3.0, 5.0, 6.0};
        std::vector<Vec3> dirs;
        for (std::size_t i = 0; i < ladder.size(); ++i) {
            const double angle = rad(-25.0 + 10.0 * static_cast<double>(i));
            dirs.push_back(Vec3{std::sin(angle), 1.0, 0.0}.normalized());
        }
        load_synthetic(dirs, std::vector<double>(ladder.size(), 5800.0),
                       std::vector<double>(ladder.begin(), ladder.end()));
        // Flat sprites: the quantity under test is the RESPONSE, and a sprite
        // whose size and alpha both track it would put the answer in twice.
        set_debug(false, 7.0, true);
        upload(Vec3{});
        frame_all_stars();

        const auto blobs = find_blobs(capture("05_magnitude"), FAINT_THRESHOLD_BYTE);
        double previous = std::numeric_limits<double>::infinity();
        double previous_m = -previous;
        bool monotone = true;
        int seen = 0;
        double worst_relative = 0.0;
        for (std::size_t i = 0; i < ladder.size(); ++i) {
            const auto m = measure(static_cast<int>(i), blobs, 20.0);
            if (m.blob == nullptr) {
                log(format("  V = %+4.1f  expected %.5f   %s", ladder[i], m.expected, why_missing(m).c_str()));
                if (!m.below_floor) {
                    fail(format("V = %+.1f has response %.4f and did not render", ladder[i], m.expected));
                    monotone = false;
                }
                continue;
            }
            ++seen;
            const double relative = std::abs(m.measured - m.expected) / std::max(m.expected, 1.0e-6);
            worst_relative = std::max(worst_relative, relative);
            if (m.measured >= previous && std::isfinite(previous)) {
                monotone = false;
                fail(format("V = %+.1f is not dimmer than V = %+.1f (%.4f vs %.4f)", ladder[i], previous_m, m.measured,
                            previous));
            }
            log(format("  V = %+4.1f  expected %.5f  measured %.5f  relative %.4f  %3d px  %.2f px off", ladder[i],
                       m.expected, m.measured, relative, m.blob->pixels, m.off));
            previous = m.measured;
            previous_m = ladder[i];
        }
        // Monotone is the question section 21 asks, but a ladder can be monotone
        // and still wrong, so the response is checked against core/ too.
        if (worst_relative > PHOTOMETRY_TOLERANCE) {
            fail(format("magnitude: worst response error %.4f (budget %.4f)", worst_relative, PHOTOMETRY_TOLERANCE));
        }
        log(format("  %d of %zu rendered, strictly decreasing: %s, worst response error %.4f (budget %.3f)", seen,
                   ladder.size(), monotone ? "yes" : "NO", worst_relative, PHOTOMETRY_TOLERANCE));
        log("");
    }

    // ---- stage 6: one effect at a time (prompt 22) --------------------------

    void stage_effects() {
        heading("6. one effect at a time at beta = 0.9  (prompt 22)");
        sky_radius_ = PRODUCTION_SKY_RADIUS;
        camera_.near = PRODUCTION_CAMERA_NEAR;
        camera_.far = PRODUCTION_CAMERA_FAR;
        camera_.fov_deg = 90.0;
        load_real();
        set_debug(false);
        struct Case {
            const char* name;
            bool ab, dop, be;
        };
        const std::array<Case, 5> cases = {Case{"baseline", false, false, false}, Case{"aberration", true, false, false},
                                           Case{"doppler", false, true, false}, Case{"beaming", false, false, true},
                                           Case{"all", true, true, true}};
        const Vec3 beta{0.0, 0.9, 0.0};
        std::vector<std::pair<std::size_t, double>> rows;
        for (std::size_t i = 0; i < cases.size(); ++i) {
            const auto& c = cases[i];
            upload(beta, c.ab, c.dop, c.be);
            look_along(beta);   // into the forward cone
            const auto blobs = find_blobs(capture(format("06_effect_%zu_%s", i, c.name)));
            double energy = 0.0;
            for (const auto& b : blobs) {
                energy += b.energy;
            }
            rows.emplace_back(blobs.size(), energy);
            log(format("  %-11s aberration %-5s doppler %-5s beaming %-5s : %4zu blobs, energy %10.1f", c.name,
                       c.ab ? "true" : "false", c.dop ? "true" : "false", c.be ? "true" : "false", blobs.size(),
                       energy));
        }
        // Each toggle has to CHANGE something, and the baseline must not be empty.
        if (rows[0].first == 0) {
            fail("baseline sky at beta = 0.9 with every effect off is empty");
        }
        for (std::size_t k = 1; k < rows.size(); ++k) {
            if (rows[k].first == rows[0].first && std::abs(rows[k].second - rows[0].second) < 1.0e-6) {
                fail(format("%s changed nothing against the baseline", cases[k].name));
            }
        }
        log("");
    }

    // ---- stage 7: aberration, GPU angle vs CPU angle (prompt 23) ------------

    void stage_aberration() {
        heading("7. aberration: rendered angle vs core/relativity/optics.hpp  (prompt 23)");
        sky_radius_ = 1000.0;
        camera_.near = 0.01;
        camera_.far = 1.0e5;
        camera_.fov_deg = 100.0;
        // Stars at known angles from the boost axis (+y), in the x-y plane, spread
        // over 100 degrees -- laid along the WIDE axis of the frame, hence +z up.
        const std::array<double, 5> angles = {30.0, 55.0, 80.0, 105.0, 130.0};
        std::vector<Vec3> dirs;
        for (double a : angles) {
            dirs.push_back(Vec3{std::sin(rad(a)), std::cos(rad(a)), 0.0});
        }
        load_synthetic(dirs, std::vector<double>(angles.size(), 5800.0), std::vector<double>(angles.size(), 0.0));
        // Debug ON: this is GEOMETRY, and at beta = 0.99 the aft stars are e^-52 of
        // their rest flux -- whether they are bright enough is stage 9's question.
        set_debug(true, 5.0);
        for (double beta : {0.0, 0.1, 0.5, 0.9, 0.99}) {
            upload(Vec3{0.0, beta, 0.0});
            // The field follows the stars: 100 degrees at rest, 15 at 0.99.
            frame_all_stars(Vec3{0.0, 0.0, 1.0}, 1.15);
            const auto blobs = find_blobs(capture("07_aberration_beta_" + beta_tag(beta)));
            const double tolerance = pixel_angle() * ANGLE_TOLERANCE_SCALE;
            double worst = 0.0;
            int worst_star = -1;
            int compared = 0;
            for (std::size_t k = 0; k < angles.size(); ++k) {
                const Vec3 cpu = sky_.apparent_direction(static_cast<int>(k));
                if (camera_.behind(cpu * sky_radius_)) {
                    continue;
                }
                const Blob* blob = blob_near(blobs, to_image(camera_.project(cpu * sky_radius_)), 30.0);
                if (blob == nullptr) {
                    continue;
                }
                // The measured pixel turned back into a direction through the same
                // projection the renderer used: the GPU's answer.
                const Vec3 gpu = camera_.unproject(from_image(blob->centre));
                const double error = sf::math::angle_between(cpu.normalized(), gpu);
                ++compared;
                if (error > worst) {
                    worst = error;
                    worst_star = static_cast<int>(k);
                }
            }
            const bool ok = compared == static_cast<int>(angles.size()) && worst <= tolerance;
            if (!ok) {
                fail(format("aberration at beta = %g: %d of %zu stars matched, worst error %.4f deg (budget %.4f)",
                            beta, compared, angles.size(), deg(worst), deg(tolerance)));
            }
            log(format("  beta %-5g : %d/%zu stars, worst GPU-vs-CPU angle %.5f deg (budget %.5f)  star %d  %s", beta,
                       compared, angles.size(), deg(worst), deg(tolerance), worst_star, ok ? "ok" : "FAIL"));
            if (beta > 0.0) {
                std::string rows;
                for (std::size_t k = 0; k < angles.size(); ++k) {
                    const double seen =
                        deg(sf::math::angle_between(sky_.apparent_direction(static_cast<int>(k)), Vec3{0, 1, 0}));
                    rows += format("%s%.0f->%.2f", k == 0 ? "" : ", ", angles[k], seen);
                }
                log(format("           rest angle -> apparent: %s   (90 deg lands at %.3f)", rows.c_str(),
                           deg(std::acos(std::min(1.0, beta)))));
            }
        }
        set_debug(false);
        log("");
    }

    // ---- stage 8: Doppler chromaticity (prompt 24) --------------------------

    void stage_doppler() {
        heading("8. Doppler: rendered chromaticity vs the shifted Planck colour  (prompt 24)");
        sky_radius_ = 1000.0;
        camera_.fov_deg = 100.0;
        // Each temperature at two angles to the boost: ahead (blue-shifted) and
        // astern (red-shifted). Aberration stays ON -- the CPU is asked where each
        // star ended up; what is tested here is the COLOUR.
        const std::array<double, 3> temperatures = {3000.0, 5800.0, 10000.0};
        constexpr double beta = 0.5;
        std::vector<Vec3> dirs;
        std::vector<double> temps;
        std::vector<double> mags;
        std::vector<std::string> labels;
        for (std::size_t i = 0; i < temperatures.size(); ++i) {
            for (int s = 0; s < 2; ++s) {
                const double polar = rad(s == 0 ? 40.0 : 120.0);
                // Spread in azimuth so no two blobs merge, WITHOUT changing any
                // star's angle to the boost -- all the Doppler factor depends on.
                const double azimuth = rad(-30.0 + 12.0 * static_cast<double>(i * 2 + static_cast<std::size_t>(s)));
                dirs.push_back(Vec3{std::sin(polar) * std::cos(azimuth), std::cos(polar),
                                    std::sin(polar) * std::sin(azimuth)});
                temps.push_back(temperatures[i]);
                // Astern stars are red-shifted AND beamed away; bright enough to
                // measure, because a star the framebuffer cannot hold carries no
                // chromaticity at all.
                mags.push_back(s == 0 ? 1.5 : -3.5);
                labels.push_back(format("%.0fK %s", temperatures[i], s == 0 ? "ahead" : "astern"));
            }
        }
        load_synthetic(dirs, temps, mags);
        set_debug(false, 7.0, true);
        upload(Vec3{0.0, beta, 0.0});
        frame_all_stars();

        const auto blobs = find_blobs(capture("08_doppler"), FAINT_THRESHOLD_BYTE);
        double worst = 0.0;
        int compared = 0;
        int skipped = 0;
        for (std::size_t k = 0; k < labels.size(); ++k) {
            const int index = static_cast<int>(k);
            const auto m = measure(index, blobs, 20.0);
            const auto expected = sky_.expected_colour(index);
            const double d = sky_.doppler_of(index);
            const double shifted = sky_.star_temperature(index) * d;
            if (m.blob == nullptr) {
                log(format("  %-14s D %.4f  T' %7.0f K   %s", labels[k].c_str(), d, shifted, why_missing(m).c_str()));
                if (m.below_floor) {
                    ++skipped;
                } else {
                    fail(format("Doppler: %s is above the sensor floor and did not render", labels[k].c_str()));
                }
                continue;
            }
            // Chromaticity, not absolute value: the ratio between the channels.
            const double es = expected.r + expected.g + expected.b;
            const double ms = m.colour[0] + m.colour[1] + m.colour[2];
            if (es <= 0.0 || ms <= 0.0) {
                ++skipped;
                continue;
            }
            const double er = expected.r / es;
            const double eb = expected.b / es;
            const double mr = m.colour[0] / ms;
            const double mb = m.colour[2] / ms;
            const double error = std::max(std::abs(er - mr), std::abs(eb - mb));
            ++compared;
            worst = std::max(worst, error);
            log(format("  %-14s D %.4f  T' %7.0f K  expected r/b %.3f/%.3f  measured %.3f/%.3f  err %.4f",
                       labels[k].c_str(), d, shifted, er, eb, mr, mb, error));
        }
        const bool ok = compared > 0 && worst <= CHROMATICITY_TOLERANCE;
        if (!ok) {
            fail(format("Doppler chromaticity: %d compared, worst %.4f (budget %.4f)", compared, worst,
                        CHROMATICITY_TOLERANCE));
        }
        log(format("  %d compared, %d below the sensor floor, worst chromaticity error %.4f (budget %.4f)   %s",
                   compared, skipped, worst, CHROMATICITY_TOLERANCE, ok ? "ok" : "FAIL"));
        log("");
    }

    // ---- stage 9: beaming (prompt 25) ---------------------------------------

    void stage_beaming() {
        heading("9. beaming: rendered intensity ratios vs the band-limited D^4  (prompt 25)");
        sky_radius_ = 1000.0;
        camera_.fov_deg = 110.0;
        const std::array<double, 5> angles = {0.0, 45.0, 75.0, 90.0, 105.0};
        std::vector<Vec3> dirs;
        for (double a : angles) {
            dirs.push_back(Vec3{std::sin(rad(a)), std::cos(rad(a)), 0.0});
        }
        // Magnitude 4: dim enough at rest that the forward star is not saturated.
        load_synthetic(dirs, std::vector<double>(angles.size(), 5800.0), std::vector<double>(angles.size(), 4.0));
        set_debug(false, 7.0, true);
        // Aberration OFF: this is about INTENSITY, and D depends on where the star
        // is, not on where it appears.
        upload(Vec3{0.0, 0.5, 0.0}, false, true, true);
        frame_all_stars(Vec3{0.0, 0.0, 1.0}, 1.15);

        const auto blobs = find_blobs(capture("09_beaming"), FAINT_THRESHOLD_BYTE);
        std::vector<double> measured;
        std::vector<double> expected;
        for (std::size_t k = 0; k < angles.size(); ++k) {
            const int index = static_cast<int>(k);
            const auto m = measure(index, blobs, 20.0);
            measured.push_back(m.measured);
            expected.push_back(m.expected);
            if (m.blob == nullptr) {
                log(format("  %5.0f deg  D %.4f  expected response %.5f   %s", angles[k], sky_.beaming_of(index),
                           m.expected, why_missing(m).c_str()));
                if (!m.below_floor) {
                    fail(format("beaming: the star at %.0f deg is above the sensor floor and did not render",
                                angles[k]));
                }
                continue;
            }
            log(format("  %5.0f deg  D %.4f  expected response %.5f  measured %.5f  %.2f px off", angles[k],
                       sky_.beaming_of(index), m.expected, m.measured, m.off));
        }
        // Ratios against the 90-degree star; the absolute value is checked as a
        // separate number, so a wrong exposure and a wrong exponent cannot cancel.
        const std::size_t reference = 3;
        double worst = 0.0;
        double worst_absolute = 0.0;
        int compared = 0;
        for (std::size_t k = 0; k < angles.size(); ++k) {
            if (measured[k] <= 0.0) {
                continue;
            }
            worst_absolute = std::max(worst_absolute, std::abs(measured[k] - expected[k]) / std::max(expected[k], 1.0e-6));
            if (k == reference || measured[reference] <= 0.0) {
                continue;
            }
            const double er = expected[k] / expected[reference];
            const double mr = measured[k] / measured[reference];
            const double relative = std::abs(mr - er) / er;
            ++compared;
            worst = std::max(worst, relative);
            log(format("    %5.0f deg / 90 deg : expected %8.4f  measured %8.4f  relative error %.4f", angles[k], er, mr,
                       relative));
        }
        const bool ok = compared == static_cast<int>(angles.size()) - 1 && worst <= INTENSITY_RATIO_TOLERANCE &&
                        worst_absolute <= PHOTOMETRY_TOLERANCE;
        if (!ok) {
            fail(format("beaming: %d of %zu ratios, worst ratio error %.4f (budget %.4f), worst absolute %.4f "
                        "(budget %.4f)",
                        compared, angles.size() - 1, worst, INTENSITY_RATIO_TOLERANCE, worst_absolute,
                        PHOTOMETRY_TOLERANCE));
        }
        log(format("  %d ratios, worst ratio error %.4f (budget %.4f), worst absolute response error %.4f (budget "
                   "%.4f)   %s",
                   compared, worst, INTENSITY_RATIO_TOLERANCE, worst_absolute, PHOTOMETRY_TOLERANCE,
                   ok ? "ok" : "FAIL"));
        log("");
    }

    // ---- stage 10: reference snapshots (prompt 26) --------------------------
    //
    // How different two renders of the same sky may be and still count as the
    // same sky: the mean absolute difference of a 64 x 64 luminance downsample,
    // insensitive to a point sprite moving by a fraction of a pixel and sensitive
    // to a star that moved, vanished or changed colour.

    static constexpr double SNAPSHOT_TOLERANCE = 0.02;
    static constexpr int SNAPSHOT_GRID = 64;

    static std::vector<double> downsample(const Frame& f, int n) {
        std::vector<double> out(static_cast<std::size_t>(n * n));
        for (int gy = 0; gy < n; ++gy) {
            const int y0 = gy * f.h / n;
            const int y1 = std::max(y0 + 1, (gy + 1) * f.h / n);
            for (int gx = 0; gx < n; ++gx) {
                const int x0 = gx * f.w / n;
                const int x1 = std::max(x0 + 1, (gx + 1) * f.w / n);
                double total = 0.0;
                for (int y = y0; y < y1; ++y) {
                    for (int x = x0; x < x1; ++x) {
                        const auto i = static_cast<std::size_t>(y * f.w + x);
                        total += 0.2126 * f.r[i] + 0.7152 * f.g[i] + 0.0722 * f.b[i];
                    }
                }
                out[static_cast<std::size_t>(gy * n + gx)] = total / ((x1 - x0) * (y1 - y0));
            }
        }
        return out;
    }

    static std::optional<Frame> load_png(const fs::path& path) {
        int w = 0;
        int h = 0;
        int channels = 0;
        stbi_uc* data = stbi_load(path.string().c_str(), &w, &h, &channels, 4);
        if (data == nullptr) {
            return std::nullopt;
        }
        sf::gfx::Pixels pixels;
        pixels.width = w;
        pixels.height = h;
        pixels.rgba.assign(data, data + static_cast<std::size_t>(w * h * 4));
        stbi_image_free(data);
        return decode(pixels);
    }

    void stage_snapshots() {
        heading("10. reference snapshots  (prompt 26)");
        sky_radius_ = PRODUCTION_SKY_RADIUS;
        camera_.near = PRODUCTION_CAMERA_NEAR;
        camera_.far = PRODUCTION_CAMERA_FAR;
        camera_.fov_deg = 75.0;
        load_real();
        set_debug(false);
        const fs::path references = fs::path{SPACEFLIGHT_SOURCE_DIR} / "docs/validation/starfield-reference";
        for (double beta : {0.0, 0.5, 0.9, 0.99}) {
            upload(Vec3{0.0, beta, 0.0});
            // One attitude for the whole ladder, 40 degrees off the boost, so
            // what changes between the images is the PHYSICS.
            camera_.basis = sf::app::looking_at(Vec3{std::sin(rad(40.0)), std::cos(rad(40.0)), 0.0}, Vec3{0, 0, 1});
            const std::string name = "10_sky_beta_" + beta_tag(beta);
            const Frame frame = capture(name);
            int lit = 0;
            for (const auto& b : find_blobs(frame)) {
                lit += b.pixels;
            }
            std::string note;
            const fs::path reference = references / (name + ".png");
            if (seed_) {
                // Seeding is a deliberate act, never a side effect of a run: a
                // suite that adopts whatever it just rendered cannot fail.
                fs::create_directories(references);
                fs::copy_file(out_ / (name + ".png"), reference, fs::copy_options::overwrite_existing);
                note = "reference SEEDED from this run";
            } else if (const auto stored = load_png(reference)) {
                const auto a = downsample(frame, SNAPSHOT_GRID);
                const auto b = downsample(*stored, SNAPSHOT_GRID);
                double total = 0.0;
                for (std::size_t i = 0; i < a.size(); ++i) {
                    total += std::abs(a[i] - b[i]);
                }
                const double difference = stored->w == frame.w && stored->h == frame.h
                                              ? total / static_cast<double>(a.size())
                                              : std::numeric_limits<double>::infinity();
                const bool ok = difference <= SNAPSHOT_TOLERANCE;
                if (!ok) {
                    fail(format("snapshot beta = %g differs from the reference by %.4f (budget %.4f)", beta,
                                difference, SNAPSHOT_TOLERANCE));
                }
                note = format("vs reference: %.5f (budget %.3f)  %s", difference, SNAPSHOT_TOLERANCE, ok ? "ok" : "FAIL");
            } else {
                note = "no reference stored yet -- --seed-references stores this run's";
            }
            log(format("  beta %-5g : %6d lit pixels   %s", beta, lit, note.c_str()));
        }
        log("");
    }

    // ---- reporting ------------------------------------------------------------

    void heading(const std::string& text) { log("---- " + text); }
    void log(const std::string& text) {
        lines_.push_back(text);
        std::printf("[starfield] %s\n", text.c_str());
        std::fflush(stdout);
    }
    void fail(const std::string& text) { failures_.push_back(text); }

    int finish() {
        log("================================================================");
        if (failures_.empty()) {
            log("STARFIELD: PASS  -- every stage measured, nothing outside budget");
        } else {
            log(format("STARFIELD: FAIL  -- %zu finding(s):", failures_.size()));
            for (const auto& f : failures_) {
                log("  * " + f);
            }
        }
        std::ofstream file(out_ / "starfield-validation.log");
        for (const auto& line : lines_) {
            file << line << '\n';
        }
        std::printf("[starfield] artefacts in %s\n", out_.string().c_str());
        return failures_.empty() ? 0 : 1;
    }

    sf::gfx::Gpu& gpu_;
    sf::gfx::Renderer& renderer_;
    fs::path out_;
    bool seed_{false};
    sf::app::StarSky sky_;
    Camera camera_;
    double sky_radius_{1000.0};
    std::vector<std::string> lines_;
    std::vector<std::string> failures_;
};

}  // namespace

int main(int argc, char** argv) {
    fs::path out = argc > 1 ? fs::path{argv[1]} : fs::path{"starfield"};
    bool seed = false;
    for (int i = 2; i < argc; ++i) {
        if (std::string{argv[i]} == "--seed-references") {
            seed = true;
        }
    }
    fs::create_directories(out);

    // Off-screen: no window, no display. SDL_GPU still wants the video subsystem;
    // where there is no display at all, the "offscreen" driver stands in.
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            std::fprintf(stderr, "SKIP: SDL: %s\n", SDL_GetError());
            return 77;
        }
    }
    std::string error;
    auto gpu = sf::gfx::Gpu::create(nullptr, false, error);
    if (!gpu) {
        // No device is not a failure of the star field: the machine cannot run
        // this suite, and that is a different fact (CTest: Skipped).
        std::fprintf(stderr, "SKIP: no GPU device: %s\n", error.c_str());
        SDL_Quit();
        return 77;
    }
    int status = 1;
    {
        sf::app::StarSky probe;
        const sf::render::PlanckTable* table = nullptr;
        sf::render::PlanckTable fallback = sf::render::build_planck_table(1024);
        if (probe.load_catalogue("") && probe.planck_table() != nullptr) {
            table = probe.planck_table();
        } else {
            table = &fallback;
        }
        sf::gfx::Renderer renderer(*gpu, "");
        if (!renderer.initialise_sky(table, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM)) {
            std::fprintf(stderr, "renderer: could not build the pipelines\n");
            return 1;
        }
        Harness harness(*gpu, renderer, out, seed);
        status = harness.run(SDL_GetCurrentVideoDriver() != nullptr ? SDL_GetCurrentVideoDriver() : "");
    }
    gpu.reset();
    SDL_Quit();
    return status;
}
