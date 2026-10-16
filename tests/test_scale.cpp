// Kinestop host tests - sysmodule/source/frame_scale.hpp: logical
// (1280x720) px to framebuffer px for each LayerMode framebuffer, i.e.
// fb_scale 0.5 (640x360: Full2x, Quarter1x) and 0.25 (320x180: Quarter2x,
// KINESTOP_DEBUG_SCALED_SMALL).
//
// Part of the sysmodule/overlay split of the original Kinestop by SertAy
// (https://github.com/sertay1/Kinestop).

#include <cmath>
#include <cstdint>

#include "check.hpp"
#include "config.hpp"
#include "dots.hpp"
#include "frame_scale.hpp"

namespace fs = kinestop::frame_scale;
namespace dots = kinestop::dots;

namespace {

struct Fb { uint32_t w, h; };
constexpr Fb FULL = {640, 360};   // Renderer::FB_WIDTH x FB_HEIGHT
constexpr Fb SMALL = {320, 180};  // Renderer::SMALL_FB_WIDTH x SMALL_FB_HEIGHT

void test_scale_factors() {
    static_assert(fs::LOGICAL_WIDTH == dots::LOGICAL_WIDTH, "logical width");
    CHECK_EQ(fs::fb_scale_for(FULL.w), 0.5f);
    CHECK_EQ(fs::fb_scale_for(SMALL.w), 0.25f);
    // Both framebuffers keep the 16:9 logical aspect, so one scale fits both axes.
    CHECK_EQ(dots::LOGICAL_HEIGHT * fs::fb_scale_for(FULL.w), (float)FULL.h);
    CHECK_EQ(dots::LOGICAL_HEIGHT * fs::fb_scale_for(SMALL.w), (float)SMALL.h);
}

// At 0.5 the sizes are exactly what test.2 drew (the formulas before
// frame_scale.hpp existed); at 0.25 they never drop below the minimums and
// stay ordered by the size setting.
void test_dot_sizes() {
    kinestop::Config cfg;
    float prev_small = 0.0f;
    for (int i = 0; i < (int)dots::NUM_SIZES; ++i) {
        cfg.visual.line_thickness = i;
        const float r = dots::dot_radius(cfg);

        const float old_core = std::ceil(r) * 0.5f;  // test.2: ceil(radius) * FB_SCALE
        CHECK_ALL(fs::core_radius_fb(r, 0.5f) == old_core, "size %d at 0.5", i);

        const float small = fs::core_radius_fb(r, 0.25f);
        CHECK_ALL(small >= fs::MIN_CORE_RADIUS_FB, "size %d at 0.25: %f", i, small);
        CHECK_ALL(small == std::fmax(std::ceil(r) * 0.25f, 0.5f), "size %d at 0.25: %f", i, small);
        CHECK_ALL(small >= prev_small, "size %d at 0.25 smaller than size %d", i, i - 1);
        // Half the 0.5 size wherever the minimum does not apply.
        if (old_core / 2.0f >= fs::MIN_CORE_RADIUS_FB)
            CHECK_ALL(small == old_core / 2.0f, "size %d: 0.25 is not half of 0.5", i);
        prev_small = small;
    }
    CHECK_EQ(fs::ring_width_fb(0.5f), 1.0f);   // test.2's fixed 1 fb px ring
    CHECK_EQ(fs::ring_width_fb(0.25f), 1.0f);  // 0.5 px clamped up to the minimum
    CHECK_EQ(fs::ring_width_fb(1.0f), 2.0f);
}

// Offset quantization: OFFSET_QUANTUM steps per framebuffer px at every
// scale, so the round-trip error is at most half a step.
void test_offset_quantization() {
    for (float scale : {0.5f, 0.25f}) {
        const float half_step_logical = 0.5f / (fs::OFFSET_QUANTUM * scale);
        bool ok = true, monotone = true;
        int32_t prev = INT32_MIN;
        for (int i = -40000; i <= 40000; ++i) {
            const float off = (float)i * 0.0137f;  // about -548..548 logical px
            const int32_t q = fs::quantize_offset(off, scale);
            const float back = fs::dequantize_offset(q, scale);
            if (std::fabs(back - off) > half_step_logical + 1e-4f) ok = false;
            if (q < prev) monotone = false;
            prev = q;
        }
        CHECK_ALL(ok, "round trip at scale %f", scale);
        CHECK_ALL(monotone, "monotone at scale %f", scale);
        CHECK_EQ(fs::quantize_offset(0.0f, scale), 0);
        CHECK_EQ(fs::quantize_offset(-0.0f, scale), 0);
    }
    // One logical px is 2 steps at 0.5 and 1 step at 0.25.
    CHECK_EQ(fs::quantize_offset(1.0f, 0.5f), 2);
    CHECK_EQ(fs::quantize_offset(1.0f, 0.25f), 1);
    CHECK_EQ(fs::dequantize_offset(2, 0.5f), 1.0f);
    CHECK_EQ(fs::dequantize_offset(1, 0.25f), 1.0f);
}

// Every anchor of every style/density, clamped the way build_frame() does
// when not free floating, lands inside the framebuffer at both scales, and
// the 0.25 centre is exactly half the 0.5 one.
void test_centers_inside() {
    dots::Point anchors[dots::MAX_ANCHORS];
    const float offsets[] = {-2000.0f, -300.0f, -10.0f, 0.0f, 10.0f, 300.0f, 2000.0f};
    bool inside_full = true, inside_small = true, halves = true, any = false;
    for (int style = 0; style < 16; ++style)
        for (int density = 0; density < 16; ++density) {
            const size_t n = dots::get_anchors(style, density, anchors, dots::MAX_ANCHORS);
            for (size_t i = 0; i < n; ++i)
                for (float dx : offsets)
                    for (float dy : offsets) {
                        any = true;
                        float px = anchors[i].x + dx, py = anchors[i].y + dy;
                        px = px < 10.0f ? 10.0f : (px > 1270.0f ? 1270.0f : px);
                        py = py < 10.0f ? 10.0f : (py > 710.0f ? 710.0f : py);
                        const float fx = fs::center_fb(px, 0.5f), fy = fs::center_fb(py, 0.5f);
                        const float sx = fs::center_fb(px, 0.25f), sy = fs::center_fb(py, 0.25f);
                        if (!(fx >= 0 && fx < FULL.w && fy >= 0 && fy < FULL.h)) inside_full = false;
                        if (!(sx >= 0 && sx < SMALL.w && sy >= 0 && sy < SMALL.h)) inside_small = false;
                        if (sx != fx * 0.5f || sy != fy * 0.5f) halves = false;
                    }
        }
    CHECK(any);
    CHECK(inside_full);
    CHECK(inside_small);
    CHECK(halves);

    // The extreme logical pixels map to the first/last framebuffer pixel.
    CHECK_EQ(fs::center_fb(0.0f, 0.25f), 0.125f);
    CHECK(fs::center_fb(1279.0f, 0.25f) < 320.0f && fs::center_fb(1279.0f, 0.25f) > 319.0f);
    CHECK(fs::center_fb(719.0f, 0.25f) < 180.0f && fs::center_fb(719.0f, 0.25f) > 179.0f);
}

} // namespace

int main() {
    test_scale_factors();
    test_dot_sizes();
    test_offset_quantization();
    test_centers_inside();
    return check::summary("scale");
}
