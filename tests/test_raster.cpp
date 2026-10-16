// Kinestop host tests - dot rasterizer (sysmodule/source/raster.hpp).
//
// Part of the sysmodule/overlay split of the original Kinestop by SertAy
// (https://github.com/sertay1/Kinestop).
//
// The renderer clears only the rectangles it drew into a buffer slot the last
// time, so draw_dot() must never touch a pixel outside the rectangle it
// returns, never touch the padding rows, and clear_rect() must restore a
// fully transparent buffer.

#include <cmath>
#include <cstdint>
#include <vector>

#include "check.hpp"
#include "raster.hpp"
#include "swizzle.hpp"

namespace raster = kinestop::raster;
namespace swz = kinestop::swizzle;

namespace {

constexpr uint32_t W = 640, H = 360;
// KINESTOP_DEBUG_SCALED_SMALL framebuffer.
constexpr uint32_t SW = 320, SH = 180;

struct Buffer {
    uint32_t w, h;
    std::vector<uint16_t> px;
    raster::Surface surf;
    Buffer(uint32_t w_ = W, uint32_t h_ = H)
        : w(w_), h(h_), px(swz::buffer_size_16bpp(w_, h_) / 2, 0), surf{px.data(), w_, h_} {}
    bool all_zero() const {
        for (uint16_t v : px) if (v) return false;
        return true;
    }
    uint16_t at(uint32_t x, uint32_t y) const { return px[swz::pixel_offset_16bpp(x, y, w)]; }
};

raster::DotStyle style(float radius, uint8_t alpha) {
    raster::DotStyle s;
    s.core_radius = radius;
    s.ring_width = 1.0f;
    s.core = {175, 235, 255, alpha};    // Ice Blue core
    s.border = {10, 25, 50, (uint8_t)(alpha + 50 > 255 ? 255 : alpha + 50)};
    return s;
}

bool inside(const raster::Rect& r, uint32_t x, uint32_t y) {
    return (int)x >= r.x0 && (int)x < r.x1 && (int)y >= r.y0 && (int)y < r.y1;
}

void test_bounds_and_clear() {
    // Centres inside, on and beyond every edge; smallest and largest dot sizes.
    const float xs[] = {-20.0f, -3.0f, 0.0f, 0.4f, 5.0f, 320.25f, 634.9f, 639.5f, 640.0f, 650.0f};
    const float ys[] = {-20.0f, -3.0f, 0.0f, 0.6f, 5.0f, 180.75f, 354.1f, 359.5f, 360.0f, 380.0f};
    const float radii[] = {1.0f, 2.7f, 5.4f};  // SIZES*0.9*0.5 range

    bool contained = true, padding_clean = true, cleared = true;
    for (float r : radii)
        for (float cx : xs)
            for (float cy : ys) {
                Buffer b;
                const raster::Rect rect = raster::draw_dot(b.surf, cx, cy, style(r, 204));
                // Every non-zero visible pixel lies in the returned rect.
                for (uint32_t y = 0; y < H; ++y)
                    for (uint32_t x = 0; x < W; ++x)
                        if (b.at(x, y) && !inside(rect, x, y)) contained = false;
                // Padding rows (360..383) are never written.
                for (uint32_t y = H; y < swz::aligned_height(H); ++y)
                    for (uint32_t x = 0; x < W; ++x)
                        if (b.at(x, y)) padding_clean = false;
                raster::clear_rect(b.surf, rect);
                if (!b.all_zero()) cleared = false;
            }
    CHECK(contained);
    CHECK(padding_clean);
    CHECK(cleared);
}

// Brute-force single-dot render over the whole surface (same coverage model
// as draw_dot(), but with no bounding box). draw_dot()'s rectangle must not
// clip anything this produces.
void reference_dot(Buffer& b, float cx, float cy, const raster::DotStyle& st) {
    const float rc = st.core_radius, ro = st.core_radius + st.ring_width;
    for (uint32_t y = 0; y < H; ++y)
        for (uint32_t x = 0; x < W; ++x) {
            const float dx = ((float)x + 0.5f) - cx, dy = ((float)y + 0.5f) - cy;
            const float d = std::sqrt(dx * dx + dy * dy);
            const float cov_core = raster::clamp01(rc + 0.5f - d);
            const float cov_all = raster::clamp01(ro + 0.5f - d);
            if (cov_all <= 0.0f) continue;
            const float wc = st.core.a / 255.0f * cov_core;
            const float wr = st.border.a / 255.0f * (cov_all - cov_core);
            b.px[swz::pixel_offset_16bpp(x, y, W)] = raster::pack_premultiplied(
                st.core.r / 255.0f * wc + st.border.r / 255.0f * wr,
                st.core.g / 255.0f * wc + st.border.g / 255.0f * wr,
                st.core.b / 255.0f * wc + st.border.b / 255.0f * wr, wc + wr);
        }
}

void test_not_clipped() {
    const float centers[][2] = {{100.0f, 100.0f}, {100.5f, 100.5f}, {100.99f, 100.01f}, {0.2f, 359.8f}, {639.9f, 0.1f}, {-2.0f, 50.0f}};
    const float radii[] = {1.0f, 1.35f, 2.7f, 3.6f, 5.4f};
    bool ok = true;
    for (const auto& c : centers)
        for (float r : radii) {
            Buffer a, b;
            const raster::DotStyle st = style(r, 204);
            raster::draw_dot(a.surf, c[0], c[1], st);
            reference_dot(b, c[0], c[1], st);
            if (a.px != b.px) ok = false;
        }
    CHECK(ok);
}

// The 320x180 surface: same containment / padding / clear guarantees, with
// the radii the sysmodule uses at fb_scale 0.25 (min core 0.5 px, ring 1 px).
void test_small_surface() {
    const float xs[] = {-10.0f, 0.0f, 0.3f, 8.125f, 160.0f, 314.6f, 319.5f, 320.0f, 330.0f};
    const float ys[] = {-10.0f, 0.0f, 0.7f, 8.0f, 90.25f, 175.0f, 179.5f, 180.0f, 190.0f};
    const float radii[] = {0.5f, 0.75f, 1.5f, 2.75f};

    bool contained = true, padding_clean = true, cleared = true, drew = false;
    for (float r : radii)
        for (float cx : xs)
            for (float cy : ys) {
                Buffer b(SW, SH);
                const raster::Rect rect = raster::draw_dot(b.surf, cx, cy, style(r, 204));
                if (!b.all_zero()) drew = true;
                for (uint32_t y = 0; y < SH; ++y)
                    for (uint32_t x = 0; x < SW; ++x)
                        if (b.at(x, y) && !inside(rect, x, y)) contained = false;
                for (uint32_t y = SH; y < swz::aligned_height(SH); ++y)
                    for (uint32_t x = 0; x < SW; ++x)
                        if (b.at(x, y)) padding_clean = false;
                raster::clear_rect(b.surf, rect);
                if (!b.all_zero()) cleared = false;
            }
    CHECK(contained);
    CHECK(padding_clean);
    CHECK(cleared);
    CHECK(drew);

    // A dot at the right edge lands in the last GOB column of a 320 px row.
    Buffer e(SW, SH);
    raster::draw_dot(e.surf, 318.5f, 100.5f, style(1.0f, 255));
    CHECK((e.at(318, 100) >> 12) == 15);
}

void test_pixels() {
    Buffer b;
    const raster::DotStyle st = style(3.0f, 255);
    const raster::Rect rect = raster::draw_dot(b.surf, 100.0f, 100.0f, st);
    CHECK(!rect.empty());

    // Centre: opaque core color, RGBA4 nibble order (r low ... a high).
    const uint16_t c = b.at(99, 99);
    CHECK_EQ((c >> 12) & 0xF, 15);
    CHECK_EQ((c >> 0) & 0xF, (175 * 15 + 127) / 255);
    CHECK_EQ((c >> 4) & 0xF, (235 * 15 + 127) / 255);
    CHECK_EQ((c >> 8) & 0xF, 15);

    // Far outside the dot: untouched.
    CHECK_EQ(b.at(120, 100), 0);

    // Premultiplied: no channel exceeds alpha anywhere.
    bool premul = true;
    for (uint32_t y = 0; y < H; ++y)
        for (uint32_t x = 0; x < W; ++x) {
            const uint16_t v = b.at(x, y);
            const int a = (v >> 12) & 0xF;
            if (((v >> 0) & 0xF) > a || ((v >> 4) & 0xF) > a || ((v >> 8) & 0xF) > a) premul = false;
        }
    CHECK(premul);

    // Overlapping dots: the union of the two rects clears everything.
    Buffer o;
    const raster::Rect r1 = raster::draw_dot(o.surf, 200.0f, 200.0f, style(5.4f, 128));
    const raster::Rect r2 = raster::draw_dot(o.surf, 204.0f, 202.0f, style(5.4f, 128));
    raster::clear_rect(o.surf, r1);
    raster::clear_rect(o.surf, r2);
    CHECK(o.all_zero());

    // Fully off-surface dots touch nothing and return an empty rect.
    Buffer e;
    const raster::Rect off = raster::draw_dot(e.surf, -50.0f, -50.0f, style(5.4f, 255));
    CHECK(off.empty());
    CHECK(e.all_zero());
}

} // namespace

int main() {
    test_bounds_and_clear();
    test_not_clipped();
    test_small_surface();
    test_pixels();
    return check::summary("raster");
}
