// Motion-cue dot layout: option tables, anchor patterns and the per-frame
// displacement math.
//
// Original Kinestop by SertAy (https://github.com/sertay1/Kinestop). The
// tables, get_anchors() and compute_offset() are ported unchanged (same
// values, same positions, same math) from the original overlay's main.cpp;
// moving them here for the sysmodule/overlay split is a modification of the
// original project. Anchors are written into a caller-provided array instead
// of a std::vector so the sysmodule never touches the heap per frame.

#include "dots.hpp"

#include <cmath>

namespace kinestop::dots {

const Theme THEMES[] = {
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
const size_t NUM_THEMES = sizeof(THEMES) / sizeof(THEMES[0]);

const int SIZES[] = {1, 2, 3, 4, 5, 6, 7, 8, 10, 12};
const size_t NUM_SIZES = sizeof(SIZES) / sizeof(SIZES[0]);

const float SENSITIVITIES[] = {0.2f, 0.5f, 0.8f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f};
const size_t NUM_SENSITIVITIES = sizeof(SENSITIVITIES) / sizeof(SENSITIVITIES[0]);

const float SMOOTHINGS[] = {0.50f, 0.75f, 0.85f, 0.92f, 0.97f};
const size_t NUM_SMOOTHINGS = sizeof(SMOOTHINGS) / sizeof(SMOOTHINGS[0]);

const float DEADZONES[] = {0.0f, 0.2f, 0.5f, 1.0f, 1.5f, 2.0f, 3.0f, 5.0f};
const size_t NUM_DEADZONES = sizeof(DEADZONES) / sizeof(DEADZONES[0]);

namespace {

// Bounded appender standing in for the original std::vector::push_back.
struct AnchorWriter {
    Point* out;
    size_t max;
    size_t count = 0;

    void push(float x, float y) {
        if (count < max) out[count] = Point{x, y};
        ++count;  // counted even when dropped so overflow is detectable in tests
    }
    size_t written() const { return count < max ? count : max; }
};

int clamp_int(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

float clamp_float(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

} // namespace

// Generates dot anchors based on Placement Style and Density Level with 0 overlapping
size_t get_anchors(int style, int density, Point* out, size_t max) {
    AnchorWriter anchors{out, max};
    int d = clamp_int(density, 0, 3); // 0: Low, 1: Medium, 2: High, 3: Ultra

    switch (style) {
        default:
        case 0: { // 4-Border Perimeter
            const int counts[] = {3, 5, 7, 9};
            int n_col = counts[d];
            int n_row = counts[d];

            for (int i = 0; i < n_col; ++i) {
                float y = 90.0f + (float)i * (540.0f / (float)(n_col - 1));
                anchors.push(32.0f, y);
                anchors.push(1248.0f, y);
            }
            for (int i = 1; i < n_row - 1; ++i) {
                float x = 32.0f + (float)i * (1216.0f / (float)(n_row - 1));
                anchors.push(x, 32.0f);
                anchors.push(x, 688.0f);
            }
            break;
        }

        case 1: { // Full Screen Grid (Covering middle & borders)
            struct GridDim { int cols; int rows; };
            const GridDim dims[] = {{4, 3}, {6, 4}, {8, 5}, {10, 6}};
            GridDim g = dims[d];

            float start_x = 60.0f, end_x = 1220.0f;
            float start_y = 50.0f, end_y = 670.0f;

            for (int r = 0; r < g.rows; ++r) {
                float y = start_y + (float)r * ((end_y - start_y) / (float)(g.rows - 1));
                for (int c = 0; c < g.cols; ++c) {
                    float x = start_x + (float)c * ((end_x - start_x) / (float)(g.cols - 1));
                    anchors.push(x, y);
                }
            }
            break;
        }

        case 2: { // Side Columns
            const int counts[] = {4, 7, 10, 13};
            int n = counts[d];
            for (int i = 0; i < n; ++i) {
                float y = 70.0f + (float)i * (580.0f / (float)(n - 1));
                anchors.push(32.0f, y);
                anchors.push(1248.0f, y);
            }
            break;
        }

        case 3: { // Clean 4 Corners (Zero Overlap Guaranteed)
            if (d == 0) { // Low: 2 dots per corner
                anchors.push(35.0f, 35.0f);   anchors.push(115.0f, 35.0f);
                anchors.push(1245.0f, 35.0f); anchors.push(1165.0f, 35.0f);
                anchors.push(35.0f, 685.0f);  anchors.push(115.0f, 685.0f);
                anchors.push(1245.0f, 685.0f);anchors.push(1165.0f, 685.0f);
            } else if (d == 1) { // Medium: 4 dots per corner (2x2)
                anchors.push(35.0f, 35.0f);   anchors.push(115.0f, 35.0f);
                anchors.push(35.0f, 115.0f);  anchors.push(115.0f, 115.0f);

                anchors.push(1245.0f, 35.0f); anchors.push(1165.0f, 35.0f);
                anchors.push(1245.0f, 115.0f);anchors.push(1165.0f, 115.0f);

                anchors.push(35.0f, 685.0f);  anchors.push(115.0f, 685.0f);
                anchors.push(35.0f, 605.0f);  anchors.push(115.0f, 605.0f);

                anchors.push(1245.0f, 685.0f);anchors.push(1165.0f, 685.0f);
                anchors.push(1245.0f, 605.0f);anchors.push(1165.0f, 605.0f);
            } else if (d == 2) { // High: 6 dots per corner (L-bracket)
                anchors.push(35.0f, 35.0f);   anchors.push(115.0f, 35.0f);  anchors.push(195.0f, 35.0f);
                anchors.push(35.0f, 115.0f);  anchors.push(35.0f, 195.0f);  anchors.push(115.0f, 115.0f);

                anchors.push(1245.0f, 35.0f); anchors.push(1165.0f, 35.0f); anchors.push(1085.0f, 35.0f);
                anchors.push(1245.0f, 115.0f);anchors.push(1245.0f, 195.0f);anchors.push(1165.0f, 115.0f);

                anchors.push(35.0f, 685.0f);  anchors.push(115.0f, 685.0f);  anchors.push(195.0f, 685.0f);
                anchors.push(35.0f, 605.0f);  anchors.push(35.0f, 525.0f);  anchors.push(115.0f, 605.0f);

                anchors.push(1245.0f, 685.0f);anchors.push(1165.0f, 685.0f); anchors.push(1085.0f, 685.0f);
                anchors.push(1245.0f, 605.0f);anchors.push(1245.0f, 525.0f);anchors.push(1165.0f, 605.0f);
            } else { // Ultra: 8 dots per corner (3x3 diagonal fan)
                anchors.push(35.0f, 35.0f);   anchors.push(115.0f, 35.0f);  anchors.push(195.0f, 35.0f);  anchors.push(275.0f, 35.0f);
                anchors.push(35.0f, 115.0f);  anchors.push(35.0f, 195.0f);  anchors.push(35.0f, 275.0f);  anchors.push(115.0f, 115.0f);

                anchors.push(1245.0f, 35.0f); anchors.push(1165.0f, 35.0f); anchors.push(1085.0f, 35.0f); anchors.push(1005.0f, 35.0f);
                anchors.push(1245.0f, 115.0f);anchors.push(1245.0f, 195.0f);anchors.push(1245.0f, 275.0f);anchors.push(1165.0f, 115.0f);

                anchors.push(35.0f, 685.0f);  anchors.push(115.0f, 685.0f);  anchors.push(195.0f, 685.0f);  anchors.push(275.0f, 685.0f);
                anchors.push(35.0f, 605.0f);  anchors.push(35.0f, 525.0f);  anchors.push(35.0f, 445.0f);  anchors.push(115.0f, 605.0f);

                anchors.push(1245.0f, 685.0f);anchors.push(1165.0f, 685.0f); anchors.push(1085.0f, 685.0f); anchors.push(1005.0f, 685.0f);
                anchors.push(1245.0f, 605.0f);anchors.push(1245.0f, 525.0f);anchors.push(1245.0f, 445.0f);anchors.push(1165.0f, 605.0f);
            }
            break;
        }

        case 4: { // Top & Bottom Rows
            const int counts[] = {4, 7, 10, 13};
            int n = counts[d];
            for (int i = 0; i < n; ++i) {
                float x = 120.0f + (float)i * (1040.0f / (float)(n - 1));
                anchors.push(x, 32.0f);
                anchors.push(x, 688.0f);
            }
            break;
        }

        case 5: { // Surround Field
            for (int i = 0; i < 5; ++i) {
                float y = 100.0f + (float)i * 130.0f;
                anchors.push(32.0f, y);
                anchors.push(1248.0f, y);
                float x = 240.0f + (float)i * 200.0f;
                anchors.push(x, 32.0f);
                anchors.push(x, 688.0f);
            }
            const int inner_counts[] = {4, 8, 12, 16};
            int in_n = inner_counts[d];
            for (int i = 0; i < in_n; ++i) {
                float angle = (float)i * (2.0f * PI / (float)in_n);
                anchors.push(640.0f + 480.0f * std::cos(angle), 360.0f + 240.0f * std::sin(angle));
            }
            break;
        }
    }
    return anchors.written();
}

// Port of the displacement half of the original draw_motion_dots(). The other
// half - clamping each final dot position to [10,1270]x[10,710] when
// !free_floating - stays with the renderer side (the sysmodule's
// build_frame() in sysmodule/source/main.cpp), because it applies per dot
// (anchor + offset) rather than to the shared offset.
Point compute_offset(const Orientation& raw, const Config& cfg) {
    // Handheld calibration delta calculation
    float pitch = raw.pitch_deg - cfg.calibration.pitch_offset;
    float roll  = raw.roll_deg  - cfg.calibration.roll_offset;

    // Apply Deadzone
    if (std::abs(pitch) < cfg.filter.deadzone_deg) pitch = 0.0f;
    if (std::abs(roll)  < cfg.filter.deadzone_deg) roll  = 0.0f;

    if (cfg.calibration.invert_pitch) pitch = -pitch;
    if (cfg.calibration.invert_roll)  roll  = -roll;

    // Dynamic floating displacement range
    float sens_multiplier = cfg.calibration.sensitivity_roll;
    float max_offset = cfg.visual.free_floating ? (36.0f * std::max(1.0f, sens_multiplier * 0.7f)) : 36.0f;
    float sens_x = 1.3f * cfg.calibration.sensitivity_roll;
    float sens_y = 1.1f * cfg.calibration.sensitivity_pitch;

    float target_dx = -clamp_float(roll * sens_x, -max_offset, max_offset);
    float target_dy =  clamp_float(pitch * sens_y, -max_offset, max_offset);

    return Point{target_dx, target_dy};
}

float dot_radius(const Config& cfg) {
    int size_idx = clamp_int(cfg.visual.line_thickness, 0, (int)NUM_SIZES - 1);
    return (float)SIZES[size_idx] * 0.9f;
}

} // namespace kinestop::dots
