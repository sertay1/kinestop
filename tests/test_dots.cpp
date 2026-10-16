// Kinestop host tests - common/dots.{hpp,cpp} against the original overlay.
//
// Part of a modified version of the original Kinestop by SertAy
// (https://github.com/sertay1/Kinestop).
//
// The "original_*" code below is copied from SertAy's original overlay
// (`git show main:overlay/source/main.cpp`): the option tables,
// get_anchors() and the displacement math of draw_motion_dots(). Only the
// tsl types were replaced by plain ones; the arithmetic is untouched. It is
// the oracle this version must reproduce exactly.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

#include "check.hpp"
#include "config.hpp"
#include "dots.hpp"
#include "math_types.hpp"

namespace dots = kinestop::dots;

namespace original {

constexpr float KINESTOP_PI = 3.14159265358979323846f;
using u8 = uint8_t;

struct ColorTheme {
    const char* name_en;
    const char* name_tr;
    u8 cr, cg, cb; // Core color
    u8 br, bg, bb; // Border contrast color
};

static const ColorTheme THEMES[] = {
    {"Ice Blue",      "Buz Mavisi",    175, 235, 255,   10,  25,  50},  // 0: Classic Ice Cyan
    {"Pure White",    "Beyaz",         255, 255, 255,   20,  20,  20},  // 1: Clean Monochrome
    {"Neon Green",    "Fıstık Yeşili",  80, 250, 140,    5,  40,  15},  // 2: Zelda Lime Green
    {"Dark Green",    "Koyu Yeşil",     40, 180,  80,    5,  30,  10},  // 3: Deep Forest Green
    {"Bright Yellow", "Sarı",          255, 235,  60,   45,  35,   5},  // 4: Vivid Lemon Yellow
    {"Amber Orange",  "Kehribar",      255, 140,  40,   45,  20,   5},  // 5: Warm Sunset Amber
    {"Crimson Red",   "Kırmızı",       255,  65,  75,   45,  10,  15},  // 6: Night Vision Red
    {"Hot Pink",      "Neon Pembe",    255, 105, 180,   45,  10,  30},  // 7: Vibrant Neon Pink
    {"Deep Purple",   "Mor",           185,  90, 255,   30,  10,  45},  // 8: Synthwave Purple
    {"Sky Blue",      "Gök Mavisi",     90, 190, 255,   10,  25,  45},  // 9: Azure Sky Blue
    {"Navy Blue",     "Koyu Mavi",      40, 110, 240,    5,  15,  40},  // 10: Deep Royal Blue
    {"Warm Gray",     "Mat Gri",       180, 185, 195,   30,  30,  35},  // 11: Subtle Minimalist Gray
};
constexpr size_t NUM_THEMES = sizeof(THEMES) / sizeof(THEMES[0]);

static const int SIZES[] = {1, 2, 3, 4, 5, 6, 7, 8, 10, 12};
constexpr size_t NUM_SIZES = sizeof(SIZES) / sizeof(SIZES[0]);

static const float SENSITIVITIES[] = {0.2f, 0.5f, 0.8f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f};
constexpr size_t NUM_SENSITIVITIES = sizeof(SENSITIVITIES) / sizeof(SENSITIVITIES[0]);

static const float SMOOTHINGS[] = {0.50f, 0.75f, 0.85f, 0.92f, 0.97f};
constexpr size_t NUM_SMOOTHINGS = sizeof(SMOOTHINGS) / sizeof(SMOOTHINGS[0]);

static const float DEADZONES[] = {0.0f, 0.2f, 0.5f, 1.0f, 1.5f, 2.0f, 3.0f, 5.0f};
constexpr size_t NUM_DEADZONES = sizeof(DEADZONES) / sizeof(DEADZONES[0]);

// Generates dot anchors based on Placement Style and Density Level with 0 overlapping
std::vector<std::pair<float, float>> get_anchors(int style, int density) {
    std::vector<std::pair<float, float>> anchors;
    int d = std::clamp(density, 0, 3); // 0: Low, 1: Medium, 2: High, 3: Ultra

    switch (style) {
        default:
        case 0: { // 4-Border Perimeter
            int counts[] = {3, 5, 7, 9};
            int n_col = counts[d];
            int n_row = counts[d];

            for (int i = 0; i < n_col; ++i) {
                float y = 90.0f + (float)i * (540.0f / (float)(n_col - 1));
                anchors.push_back({32.0f, y});
                anchors.push_back({1248.0f, y});
            }
            for (int i = 1; i < n_row - 1; ++i) {
                float x = 32.0f + (float)i * (1216.0f / (float)(n_row - 1));
                anchors.push_back({x, 32.0f});
                anchors.push_back({x, 688.0f});
            }
            break;
        }

        case 1: { // Full Screen Grid (Covering middle & borders)
            struct GridDim { int cols; int rows; };
            GridDim dims[] = {{4, 3}, {6, 4}, {8, 5}, {10, 6}};
            GridDim g = dims[d];

            float start_x = 60.0f, end_x = 1220.0f;
            float start_y = 50.0f, end_y = 670.0f;

            for (int r = 0; r < g.rows; ++r) {
                float y = start_y + (float)r * ((end_y - start_y) / (float)(g.rows - 1));
                for (int c = 0; c < g.cols; ++c) {
                    float x = start_x + (float)c * ((end_x - start_x) / (float)(g.cols - 1));
                    anchors.push_back({x, y});
                }
            }
            break;
        }

        case 2: { // Side Columns
            int counts[] = {4, 7, 10, 13};
            int n = counts[d];
            for (int i = 0; i < n; ++i) {
                float y = 70.0f + (float)i * (580.0f / (float)(n - 1));
                anchors.push_back({32.0f, y});
                anchors.push_back({1248.0f, y});
            }
            break;
        }

        case 3: { // Clean 4 Corners (Zero Overlap Guaranteed)
            if (d == 0) { // Low: 2 dots per corner
                anchors.push_back({35.0f, 35.0f});   anchors.push_back({115.0f, 35.0f});
                anchors.push_back({1245.0f, 35.0f}); anchors.push_back({1165.0f, 35.0f});
                anchors.push_back({35.0f, 685.0f});  anchors.push_back({115.0f, 685.0f});
                anchors.push_back({1245.0f, 685.0f});anchors.push_back({1165.0f, 685.0f});
            } else if (d == 1) { // Medium: 4 dots per corner (2x2)
                anchors.push_back({35.0f, 35.0f});   anchors.push_back({115.0f, 35.0f});
                anchors.push_back({35.0f, 115.0f});  anchors.push_back({115.0f, 115.0f});

                anchors.push_back({1245.0f, 35.0f}); anchors.push_back({1165.0f, 35.0f});
                anchors.push_back({1245.0f, 115.0f});anchors.push_back({1165.0f, 115.0f});

                anchors.push_back({35.0f, 685.0f});  anchors.push_back({115.0f, 685.0f});
                anchors.push_back({35.0f, 605.0f});  anchors.push_back({115.0f, 605.0f});

                anchors.push_back({1245.0f, 685.0f});anchors.push_back({1165.0f, 685.0f});
                anchors.push_back({1245.0f, 605.0f});anchors.push_back({1165.0f, 605.0f});
            } else if (d == 2) { // High: 6 dots per corner (L-bracket)
                anchors.push_back({35.0f, 35.0f});   anchors.push_back({115.0f, 35.0f});  anchors.push_back({195.0f, 35.0f});
                anchors.push_back({35.0f, 115.0f});  anchors.push_back({35.0f, 195.0f});  anchors.push_back({115.0f, 115.0f});

                anchors.push_back({1245.0f, 35.0f}); anchors.push_back({1165.0f, 35.0f}); anchors.push_back({1085.0f, 35.0f});
                anchors.push_back({1245.0f, 115.0f});anchors.push_back({1245.0f, 195.0f});anchors.push_back({1165.0f, 115.0f});

                anchors.push_back({35.0f, 685.0f});  anchors.push_back({115.0f, 685.0f});  anchors.push_back({195.0f, 685.0f});
                anchors.push_back({35.0f, 605.0f});  anchors.push_back({35.0f, 525.0f});  anchors.push_back({115.0f, 605.0f});

                anchors.push_back({1245.0f, 685.0f});anchors.push_back({1165.0f, 685.0f}); anchors.push_back({1085.0f, 685.0f});
                anchors.push_back({1245.0f, 605.0f});anchors.push_back({1245.0f, 525.0f});anchors.push_back({1165.0f, 605.0f});
            } else { // Ultra: 8 dots per corner (3x3 diagonal fan)
                anchors.push_back({35.0f, 35.0f});   anchors.push_back({115.0f, 35.0f});  anchors.push_back({195.0f, 35.0f});  anchors.push_back({275.0f, 35.0f});
                anchors.push_back({35.0f, 115.0f});  anchors.push_back({35.0f, 195.0f});  anchors.push_back({35.0f, 275.0f});  anchors.push_back({115.0f, 115.0f});

                anchors.push_back({1245.0f, 35.0f}); anchors.push_back({1165.0f, 35.0f}); anchors.push_back({1085.0f, 35.0f}); anchors.push_back({1005.0f, 35.0f});
                anchors.push_back({1245.0f, 115.0f});anchors.push_back({1245.0f, 195.0f});anchors.push_back({1245.0f, 275.0f});anchors.push_back({1165.0f, 115.0f});

                anchors.push_back({35.0f, 685.0f});  anchors.push_back({115.0f, 685.0f});  anchors.push_back({195.0f, 685.0f});  anchors.push_back({275.0f, 685.0f});
                anchors.push_back({35.0f, 605.0f});  anchors.push_back({35.0f, 525.0f});  anchors.push_back({35.0f, 445.0f});  anchors.push_back({115.0f, 605.0f});

                anchors.push_back({1245.0f, 685.0f});anchors.push_back({1165.0f, 685.0f}); anchors.push_back({1085.0f, 685.0f}); anchors.push_back({1005.0f, 685.0f});
                anchors.push_back({1245.0f, 605.0f});anchors.push_back({1245.0f, 525.0f});anchors.push_back({1245.0f, 445.0f});anchors.push_back({1165.0f, 605.0f});
            }
            break;
        }

        case 4: { // Top & Bottom Rows
            int counts[] = {4, 7, 10, 13};
            int n = counts[d];
            for (int i = 0; i < n; ++i) {
                float x = 120.0f + (float)i * (1040.0f / (float)(n - 1));
                anchors.push_back({x, 32.0f});
                anchors.push_back({x, 688.0f});
            }
            break;
        }

        case 5: { // Surround Field
            for (int i = 0; i < 5; ++i) {
                float y = 100.0f + (float)i * 130.0f;
                anchors.push_back({32.0f, y});
                anchors.push_back({1248.0f, y});
                float x = 240.0f + (float)i * 200.0f;
                anchors.push_back({x, 32.0f});
                anchors.push_back({x, 688.0f});
            }
            int inner_counts[] = {4, 8, 12, 16};
            int in_n = inner_counts[d];
            for (int i = 0; i < in_n; ++i) {
                float angle = (float)i * (2.0f * KINESTOP_PI / (float)in_n);
                anchors.push_back({640.0f + 480.0f * std::cos(angle), 360.0f + 240.0f * std::sin(angle)});
            }
            break;
        }
    }
    return anchors;
}

// Displacement half of draw_motion_dots(), with g_orientation/g_config as
// parameters. Returns {target_dx, target_dy}.
std::pair<float, float> motion_offset(const kinestop::Orientation& g_orientation, const kinestop::Config& g_config) {
    // Handheld calibration delta calculation
    float pitch = g_orientation.pitch_deg - g_config.calibration.pitch_offset;
    float roll  = g_orientation.roll_deg  - g_config.calibration.roll_offset;

    // Apply Deadzone
    if (std::abs(pitch) < g_config.filter.deadzone_deg) pitch = 0.0f;
    if (std::abs(roll)  < g_config.filter.deadzone_deg) roll  = 0.0f;

    if (g_config.calibration.invert_pitch) pitch = -pitch;
    if (g_config.calibration.invert_roll)  roll  = -roll;

    // Dynamic floating displacement range
    float sens_multiplier = g_config.calibration.sensitivity_roll;
    float max_offset = g_config.visual.free_floating ? (36.0f * std::max(1.0f, sens_multiplier * 0.7f)) : 36.0f;
    float sens_x = 1.3f * g_config.calibration.sensitivity_roll;
    float sens_y = 1.1f * g_config.calibration.sensitivity_pitch;

    float target_dx = -std::clamp(roll * sens_x, -max_offset, max_offset);
    float target_dy =  std::clamp(pitch * sens_y, -max_offset, max_offset);
    return {target_dx, target_dy};
}

float dot_radius(const kinestop::Config& g_config) {
    int size_idx = std::clamp(g_config.visual.line_thickness, 0, (int)NUM_SIZES - 1);
    return (float)SIZES[size_idx] * 0.9f;
}

} // namespace original

