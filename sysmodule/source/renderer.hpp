// Kinestop sysmodule - VI layer + framebuffer that shows the motion-cue dots.
//
// Original Kinestop by SertAy (https://github.com/sertay1/Kinestop). The
// sysmodule/overlay split is a modification of the original project: dots
// used to be drawn by the Tesla overlay itself; now this module owns a VI
// managed layer the way libtesla does, but with a 640x360 framebuffer that VI
// scales to the full screen.

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

class Renderer {
public:
    static constexpr u32 FB_WIDTH  = 640;
    static constexpr u32 FB_HEIGHT = 360;
    static constexpr u32 NUM_FBS   = 2;

    // VI coordinate space for a full-screen layer. libtesla uses 1920x1080
    // (tsl::cfg::ScreenWidth/ScreenHeight) for every layer size/position.
    static constexpr s32 LAYER_WIDTH  = 1920;
    static constexpr s32 LAYER_HEIGHT = 1080;

    // libtesla drops its own layer to Z=34 while docked with TV underscan
    // active; stay just below it in that case.
    static constexpr s32 UNDERSCAN_Z = 33;

    Renderer();

    // Creates the layer and framebuffer. On failure everything created so far
    // is torn down again and `stage` names the step that failed.
    Result init(bool show_in_captures, KinestopStage& stage);

    // Presents one transparent frame, then destroys everything.
    void exit();

    bool ready() const { return m_fb_ok; }
    bool in_captures() const { return m_in_captures; }

    // Draws and queues a frame. On failure the caller should exit() and retry
    // init() later; `stage` is set to KinestopStage_Framebuffer. Succeeds with
    // `presented` false when the compositor released no buffer within a
    // bounded wait: the frame was skipped and should be presented again.
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
    bool m_underscan_z;
    s32 m_normal_z;
    Result m_soft_rc;

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
    Result draw_slot(const DotFrame* frame, bool& presented);
    void teardown();
};

} // namespace kinestop
