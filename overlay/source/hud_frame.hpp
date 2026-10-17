// Kinestop HUD - launch-mode argument and the dot frame (anchors, sizes,
// colors and per-frame dot centres) that main.cpp's HUD draws into libtesla's
// framebuffer.
//
// Original Kinestop by SertAy (https://github.com/sertay1/Kinestop). This
// file is part of a modified version of the original project.
//
// Header-only and free of libnx so the host tests (tests/test_hud.cpp) can
// check it.

#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>

#include "config.hpp"
#include "dots.hpp"
#include "frame_scale.hpp"
#include "raster.hpp"

namespace kinestop::hud {

constexpr const char* HUD_ARG = "-hud";  // launch argument of the HUD mode

// True when kinestop.ovl was launched in HUD mode (argv[0] is the path).
inline bool is_hud_launch(int argc, char** argv) {
    for (int i = 1; i < argc; ++i)
        if (argv[i] != nullptr && std::strcmp(argv[i], HUD_ARG) == 0)
            return true;
    return false;
}

// Name of the HUD mode in the menu language: the label of the settings
// menu's launch item and of the launch mode in Ultrahand's overlays.ini.
inline const char* mode_label(int language) {
    return language == 1 ? "Hareket Noktaları" : "Motion Dots";
}

// The HUD's way back to the settings: armed when the menu combo is pressed,
// it fires once every button of the combo has been released, so the
// settings panel never starts with combo buttons still held. (libtesla
// reads a ZL/ZR/L/R first seen held on its own and let go within 400 ms as
// a page/jump click: a lone ZR left over from ZL+ZR+DDOWN moved the cursor
// a page down, onto Dot Opacity.)
struct ComboRelease {
    bool armed = false;

    // Once per HUD frame; true on the frame the settings should open.
    bool update(uint64_t keys_down, uint64_t keys_held, uint64_t combo) {
        if (combo == 0)
            return false;
        if (!armed) {
            armed = (keys_held & combo) == combo && (keys_down & combo) != 0;
            return false;
        }
        return (keys_held & combo) == 0;
    }
};

// Anchors, sizes and colors are fixed for a HUD launch (the settings live in
// the other mode); only the shared offset changes per frame.
struct DotFrame {
    float scale;  // logical (1280x720) px -> framebuffer px
    size_t count;
    dots::Point anchors[dots::MAX_ANCHORS];
    raster::DotStyle style;
};

// Port of the 2.0.0 test builds' build_frame(), itself from the original
// draw_motion_dots() colors and sizes.
inline void build_dot_frame(const Config& cfg, uint32_t fb_width, DotFrame& frame) {
    namespace fs = frame_scale;
    frame.scale = fs::fb_scale_for(fb_width);
    frame.count = dots::get_anchors(cfg.visual.style, cfg.visual.density, frame.anchors, dots::MAX_ANCHORS);

    const int theme_idx = std::clamp(cfg.visual.color, 0, (int)dots::NUM_THEMES - 1);
    const dots::Theme& theme = dots::THEMES[theme_idx];

    const int alpha = std::clamp((int)(cfg.visual.opacity * 255.0f), 25, 255);
    const int border_alpha = std::min(alpha + 50, 255);

    // Core: ceil of the original's radius, never below 0.5 fb px. Ring: a
    // solid 2-logical-px band, never below 1 fb px (see frame_scale.hpp).
    frame.style.core_radius = fs::core_radius_fb(dots::dot_radius(cfg), frame.scale);
    frame.style.ring_width  = fs::ring_width_fb(frame.scale);
    frame.style.core   = {theme.cr, theme.cg, theme.cb, (uint8_t)alpha};
    frame.style.border = {theme.br, theme.bg, theme.bb, (uint8_t)border_alpha};
}

// The shared offset (logical px) directly passed for smooth subpixel interpolation.
inline dots::Point snap_offset(dots::Point offset, float /*scale*/) {
    return offset;
}

// Centre of dot i in framebuffer px for a snapped offset. Not free floating,
// every dot stays on screen (the per-dot half of the original
// draw_motion_dots()).
inline dots::Point dot_center(const DotFrame& frame, size_t i, dots::Point snapped, bool free_floating) {
    float px = frame.anchors[i].x + snapped.x;
    float py = frame.anchors[i].y + snapped.y;
    if (!free_floating) {
        px = std::clamp(px, 10.0f, dots::LOGICAL_WIDTH - 10.0f);
        py = std::clamp(py, 10.0f, dots::LOGICAL_HEIGHT - 10.0f);
    }
    return {frame_scale::center_fb(px, frame.scale), frame_scale::center_fb(py, frame.scale)};
}

// Draws every dot of the frame into a cleared surface.
inline void draw_dots(const raster::Surface& surface, const DotFrame& frame, dots::Point offset, bool free_floating) {
    const dots::Point snapped = snap_offset(offset, frame.scale);
    for (size_t i = 0; i < frame.count; ++i) {
        const dots::Point c = dot_center(frame, i, snapped, free_floating);
        raster::draw_dot(surface, c.x, c.y, frame.style);
    }
}

} // namespace kinestop::hud
