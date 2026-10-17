// Kinestop HUD - motion-cue dot rasterizer for an RGBA4444 block-linear
// framebuffer (libtesla's, written through Renderer::getCurrentFramebuffer()).
//
// Part of a modified version of the original Kinestop by SertAy
// (https://github.com/sertay1/Kinestop). The look (filled core in the theme's
// core color, ring in its border color) follows the original overlay's
// draw_motion_cue_dot(), drawn at half resolution with edge anti-aliasing.
//
// Header-only and host-compilable (stdint + cmath).
//
// Pixel format: libnx RGBA4 bit order (r = bits 0-3, g = 4-7, b = 8-11,
// a = 12-15), the same layout as tsl::Color. libtesla's setPixelBlendDst()
// over a cleared (all-zero) framebuffer stores rgb * a, i.e. premultiplied
// alpha, which is what the original Kinestop HUD produced (and what every
// other libtesla draw call produces over a cleared screen); we store
// premultiplied colors too and composite with premultiplied "over".

#pragma once

#include <stdint.h>
#include <cmath>

#include "swizzle.hpp"

namespace kinestop::raster {

struct Rgba8 {
    uint8_t r, g, b, a;  // straight (not premultiplied) color, 0..255
};

struct DotStyle {
    float core_radius;  // framebuffer px
    float ring_width;   // framebuffer px, drawn just outside the core
    Rgba8 core;
    Rgba8 border;
};

// Half-open pixel rectangle [x0, x1) x [y0, y1); empty when x0 >= x1 or y0 >= y1.
struct Rect {
    int16_t x0, y0, x1, y1;
    bool empty() const { return x0 >= x1 || y0 >= y1; }
};

struct Surface {
    uint16_t* pixels;  // start of one buffer slot
    uint32_t width;    // logical width (swizzle uses the 32-px-aligned row)
    uint32_t height;   // visible rows; padding rows are never written
};

inline int to_nibble(float v) {
    int n = (int)(v * 15.0f + 0.5f);
    return n < 0 ? 0 : (n > 15 ? 15 : n);
}

// Premultiplied 0..1 components -> RGBA4444 (libnx RGBA4 order).
inline uint16_t pack_premultiplied(float r, float g, float b, float a) {
    return (uint16_t)(to_nibble(r) | (to_nibble(g) << 4) | (to_nibble(b) << 8) | (to_nibble(a) << 12));
}

inline float clamp01(float v) {
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

// Draws one dot centred on (cx, cy) (framebuffer px, pixel centres at +0.5),
// clipped to the surface. Returns the rectangle it touched so the caller can
// clear exactly that area before the slot is reused.
inline Rect draw_dot(const Surface& s, float cx, float cy, const DotStyle& st) {
    const float rc = st.core_radius;
    const float ro = st.core_radius + st.ring_width;
    const float reach = ro + 1.0f;

    int x0 = (int)std::floor(cx - reach);
    int y0 = (int)std::floor(cy - reach);
    int x1 = (int)std::ceil(cx + reach) + 1;
    int y1 = (int)std::ceil(cy + reach) + 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int)s.width) x1 = (int)s.width;
    if (y1 > (int)s.height) y1 = (int)s.height;

    Rect rect{(int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1};
    if (rect.empty()) return Rect{0, 0, 0, 0};

    const float core_a = st.core.a / 255.0f;
    const float ring_a = st.border.a / 255.0f;
    const float core_r = st.core.r / 255.0f, core_g = st.core.g / 255.0f, core_b = st.core.b / 255.0f;
    const float ring_r = st.border.r / 255.0f, ring_g = st.border.g / 255.0f, ring_b = st.border.b / 255.0f;

    for (int y = y0; y < y1; ++y) {
        const float dy = ((float)y + 0.5f) - cy;
        for (int x = x0; x < x1; ++x) {
            const float dx = ((float)x + 0.5f) - cx;
            const float d = std::sqrt(dx * dx + dy * dy);

            // Approximate area coverage of the core disk and of the whole dot
            // (core + ring) by a 1-px linear ramp across each edge.
            const float cov_core = clamp01(rc + 0.5f - d);
            const float cov_all  = clamp01(ro + 0.5f - d);
            if (cov_all <= 0.0f) continue;
            const float cov_ring = cov_all - cov_core;

            // Core and ring cover disjoint areas of the pixel, so their
            // premultiplied contributions add.
            const float wc = core_a * cov_core;
            const float wr = ring_a * cov_ring;
            float sa = wc + wr;
            float sr = core_r * wc + ring_r * wr;
            float sg = core_g * wc + ring_g * wr;
            float sb = core_b * wc + ring_b * wr;

            uint16_t& px = s.pixels[swizzle::pixel_offset_16bpp((uint32_t)x, (uint32_t)y, s.width)];
            if (px != 0) {
                // Premultiplied source-over onto what is already there
                // (overlapping dots).
                const float inv = 1.0f - sa;
                sr += ((px >> 0) & 0xF) / 15.0f * inv;
                sg += ((px >> 4) & 0xF) / 15.0f * inv;
                sb += ((px >> 8) & 0xF) / 15.0f * inv;
                sa += ((px >> 12) & 0xF) / 15.0f * inv;
            }
            px = pack_premultiplied(sr, sg, sb, sa);
        }
    }
    return rect;
}

// Zeroes (makes fully transparent) a rectangle returned by draw_dot().
inline void clear_rect(const Surface& s, const Rect& r) {
    for (int y = r.y0; y < r.y1; ++y)
        for (int x = r.x0; x < r.x1; ++x)
            s.pixels[swizzle::pixel_offset_16bpp((uint32_t)x, (uint32_t)y, s.width)] = 0;
}

} // namespace kinestop::raster
