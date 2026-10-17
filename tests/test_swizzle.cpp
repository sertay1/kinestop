// Kinestop host tests - block-linear framebuffer addressing (swizzle.hpp).
//
// Part of a modified version of the original Kinestop by SertAy
// (https://github.com/sertay1/Kinestop).
//
// Three independent oracles:
//   1. libtesla's Renderer::getPixelOffset() (copied verbatim, known good on
//      hardware for the 448 and 1280 px wide Tesla framebuffers);
//   2. a port of libnx's _convertToBlocklinear()/_convertGobTo16Bx2()
//      (nx/source/display/framebuffer.c), which is what actually lays out a
//      framebufferCreate() buffer;
//   3. the Tegra X1 block-linear address formula written from the GOB
//      definition (GOB = 64 B x 8 rows, 16Bx2 sectors, block = 16 GOBs tall).

#include <cstdint>
#include <cstring>
#include <vector>

#include "check.hpp"
#include "swizzle.hpp"

using kinestop::swizzle::aligned_height;
using kinestop::swizzle::aligned_width_16bpp;
using kinestop::swizzle::buffer_size_16bpp;
using kinestop::swizzle::gob_offset_16bpp;
using kinestop::swizzle::pixel_offset_16bpp;

namespace {

// ---- Oracle 1: libtesla (overlay/lib/libultrahand/libtesla/include/tesla.hpp)
// offsetWidthVar = (((cfg::FramebufferWidth / 2) >> 4) << 3);
uint32_t tesla_offset_width_var(uint32_t fb_width) {
    return (((fb_width / 2) >> 4) << 3);
}

// Renderer::getPixelOffset() without the scissor check.
uint32_t tesla_get_pixel_offset(uint32_t x, uint32_t y, uint32_t offsetWidthVar) {
    return ((((y & 127) >> 4) + ((x >> 5) << 3) + ((y >> 7) * offsetWidthVar)) << 9) +
           ((y & 8) << 5) + ((x & 16) << 3) + ((y & 6) << 4) +
           ((x & 8) << 1) + ((y & 1) << 3) + (x & 7);
}

// ---- Oracle 2: port of libnx framebuffer.c. Instead of copying pixel data,
// records for every 16-byte unit of the linear source image where it lands in
// the block-linear output. Returns a map: linear 16-byte unit index ->
// output byte offset (UINT32_MAX = not written).
std::vector<uint32_t> libnx_blocklinear_map(uint32_t width, uint32_t height) {
    const uint32_t bytes_per_pixel = 2;
    const uint32_t block_height_log2 = 4;
    const uint32_t block_height = 8 * (1U << block_height_log2);
    const uint32_t width_aligned_bytes = (width * bytes_per_pixel + 63) & ~63u;
    const uint32_t height_aligned = (height + block_height - 1) & ~(block_height - 1);
    const uint32_t stride = width_aligned_bytes;

    std::vector<uint32_t> map((size_t)stride * height_aligned / 16, UINT32_MAX);

    // _convertGobTo16Bx2(outgob, ingob, stride), with "ingob" given as the
    // linear byte offset of the GOB's top-left corner.
    auto convert_gob = [&](uint32_t outgob, uint32_t ingob) {
        for (uint32_t i = 0; i < 32; i++) {
            const uint32_t y = ((i >> 1) & 0x06) | (i & 0x01);
            const uint32_t x = ((i << 3) & 0x10) | ((i << 1) & 0x20);
            const uint32_t src = ingob + y * stride + x;
            map[src / 16] = outgob;
            outgob += 16;
        }
    };

    // _convertToBlocklinear(outbuf, inbuf, stride, height, block_height_log2)
    const uint32_t block_height_gobs = 1U << block_height_log2;
    const uint32_t block_height_px = 8U << block_height_log2;
    const uint32_t width_blocks = stride >> 6;
    const uint32_t height_blocks = (height + block_height_px - 1) >> (3 + block_height_log2);
    uint32_t outgob = 0;
    for (uint32_t block_y = 0; block_y < height_blocks; block_y++) {
        for (uint32_t block_x = 0; block_x < width_blocks; block_x++) {
            for (uint32_t gob_y = 0; gob_y < block_height_gobs; gob_y++) {
                const uint32_t x = block_x * 64;
                const uint32_t y = block_y * block_height_px + gob_y * 8;
                if (y < height)
                    convert_gob(outgob, y * stride + x);
                outgob += 512;
            }
        }
    }
    return map;
}

// Byte offset of pixel (x, y) according to the libnx map.
uint32_t libnx_byte_offset(const std::vector<uint32_t>& map, uint32_t x, uint32_t y, uint32_t width) {
    const uint32_t stride = (width * 2 + 63) & ~63u;
    const uint32_t linear = y * stride + x * 2;
    const uint32_t unit = map[linear / 16];
    return unit == UINT32_MAX ? UINT32_MAX : unit + (linear % 16);
}

// ---- Oracle 3: Tegra block-linear from first principles.
uint32_t tegra_byte_offset(uint32_t x, uint32_t y, uint32_t width) {
    const uint32_t xb = x * 2;                         // byte column
    const uint32_t gobs_per_row = (width * 2 + 63) / 64;
    const uint32_t block_rows = 128;                   // 16 GOBs x 8 rows
    const uint32_t block_bytes = 512 * 16;

    const uint32_t block = (y / block_rows) * gobs_per_row + (xb / 64);
    const uint32_t gob_in_block = (y % block_rows) / 8;
    // Offset inside a 512-byte GOB with 16Bx2 sector ordering.
    const uint32_t in_gob = (((xb & 63) >> 5) << 8) | (((y & 7) >> 1) << 6) |
                            (((xb & 31) >> 4) << 5) | ((y & 1) << 4) | (xb & 15);
    return block * block_bytes + gob_in_block * 512 + in_gob;
}

void test_tesla_widths() {
    struct { uint32_t w, h; } sizes[] = {{1280, 720}, {448, 720}};
    for (auto s : sizes) {
        const uint32_t owv = tesla_offset_width_var(s.w);
        bool ok = true;
        uint32_t bx = 0, by = 0;
        for (uint32_t y = 0; y < s.h && ok; ++y)
            for (uint32_t x = 0; x < s.w; ++x)
                if (pixel_offset_16bpp(x, y, s.w) != tesla_get_pixel_offset(x, y, owv)) {
                    ok = false; bx = x; by = y; break;
                }
        CHECK_ALL(ok, "width %u: mismatch vs libtesla at (%u,%u)", s.w, bx, by);
    }
}

void test_640x360_layout() {
    const uint32_t W = 640, H = 360;
    CHECK_EQ(aligned_width_16bpp(W), 640u);
    CHECK_EQ(aligned_height(H), 384u);
    CHECK_EQ(buffer_size_16bpp(W, H), 491520u);
    CHECK_EQ(2u * buffer_size_16bpp(W, H), 983040u);

    const uint32_t units = buffer_size_16bpp(W, H) / 2;  // 245,760 u16 slots
    CHECK_EQ(units, 245760u);

    std::vector<uint8_t> seen(units, 0);
    bool in_range = true, unique = true;
    for (uint32_t y = 0; y < H; ++y)
        for (uint32_t x = 0; x < W; ++x) {
            const uint32_t off = pixel_offset_16bpp(x, y, W);
            if (off >= units) { in_range = false; continue; }
            if (seen[off]++) unique = false;
        }
    CHECK(in_range);
    CHECK(unique);

    // The 24 padding rows of the last block row must not be addressed by any
    // visible pixel, and the visible pixels fill everything else.
    size_t used = 0;
    for (uint8_t s : seen) used += s;
    CHECK_EQ(used, (size_t)W * H);

    // Padded area: offsets of the virtual rows 360..383 are all unused.
    bool padding_free = true;
    for (uint32_t y = H; y < aligned_height(H); ++y)
        for (uint32_t x = 0; x < W; ++x)
            if (seen[pixel_offset_16bpp(x, y, W)]) padding_free = false;
    CHECK(padding_free);
}

// A 320x180 RGBA4444 framebuffer (fb_scale 0.25).
// libnx framebufferCreate(): stride (320*2 + 63) & ~63 = 640 B, rows
// (180 + 127) & ~127 = 256 -> 163,840 B per slot.
void test_320x180_layout() {
    const uint32_t W = 320, H = 180;
    CHECK_EQ(aligned_width_16bpp(W), 320u);
    CHECK_EQ(aligned_width_16bpp(W) * 2u, 640u);  // stride in bytes
    CHECK_EQ(aligned_height(H), 256u);
    CHECK_EQ(buffer_size_16bpp(W, H), 163840u);
    CHECK_EQ(2u * buffer_size_16bpp(W, H), 327680u);
    CHECK_EQ((2u * buffer_size_16bpp(W, H)) % 0x1000u, 0u);  // libnx page-aligns the total

    // Same numbers as the libnx port (oracle 2) derives.
    const std::vector<uint32_t> map = libnx_blocklinear_map(W, H);
    CHECK_EQ((uint32_t)map.size() * 16u, buffer_size_16bpp(W, H));

    const uint32_t units = buffer_size_16bpp(W, H) / 2;
    std::vector<uint8_t> seen(units, 0);
    bool in_range = true, unique = true;
    for (uint32_t y = 0; y < H; ++y)
        for (uint32_t x = 0; x < W; ++x) {
            const uint32_t off = pixel_offset_16bpp(x, y, W);
            if (off >= units) { in_range = false; continue; }
            if (seen[off]++) unique = false;
        }
    CHECK(in_range);
    CHECK(unique);

    bool padding_free = true;
    for (uint32_t y = H; y < aligned_height(H); ++y)
        for (uint32_t x = 0; x < W; ++x)
            if (seen[pixel_offset_16bpp(x, y, W)]) padding_free = false;
    CHECK(padding_free);

    // libtesla's formula agrees for this width too (320 is a multiple of 32).
    const uint32_t owv = tesla_offset_width_var(W);
    bool tesla_ok = true;
    for (uint32_t y = 0; y < H && tesla_ok; ++y)
        for (uint32_t x = 0; x < W; ++x)
            if (pixel_offset_16bpp(x, y, W) != tesla_get_pixel_offset(x, y, owv)) { tesla_ok = false; break; }
    CHECK(tesla_ok);
}

void test_reference_impls() {
    struct { uint32_t w, h; } sizes[] = {
        {640, 360}, {320, 180}, {448, 720}, {1280, 720}, {1920, 1080}, {100, 50}, {33, 130}, {32, 8}, {1, 1},
    };
    for (auto s : sizes) {
        const std::vector<uint32_t> map = libnx_blocklinear_map(s.w, s.h);
        bool ok_libnx = true, ok_tegra = true;
        uint32_t fx = 0, fy = 0;
        for (uint32_t y = 0; y < s.h; ++y)
            for (uint32_t x = 0; x < s.w; ++x) {
                const uint32_t ours = pixel_offset_16bpp(x, y, s.w) * 2;
                if (ok_libnx && ours != libnx_byte_offset(map, x, y, s.w)) { ok_libnx = false; fx = x; fy = y; }
                if (ok_tegra && ours != tegra_byte_offset(x, y, s.w)) { ok_tegra = false; fx = x; fy = y; }
            }
        CHECK_ALL(ok_libnx, "%ux%u: mismatch vs libnx _convertToBlocklinear at (%u,%u)", s.w, s.h, fx, fy);
        CHECK_ALL(ok_tegra, "%ux%u: mismatch vs Tegra GOB formula at (%u,%u)", s.w, s.h, fx, fy);

        // Every visible pixel stays inside one buffer slot.
        bool bounded = true;
        for (uint32_t y = 0; y < s.h; ++y)
            for (uint32_t x = 0; x < s.w; ++x)
                if (pixel_offset_16bpp(x, y, s.w) * 2 + 2 > buffer_size_16bpp(s.w, s.h)) bounded = false;
        CHECK_ALL(bounded, "%ux%u: offset past buffer_size_16bpp", s.w, s.h);
    }
}

// The renderer flushes only the GOBs a dot rectangle overlaps, as 512 B
// starting at gob_offset_16bpp() of the GOB's top-left pixel. That is only
// enough if every pixel of the GOB lands inside those 512 bytes and distinct
// GOBs do not overlap.
void test_gob_offsets_for(uint32_t w, uint32_t h) {
    const uint32_t gobs = buffer_size_16bpp(w, h) / kinestop::swizzle::GOB_BYTES;
    std::vector<uint32_t> owner(gobs, UINT32_MAX);
    bool inside = true, aligned = true, unique = true;
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            const uint32_t base = gob_offset_16bpp(x, y, w);
            const uint32_t off = pixel_offset_16bpp(x, y, w);
            if (off < base || off >= base + kinestop::swizzle::GOB_BYTES / 2) inside = false;
            if ((base * 2) % kinestop::swizzle::GOB_BYTES != 0) aligned = false;
            const uint32_t gob_id = (y / 8) * (w / 32) + (x / 32);
            uint32_t& o = owner[base * 2 / kinestop::swizzle::GOB_BYTES];
            if (o == UINT32_MAX) o = gob_id;
            else if (o != gob_id) unique = false;
        }
    CHECK_ALL(inside, "pixel outside the 512 B at gob_offset_16bpp()");
    CHECK_ALL(aligned, "gob_offset_16bpp() not 512 B aligned");
    CHECK_ALL(unique, "two GOBs share a gob_offset_16bpp()");
}

void test_gob_offsets() {
    test_gob_offsets_for(640, 384);  // 640x360, padded
    test_gob_offsets_for(320, 256);  // 320x180, padded
}

} // namespace

int main() {
    test_tesla_widths();
    test_640x360_layout();
    test_320x180_layout();
    test_reference_impls();
    test_gob_offsets();
    return check::summary("swizzle");
}