namespace {

void test_tables() {
    CHECK_EQ(dots::NUM_THEMES, original::NUM_THEMES);
    for (size_t i = 0; i < std::min(dots::NUM_THEMES, original::NUM_THEMES); ++i) {
        const auto& a = dots::THEMES[i];
        const auto& b = original::THEMES[i];
        CHECK_ALL(!std::strcmp(a.name_en, b.name_en) && !std::strcmp(a.name_tr, b.name_tr) &&
                  a.cr == b.cr && a.cg == b.cg && a.cb == b.cb && a.br == b.br && a.bg == b.bg && a.bb == b.bb,
                  "theme %zu differs", i);
    }
    CHECK_EQ(dots::NUM_SIZES, original::NUM_SIZES);
    CHECK(std::equal(dots::SIZES, dots::SIZES + dots::NUM_SIZES, original::SIZES));
    CHECK_EQ(dots::NUM_SENSITIVITIES, original::NUM_SENSITIVITIES);
    CHECK(std::equal(dots::SENSITIVITIES, dots::SENSITIVITIES + dots::NUM_SENSITIVITIES, original::SENSITIVITIES));
    CHECK_EQ(dots::NUM_SMOOTHINGS, original::NUM_SMOOTHINGS);
    CHECK(std::equal(dots::SMOOTHINGS, dots::SMOOTHINGS + dots::NUM_SMOOTHINGS, original::SMOOTHINGS));
    CHECK_EQ(dots::NUM_DEADZONES, original::NUM_DEADZONES);
    CHECK(std::equal(dots::DEADZONES, dots::DEADZONES + dots::NUM_DEADZONES, original::DEADZONES));

    // The config ranges sanitize() enforces must cover every table index.
    CHECK_EQ(dots::NUM_THEMES, 12u);  // visual.color 0..11
    CHECK_EQ(dots::NUM_SIZES, 10u);   // visual.line_thickness 0..9
}

void test_anchor_counts() {
    // Expected counts of the original implementation, per style (rows) and
    // density (columns).
    const size_t expected[6][4] = {
        { 8, 16, 24, 32},  // 0 Perimeter: 2n + 2(n-2)
        {12, 24, 40, 60},  // 1 Full Grid: cols x rows
        { 8, 14, 20, 26},  // 2 Sides: 2n
        { 8, 16, 24, 32},  // 3 Corners
        { 8, 14, 20, 26},  // 4 Top/Bottom: 2n
        {24, 28, 32, 36},  // 5 Surround: 20 + inner
    };

    size_t largest = 0;
    for (int style = 0; style < 6; ++style)
        for (int density = 0; density < 4; ++density) {
            dots::Point pts[dots::MAX_ANCHORS + 8];
            const size_t n = dots::get_anchors(style, density, pts, dots::MAX_ANCHORS);
            const size_t n_orig = original::get_anchors(style, density).size();
            CHECK_ALL(n == expected[style][density], "style %d density %d: %zu anchors, expected %zu",
                      style, density, n, expected[style][density]);
            CHECK_ALL(n_orig == expected[style][density], "oracle style %d density %d: %zu", style, density, n_orig);
            CHECK_ALL(n <= dots::MAX_ANCHORS, "style %d density %d exceeds MAX_ANCHORS", style, density);
            largest = std::max(largest, n_orig);
        }
    CHECK(largest <= dots::MAX_ANCHORS);
}

void test_anchor_positions() {
    // Includes out-of-range styles (original: default -> perimeter) and
    // densities (original: clamped to 0..3).
    for (int style = -2; style <= 8; ++style)
        for (int density = -3; density <= 6; ++density) {
            dots::Point pts[dots::MAX_ANCHORS];
            const size_t n = dots::get_anchors(style, density, pts, dots::MAX_ANCHORS);
            const auto orig = original::get_anchors(style, density);
            CHECK_ALL(n == orig.size(), "style %d density %d: count %zu vs %zu", style, density, n, orig.size());
            bool same = n == orig.size();
            size_t bad = 0;
            for (size_t i = 0; same && i < n; ++i)
                if (pts[i].x != orig[i].first || pts[i].y != orig[i].second) { same = false; bad = i; }
            CHECK_ALL(same, "style %d density %d: anchor %zu (%g,%g) vs (%g,%g)", style, density, bad,
                      (double)pts[bad].x, (double)pts[bad].y,
                      bad < orig.size() ? (double)orig[bad].first : 0.0,
                      bad < orig.size() ? (double)orig[bad].second : 0.0);
        }
}

void test_anchor_truncation() {
    // Writes at most `max` points and never past them.
    dots::Point pts[dots::MAX_ANCHORS + 1];
    const dots::Point sentinel{-12345.0f, -54321.0f};
    for (auto& p : pts) p = sentinel;
    const size_t n = dots::get_anchors(1, 3, pts, 10);  // 60 available
    CHECK_EQ(n, 10u);
    const auto orig = original::get_anchors(1, 3);
    bool prefix = true;
    for (size_t i = 0; i < 10; ++i)
        if (pts[i].x != orig[i].first || pts[i].y != orig[i].second) prefix = false;
    CHECK(prefix);
    CHECK(pts[10].x == sentinel.x && pts[10].y == sentinel.y);

    CHECK_EQ(dots::get_anchors(0, 0, pts, 0), 0u);
    CHECK(pts[0].x == orig[0].first);  // untouched by the max=0 call
}

void test_compute_offset() {
    const float deadzones[] = {0.0f, 0.5f, 2.0f, 5.0f};
    const float sens_roll[] = {0.2f, 1.0f, 1.5f, 3.0f, 6.0f};
    const float sens_pitch[] = {0.2f, 1.0f, 6.0f};
    const float offsets[][2] = {{0.0f, 0.0f}, {5.25f, -3.5f}, {-40.0f, 12.0f}};

    size_t cases = 0;
    bool ok = true;
    char ctx[200] = "";
    for (float dz : deadzones)
        for (int inv = 0; inv < 4; ++inv)
            for (float sr : sens_roll)
                for (float sp : sens_pitch)
                    for (int ff = 0; ff < 2; ++ff)
                        for (const auto& off : offsets) {
                            kinestop::Config cfg;
                            cfg.filter.deadzone_deg = dz;
                            cfg.calibration.invert_pitch = inv & 1;
                            cfg.calibration.invert_roll = (inv >> 1) & 1;
                            cfg.calibration.sensitivity_roll = sr;
                            cfg.calibration.sensitivity_pitch = sp;
                            cfg.calibration.pitch_offset = off[0];
                            cfg.calibration.roll_offset = off[1];
                            cfg.visual.free_floating = ff;

                            for (float p = -90.0f; p <= 90.0f; p += 3.75f)
                                for (float r = -90.0f; r <= 90.0f; r += 3.75f) {
                                    kinestop::Orientation o;
                                    o.pitch_deg = p;
                                    o.roll_deg = r;
                                    o.yaw_deg = 17.0f;
                                    const dots::Point got = dots::compute_offset(o, cfg);
                                    const auto want = original::motion_offset(o, cfg);
                                    ++cases;
                                    if (ok && (got.x != want.first || got.y != want.second)) {
                                        ok = false;
                                        std::snprintf(ctx, sizeof(ctx),
                                                      "p=%g r=%g dz=%g inv=%d sr=%g sp=%g ff=%d: (%g,%g) vs (%g,%g)",
                                                      (double)p, (double)r, (double)dz, inv, (double)sr, (double)sp, ff,
                                                      (double)got.x, (double)got.y, (double)want.first, (double)want.second);
                                    }
                                }
                        }
    CHECK_ALL(ok, "%s", ctx);
    CHECK(cases > 100000);

    // Spot checks with hand-computed values (defaults: sens 1, dz 0.5, free floating).
    kinestop::Config cfg;
    kinestop::Orientation o;
    o.pitch_deg = 10.0f;
    o.roll_deg = -20.0f;
    dots::Point d = dots::compute_offset(o, cfg);
    CHECK_NEAR(d.x, 26.0f, 1e-4);   // -(-20 * 1.3)
    CHECK_NEAR(d.y, 11.0f, 1e-4);   // 10 * 1.1
    o.pitch_deg = 0.4f;             // inside the 0.5 deadzone
    o.roll_deg = 100.0f;            // clamped to max_offset 36
    d = dots::compute_offset(o, cfg);
    CHECK_EQ(d.y, 0.0f);
    CHECK_NEAR(d.x, -36.0f, 1e-4);
}

void test_dot_radius() {
    for (int idx = -3; idx < 14; ++idx) {
        kinestop::Config cfg;
        cfg.visual.line_thickness = idx;
        CHECK_ALL(dots::dot_radius(cfg) == original::dot_radius(cfg), "line_thickness %d", idx);
    }
}

void test_offset_sensitivity() {
    // Default boost: 1.0x + 4 steps = 4.0x (0.2 0.5 0.8 1.0 1.5 2.0 3.0 4.0 5.0 6.0).
    CHECK_EQ(dots::offset_sensitivity(1.0f, 4), 4.0f);
    CHECK_EQ(dots::offset_sensitivity(1.0f, 0), 1.0f);
    CHECK_EQ(dots::offset_sensitivity(0.2f, 1), 0.5f);
    CHECK_EQ(dots::offset_sensitivity(5.0f, 4), 6.0f);   // clamped at the top
    CHECK_EQ(dots::offset_sensitivity(6.0f, 9), 6.0f);
    CHECK_EQ(dots::offset_sensitivity(1.02f, 1), 1.5f);  // off-table value snaps to the nearest entry
    CHECK_EQ(dots::offset_sensitivity(1.0f, -9), 0.2f);  // clamped at the bottom
}

} // namespace

int main() {
    test_tables();
    test_anchor_counts();
    test_anchor_positions();
    test_anchor_truncation();
    test_compute_offset();
    test_dot_radius();
    test_offset_sensitivity();
    return check::summary("dots");
}
