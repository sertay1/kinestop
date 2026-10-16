// Kinestop sysmodule - VI layer + framebuffer that shows the motion-cue dots.
//
// Original Kinestop by SertAy (https://github.com/sertay1/Kinestop). The
// sysmodule/overlay split is a modification of the original project: dots
// used to be drawn by the Tesla overlay itself; now this module owns a VI
// managed layer the way libtesla does, but with a small framebuffer (640x360
// normally) that VI scales up to the layer size. Buffers are dequeued with a hard deadline so a
// compositor that stops releasing them can never stall the main loop.

#pragma once

#include <switch.h>
#include <cstddef>

#include "../../common/kinestop_ipc.h"
#include "../../common/dots.hpp"
#include "raster.hpp"

namespace kinestop {

// One frame's worth of dots, already in framebuffer coordinates.
struct DotFrame {
    size_t count;
    dots::Point centers[dots::MAX_ANCHORS];
    raster::DotStyle style;
};

// Cumulative present-path counters (never reset, wrap at 2^32).
struct PresentStats {
    u32 dequeue_timeouts;  // presents abandoned: no buffer free before the deadline, or its fence did not signal in time
    u32 fence_timeouts;    // ...of which the release fence timed out (the slot was cancelled again)
    u32 would_blocks;      // bqDequeueBuffer WouldBlock replies
    u32 event_wakes;       // buffer-release event waits that returned signalled
    u32 fence_errors;      // fence waits that failed with something other than a timeout (buffer used anyway)
    Result last_fence_rc;  // Result of the latest such failure, 0 if none
};

// Framebuffer size + layer size combinations. Chosen at init(); changing it
// means re-creating the layer.
enum class LayerMode : u8 {
    Full2x,     // 640x360 fb in the full 1920x1080 VI space (2x on the 1280x720 panel)
    Quarter1x,  // KINESTOP_DEBUG_UNSCALED_LAYER: 640x360 fb in a 960x540 VI-unit layer
                // at 0,0 (1:1 on the handheld panel; the whole pattern, half size)
    Quarter2x,  // KINESTOP_DEBUG_SCALED_SMALL: 320x180 fb in a 960x540 VI-unit layer
                // at 0,0 (2x on the panel, like Full2x; the whole pattern, half size)
};

const char* layer_mode_name(LayerMode mode);

// What a successful init() set up, for the "layer up" log line.
struct LayerInfo {
    s32 z_min, z_max;      // viGetZOrderCountMin/Max (-1 if the call failed)
    s32 z;                 // Z actually applied
    s32 width, height;     // layer size in VI units (at 0,0)
    bool underscan;        // docked with TV underscan: Z = UNDERSCAN_Z
    u32 fb_width, fb_height;  // framebuffer pixels
    bool in_captures;
    LayerMode mode;
};

class Renderer {
public:
    // Framebuffer sizes. 640x360 RGBA4444 is 1280 B x 384 rows (height
    // padded to 128) = 491,520 B per slot; 320x180 is 640 B x 256 rows =
    // 163,840 B per slot (libnx framebufferCreate() formula, see swizzle.hpp).
    static constexpr u32 FB_WIDTH  = 640;
    static constexpr u32 FB_HEIGHT = 360;
    static constexpr u32 SMALL_FB_WIDTH  = 320;
    static constexpr u32 SMALL_FB_HEIGHT = 180;
    static constexpr u32 NUM_FBS   = 2;

    // VI coordinate space for a full-screen layer. libtesla uses 1920x1080
    // (tsl::cfg::ScreenWidth/ScreenHeight) for every layer size/position.
    static constexpr s32 LAYER_WIDTH  = 1920;
    static constexpr s32 LAYER_HEIGHT = 1080;

    // LayerMode::Quarter1x / Quarter2x: a 960x540 VI-unit layer at (0,0),
    // the top-left quarter of the screen. The framebuffer still holds the
    // whole dot pattern (scaled by fb_width / 1280), so the whole pattern
    // shows there at half size.
    static constexpr s32 QUARTER_LAYER_WIDTH  = 960;
    static constexpr s32 QUARTER_LAYER_HEIGHT = 540;

    // libtesla drops its own layer to Z=34 while docked with TV underscan
    // active; stay just below it in that case.
    static constexpr s32 UNDERSCAN_Z = 33;

    Renderer();

    // Creates the layer and framebuffer. On failure everything created so far
    // is torn down again and `stage` names the step that failed.
    Result init(bool show_in_captures, LayerMode mode, bool low_z, KinestopStage& stage);

    // Presents one transparent frame (bounded), then destroys everything:
    // layer, display, vi session and the framebuffers (heap memory).
    void exit();

    bool ready() const { return m_fb_ok; }
    bool in_captures() const { return m_in_captures; }
    LayerMode mode() const { return m_mode; }
    bool low_z() const { return m_low_z; }
    // Framebuffer size of the current layer (valid while ready()).
    u32 fb_width() const { return m_fb_width; }
    u32 fb_height() const { return m_fb_height; }
    const LayerInfo& info() const { return m_info; }
    const PresentStats& stats() const { return m_stats; }

    // Draws and queues a frame. On failure the caller should exit() and retry
    // init() later; `stage` is set to KinestopStage_Framebuffer. Succeeds with
    // `presented` false when the compositor did not hand out a usable buffer
    // within a bounded time (counted in stats().dequeue_timeouts): the frame
    // was skipped, nothing was touched, and it should be presented again.
    // Never blocks for more than about DEQUEUE_DEADLINE_NS + FENCE_TIMEOUT_US.
    Result present(const DotFrame& frame, KinestopStage& stage, bool& presented);

    // Re-evaluates docked + TV underscan and adjusts Z like libtesla does.
    // Cheap (two small IPCs); call about once per second. Returns the
    // viSetLayerZ() failure, if any (non-fatal; retried on the next call).
    Result update_z();

    // Result of the last non-fatal error inside init() (e.g. a capture layer
    // stack that could not be joined), 0 if none.
    Result last_soft_error() const { return m_soft_rc; }

private:
    bool m_vi_ok;
    bool m_display_ok;
    bool m_managed_ok;
    bool m_layer_ok;
    bool m_nwin_ok;
    bool m_fb_ok;
    bool m_in_captures;
    LayerMode m_mode;
    u32 m_fb_width, m_fb_height;
    bool m_underscan_z;
    s32 m_normal_z;
    bool m_low_z = false;  // KINESTOP_DEBUG_LOW_Z: normal Z is UNDERSCAN_Z
    Result m_soft_rc;
    LayerInfo m_info;
    PresentStats m_stats;

    ViDisplay m_display;
    ViLayer m_layer;
    NWindow m_window;
    Framebuffer m_fb;

    // Per-slot rectangles drawn last time, cleared before the slot is reused.
    raster::Rect m_dirty[NUM_FBS][dots::MAX_ANCHORS];
    size_t m_dirty_count[NUM_FBS];

    Result add_to_layer_stack(ViLayerStack stack);
    bool underscan_active();
    raster::Surface surface_for_slot(s32 slot);
    Result dequeue_bounded(s32& out_slot);
    Result draw_slot(const DotFrame* frame, bool& presented);
    void teardown();
};

} // namespace kinestop
