// Kinestop HUD - logical (1280x720) px to framebuffer px for the dot frame:
// dot centres, the quantized shared offset and the dot core/ring sizes, for
// a framebuffer of any width (the HUD uses 640x360).
//
// Original Kinestop by SertAy (https://github.com/sertay1/Kinestop). This
// file is part of a modified version of the original project.
//
// Header-only and free of libnx so the host tests (tests/test_scale.cpp)
// can check it; hud_frame.hpp uses it.

#pragma once

#include <cmath>
#include <cstdint>

namespace kinestop::frame_scale {

constexpr float LOGICAL_WIDTH = 1280.0f;  // dots::LOGICAL_WIDTH

// Shared dot offset quantization: steps per framebuffer pixel.
constexpr float OFFSET_QUANTUM = 4.0f;

// Smallest dot core and ring, in framebuffer px. At fb_scale 0.5 the
// smallest size already maps to 0.5 px; at 0.25 (a 320x180 framebuffer) the two
// smallest sizes would otherwise shrink to sub-half-pixel smudges.
constexpr float MIN_CORE_RADIUS_FB = 0.5f;
constexpr float MIN_RING_WIDTH_FB = 1.0f;

// Logical px -> framebuffer px for the current layer: 0.5 for a 640x360
// framebuffer, 0.25 for 320x180.
inline float fb_scale_for(uint32_t fb_width) {
    return (float)fb_width / LOGICAL_WIDTH;
}

// Logical offset -> quantized framebuffer steps, and back.
inline int32_t quantize_offset(float logical, float fb_scale) {
    return (int32_t)std::lround(logical * fb_scale * OFFSET_QUANTUM);
}
inline float dequantize_offset(int32_t q, float fb_scale) {
    return (float)q / (OFFSET_QUANTUM * fb_scale);
}

// Logical pixel x covers [x, x+1); its centre maps to (x + 0.5) * scale.
inline float center_fb(float logical, float fb_scale) {
    return (logical + 0.5f) * fb_scale;
}

// The original's draw_filled_circle() fills every pixel within
// ceil(radius), so the visible core is ceil(SIZES[i] * 0.9) logical px:
// 1, 2, 3, ... 9, 11 - ten distinct sizes, the smallest 0.5 fb px at
// fb_scale 0.5, never below MIN_CORE_RADIUS_FB.
inline float core_radius_fb(float logical_radius, float fb_scale) {
    const float core = std::ceil(logical_radius) * fb_scale;
    return core < MIN_CORE_RADIUS_FB ? MIN_CORE_RADIUS_FB : core;
}

// The original outlines the core with a ~1-logical-px circle at
// radius + 2, leaving a ~1 px gap. At half resolution that ring and gap
// would be 0.5 fb px each and blur into a faint smear, so the ring here is
// a solid band directly around the core: 2 logical px (1 fb px at fb_scale
// 0.5), ending at about the same outer radius as the original's, and never
// thinner than MIN_RING_WIDTH_FB.
inline float ring_width_fb(float fb_scale) {
    const float ring = 2.0f * fb_scale;
    return ring < MIN_RING_WIDTH_FB ? MIN_RING_WIDTH_FB : ring;
}

} // namespace kinestop::frame_scale
