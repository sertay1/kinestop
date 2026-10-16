#pragma once

// Motion-cue dot layout: the option tables for the settings menu and the
// anchor/offset math the HUD draws from.
// Ported from the original overlay's draw_motion_dots()/get_anchors() by SertAy.

#include <cstddef>
#include <cstdint>
#include "config.hpp"
#include "math_types.hpp"

namespace kinestop::dots {

// All layout math is in a 1280x720 logical screen. The HUD scales it down to
// its 640x360 framebuffer; VI scales the framebuffer back up to full screen.
constexpr float LOGICAL_WIDTH  = 1280.0f;
constexpr float LOGICAL_HEIGHT = 720.0f;

constexpr size_t MAX_ANCHORS = 64;  // largest pattern is Full Grid Ultra (10x6 = 60)

struct Theme {
    const char* name_en;
    const char* name_tr;
    uint8_t cr, cg, cb;  // core color
    uint8_t br, bg, bb;  // border contrast color
};

extern const Theme  THEMES[];
extern const size_t NUM_THEMES;

extern const int    SIZES[];          // dot size in logical px, indexed by visual.line_thickness
extern const size_t NUM_SIZES;
extern const float  SENSITIVITIES[];
extern const size_t NUM_SENSITIVITIES;
extern const float  SMOOTHINGS[];     // filter.alpha presets
extern const size_t NUM_SMOOTHINGS;
extern const float  DEADZONES[];
extern const size_t NUM_DEADZONES;

struct Point { float x, y; };

// Anchor positions for a placement style/density, in logical coordinates.
// Writes at most `max` points and returns how many were written.
size_t get_anchors(int style, int density, Point* out, size_t max);

// Per-frame displacement applied to every anchor, from the raw filtered
// orientation: subtracts calibration offsets, applies deadzone, inversion,
// sensitivity and the free-floating range. Logical px.
Point compute_offset(const Orientation& raw, const Config& cfg);

// `sensitivity` moved `steps` places along SENSITIVITIES (from the nearest
// table entry), clamped to the table: the console-sensor boost.
float offset_sensitivity(float sensitivity, int steps);

// Dot core radius in logical px for the configured size index.
float dot_radius(const Config& cfg);

} // namespace kinestop::dots
