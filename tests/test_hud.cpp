// Kinestop host tests - overlay/source/hud_frame.hpp: the HUD launch
// argument and the dot frame drawn into the 640x360 HUD framebuffer.
//
// Part of a modified version of the original Kinestop by SertAy
// (https://github.com/sertay1/Kinestop).

#include <cstdint>
#include <vector>

#include "check.hpp"
#include "config.hpp"
#include "dots.hpp"
#include "hud_frame.hpp"
#include "swizzle.hpp"

namespace hud = kinestop::hud;
namespace dots = kinestop::dots;
namespace swz = kinestop::swizzle;

namespace {

constexpr uint32_t W = 640, H = 360;

void test_launch_args() {
    char path[] = "sdmc:/switch/.overlays/kinestop.ovl";
    char hud_arg[] = "-hud";
    char silent[] = "--silentLaunch";
    char other[] = "--hud";
    char* settings[] = {path};
    char* hud_only[] = {path, hud_arg};
    char* hud_silent[] = {path, hud_arg, silent};
    char* silent_hud[] = {path, silent, hud_arg};
    char* not_hud[] = {path, other, silent};
    char* hud_as_path[] = {hud_arg};  // argv[0] is the path, never a mode

    CHECK(!hud::is_hud_launch(0, nullptr));
    CHECK(!hud::is_hud_launch(1, settings));
    CHECK(hud::is_hud_launch(2, hud_only));
    CHECK(hud::is_hud_launch(3, hud_silent));
    CHECK(hud::is_hud_launch(3, silent_hud));
    CHECK(!hud::is_hud_launch(3, not_hud));
    CHECK(!hud::is_hud_launch(1, hud_as_path));
}

void test_frame_style() {
    kinestop::Config cfg;
    hud::DotFrame frame;
    hud::build_dot_frame(cfg, W, frame);
    CHECK_EQ(frame.scale, 0.5f);

    dots::Point anchors[dots::MAX_ANCHORS];
    CHECK_EQ(frame.count, dots::get_anchors(cfg.visual.style, cfg.visual.density, anchors, dots::MAX_ANCHORS));
    CHECK(frame.count > 0);
    CHECK_EQ(frame.style.ring_width, 1.0f);

    // Opacity -> alpha, clamped to 25..255; the border is 50 more, up to 255.
    cfg.visual.opacity = 0.0f;
    hud::build_dot_frame(cfg, W, frame);
    CHECK_EQ(frame.style.core.a, 25);
    CHECK_EQ(frame.style.border.a, 75);
    cfg.visual.opacity = 1.0f;
    hud::build_dot_frame(cfg, W, frame);
    CHECK_EQ(frame.style.core.a, 255);
    CHECK_EQ(frame.style.border.a, 255);

    // Out-of-range colors fall back to the first/last theme.
    cfg.visual.color = -5;
    hud::build_dot_frame(cfg, W, frame);
    CHECK_EQ(frame.style.core.r, dots::THEMES[0].cr);
    CHECK_EQ(frame.style.border.b, dots::THEMES[0].bb);
    cfg.visual.color = 1000;
    hud::build_dot_frame(cfg, W, frame);
    CHECK_EQ(frame.style.core.g, dots::THEMES[dots::NUM_THEMES - 1].cg);
}

// Clamped (not free floating) centres stay 5 fb px inside the framebuffer
// for any offset; free floating ones follow the offset off screen.
void test_centers() {
    kinestop::Config cfg;
    hud::DotFrame frame;
    bool inside = true, any = false;
    const float offsets[] = {-5000.0f, -333.3f, 0.0f, 12.34f, 5000.0f};
    for (int style = 0; style < 16; ++style)
        for (int density = 0; density < 16; ++density) {
            cfg.visual.style = style;
            cfg.visual.density = density;
            hud::build_dot_frame(cfg, W, frame);
            for (float ox : offsets)
                for (float oy : offsets) {
                    const dots::Point snapped = hud::snap_offset({ox, oy}, frame.scale);
                    for (size_t i = 0; i < frame.count; ++i) {
                        any = true;
                        const dots::Point c = hud::dot_center(frame, i, snapped, false);
                        if (!(c.x >= 5.0f && c.x < W - 4.0f && c.y >= 5.0f && c.y < H - 4.0f))
                            inside = false;
                    }
                }
        }
    CHECK(any);
    CHECK(inside);

    cfg = kinestop::Config{};
    hud::build_dot_frame(cfg, W, frame);
    const dots::Point far = hud::dot_center(frame, 0, hud::snap_offset({5000.0f, 0.0f}, frame.scale), true);
    CHECK(far.x > (float)W);

    // Snapping: quarter framebuffer pixels, i.e. 0.5 logical px at 0.5.
    const dots::Point s = hud::snap_offset({1.3f, -0.2f}, 0.5f);
    CHECK_EQ(s.x, 1.5f);
    CHECK_EQ(s.y, 0.0f);
}

// draw_dots() puts every dot's core colour at its centre and leaves the
// padding rows (360..383) untouched.
void test_draw() {
    kinestop::Config cfg;
    cfg.visual.opacity = 1.0f;
    hud::DotFrame frame;
    hud::build_dot_frame(cfg, W, frame);

    std::vector<uint16_t> px(swz::buffer_size_16bpp(W, H) / 2, 0);
    const kinestop::raster::Surface surf{px.data(), W, H};
    const dots::Point offset{20.0f, -14.0f};
    hud::draw_dots(surf, frame, offset, false);

    const dots::Point snapped = hud::snap_offset(offset, frame.scale);
    bool opaque_centers = true;
    for (size_t i = 0; i < frame.count; ++i) {
        const dots::Point c = hud::dot_center(frame, i, snapped, false);
        const uint16_t v = px[swz::pixel_offset_16bpp((uint32_t)c.x, (uint32_t)c.y, W)];
        if ((v >> 12) != 0xF) opaque_centers = false;
    }
    CHECK(opaque_centers);

    bool padding_clear = true;
    for (uint32_t y = H; y < swz::aligned_height(H); ++y)
        for (uint32_t x = 0; x < W; ++x)
            if (px[swz::pixel_offset_16bpp(x, y, W)] != 0) padding_clear = false;
    CHECK(padding_clear);
}

} // namespace

int main() {
    test_launch_args();
    test_frame_style();
    test_centers();
    test_draw();
    return check::summary("hud");
}
