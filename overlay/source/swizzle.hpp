// Kinestop HUD - block-linear pixel addressing for 16-bit framebuffers.
//
// Part of a modified version of the original Kinestop by SertAy
// (https://github.com/sertay1/Kinestop).
//
// Header-only and host-compilable (stdint only) so it can be unit tested
// against libnx's linear->block-linear converter on a PC.
//
// libnx's framebufferCreate() lays buffers out as NvLayout_BlockLinear with
// NvKind_Generic_16BX2 and block_height_log2 = 4:
//   * a GOB is 64 bytes x 8 rows (32 x 8 pixels at 2 bytes/pixel), 512 bytes,
//     stored with the 16Bx2 sector swizzle (_convertGobTo16Bx2 in libnx);
//   * a block is 1 GOB wide and 16 GOBs (128 rows) tall, 8 KiB;
//   * blocks are stored row-major, ceil(width_bytes / 64) blocks per row.
// Height is padded to a multiple of 128 rows, so 640x360 occupies
// 1280 B x 384 rows = 491,520 bytes per buffer.
//
// For widths that are a multiple of 32 this is exactly libtesla's
// Renderer::getPixelOffset(), whose offsetWidthVar is
// ((width / 2) >> 4) << 3 == blocks_per_row * 8.

#pragma once

#include <stdint.h>

namespace kinestop::swizzle {

// Framebuffer row width in pixels after libnx's 64-byte GOB alignment.
constexpr uint32_t aligned_width_16bpp(uint32_t width) {
    return (width + 31u) & ~31u;
}

// Rows after libnx's block-height alignment (block_height_log2 = 4).
constexpr uint32_t aligned_height(uint32_t height) {
    return (height + 127u) & ~127u;
}

// Bytes in one buffer slot, matching libnx Framebuffer::fb_size.
constexpr uint32_t buffer_size_16bpp(uint32_t width, uint32_t height) {
    return aligned_width_16bpp(width) * 2u * aligned_height(height);
}

// One GOB: 64 bytes x 8 rows = 32 x 8 pixels at 2 bytes/pixel.
constexpr uint32_t GOB_WIDTH_16BPP = 32;
constexpr uint32_t GOB_HEIGHT = 8;
constexpr uint32_t GOB_BYTES = 512;

// Offset in 16-bit units (not bytes) of pixel (x, y) in a buffer `width`
// pixels wide. No bounds check.
constexpr uint32_t pixel_offset_16bpp(uint32_t x, uint32_t y, uint32_t width) {
    const uint32_t blocks_per_row = (width + 31u) >> 5;
    // Units of 512 pixels (two GOBs): GOB pair within the block, block column,
    // block row. Then the odd GOB of the pair, then the 16Bx2 sector swizzle
    // inside the GOB: pixel bits x4 y2 y1 x3 y0 x2 x1 x0.
    return ((((y & 127u) >> 4) + ((x >> 5) << 3) + ((y >> 7) * (blocks_per_row << 3))) << 9) +
           ((y & 8u) << 5) + ((x & 16u) << 3) + ((y & 6u) << 4) +
           ((x & 8u) << 1) + ((y & 1u) << 3) + (x & 7u);
}

// Offset in 16-bit units of the first pixel of the GOB that holds (x, y).
// Every pixel of that GOB lies in [gob_offset, gob_offset + GOB_BYTES / 2):
// the in-GOB bits of pixel_offset_16bpp() are all zero for the GOB's
// top-left pixel and sum to at most 255.
constexpr uint32_t gob_offset_16bpp(uint32_t x, uint32_t y, uint32_t width) {
    return pixel_offset_16bpp(x & ~31u, y & ~7u, width);
}

} // namespace kinestop::swizzle
