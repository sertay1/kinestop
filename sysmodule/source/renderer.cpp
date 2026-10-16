// Kinestop sysmodule - VI layer + framebuffer that shows the motion-cue dots.
//
// Original Kinestop by SertAy (https://github.com/sertay1/Kinestop). The
// sysmodule/overlay split is a modification of the original project.
//
// Layer setup mirrors libtesla's Renderer::init() (tesla.hpp), which is known
// to work from an AppletType_None process (nx-ovlloader):
//   viInitialize(Manager) -> viOpenDefaultDisplay -> viCreateManagedLayer
//   -> viCreateLayer -> FitToLayer -> Z -> AddToLayerStack (cmd 6000)
//   -> viSetLayerSize/Position -> nwindowCreateFromLayer -> framebufferCreate
// Differences, all deliberate:
//   * Z is one below the maximum so Tesla's own UI (which takes the maximum)
//     stays above the dots.
//   * Screenshot/Recording stacks only when show_in_captures is set. Stack
//     membership is fixed at creation, so changing it re-creates the layer.
//     libtesla also joins Arbitrary (am captures), LastFrame (applet-
//     transition snapshot), ApplicationForDebug (creport/debug captures) and
//     Null (the stack of the empty display). None of them is needed for the
//     LCD or a TV (Default + Lcd), so we leave them out.
//   * The framebuffer is 640x360 and the layer is the whole 1920x1080 VI
//     space, so VI upscales it 3x (FitToLayer).
//   * Buffers are dequeued/queued by hand instead of framebufferBegin/End,
//     which call diagAbortWithResult() on failure. Here a failure just tears
//     the layer down so the main loop can retry.
//   * The dequeue is preceded by a bounded wait on the buffer-release event
//     (nwindowDequeueBuffer() itself waits forever), and only the GOBs that
//     were written are flushed from the data cache, not the whole slot.

#include "renderer.hpp"

#include <cstring>

extern "C" u64 __nx_vi_layer_id;

namespace kinestop {

namespace {

// How long a present waits for the compositor to release a buffer before it
// skips the frame (the main loop tries again next iteration). With two
// buffers and vsync a release is normally at most one refresh (~17 ms) away.
constexpr u64 DEQUEUE_TIMEOUT_NS = 100000000ULL;  // 100 ms

// Cleans + invalidates the cache lines of every GOB (32x8 px, 512 B) that
// overlaps `r`. Every CPU write to a slot happens inside such a rectangle
// (clear_rect() / draw_dot()), so this is all the GPU needs to see.
void flush_rect(const raster::Surface& s, const raster::Rect& r) {
    if (r.empty()) return;
    for (u32 gy = (u32)r.y0 & ~7u; gy < (u32)r.y1; gy += swizzle::GOB_HEIGHT)
        for (u32 gx = (u32)r.x0 & ~31u; gx < (u32)r.x1; gx += swizzle::GOB_WIDTH_16BPP)
            armDCacheFlush(s.pixels + swizzle::gob_offset_16bpp(gx, gy, s.width), swizzle::GOB_BYTES);
}

} // namespace

Renderer::Renderer()
    : m_vi_ok(false)
    , m_display_ok(false)
    , m_managed_ok(false)
    , m_layer_ok(false)
    , m_nwin_ok(false)
    , m_fb_ok(false)
    , m_in_captures(false)
    , m_underscan_z(false)
    , m_normal_z(0)
    , m_soft_rc(0)
{
    std::memset(&m_display, 0, sizeof(m_display));
    std::memset(&m_layer, 0, sizeof(m_layer));
    std::memset(&m_window, 0, sizeof(m_window));
    std::memset(&m_fb, 0, sizeof(m_fb));
    std::memset(m_dirty_count, 0, sizeof(m_dirty_count));
}

// IManagerDisplayService::AddToLayerStack, same as tsl::hlp::viAddToLayerStack.
Result Renderer::add_to_layer_stack(ViLayerStack stack) {
    const struct {
        u32 stack;
        u64 layerId;
    } in = { (u32)stack, m_layer.layer_id };

    return serviceDispatchIn(viGetSession_IManagerDisplayService(), 6000, in);
}

// libtesla: getUnderscanPixels() is non-zero only when docked
// (ult::consoleIsDocked(): apm performance mode == Boost) and the TV settings
// have underscan set.
bool Renderer::underscan_active() {
    ApmPerformanceMode mode = ApmPerformanceMode_Invalid;
    if (R_FAILED(apmGetPerformanceMode(&mode)) || mode != ApmPerformanceMode_Boost)
        return false;

    SetSysTvSettings tv;
    if (R_FAILED(setsysGetTvSettings(&tv)))
        return false;
    return tv.underscan != 0;
}

Result Renderer::update_z() {
    if (!m_layer_ok) return 0;
    bool active = underscan_active();
    if (active == m_underscan_z) return 0;

    // On failure m_underscan_z is left as is, so the next call retries.
    Result rc = viSetLayerZ(&m_layer, active ? UNDERSCAN_Z : m_normal_z);
    if (R_SUCCEEDED(rc)) m_underscan_z = active;
    return rc;
}

Result Renderer::init(bool show_in_captures, KinestopStage& stage) {
    if (m_fb_ok) return 0;
    teardown();  // clean slate if a previous attempt got partway
    m_soft_rc = 0;

    Result rc;

    stage = KinestopStage_ViInit;
    rc = viInitialize(ViServiceType_Manager);
    if (R_FAILED(rc)) goto fail;
    m_vi_ok = true;

    stage = KinestopStage_Display;
    rc = viOpenDefaultDisplay(&m_display);
    if (R_FAILED(rc)) goto fail;
    m_display_ok = true;

    stage = KinestopStage_Layer;
    __nx_vi_layer_id = 0;
    rc = viCreateManagedLayer(&m_display, static_cast<ViLayerFlags>(0), 0, &__nx_vi_layer_id);
    if (R_FAILED(rc)) goto fail;
    m_managed_ok = true;

    rc = viCreateLayer(&m_display, &m_layer);  // opens __nx_vi_layer_id
    if (R_FAILED(rc)) goto fail;
    m_layer_ok = true;

    rc = viSetLayerScalingMode(&m_layer, ViScalingMode_FitToLayer);
    if (R_FAILED(rc)) goto fail;

    {
        s32 z_max = 0;
        if (R_SUCCEEDED(viGetZOrderCountMax(&m_display, &z_max)) && z_max > 0)
            m_normal_z = z_max - 1;
        else
            m_normal_z = 254;  // libtesla falls back to 255 for itself
        m_underscan_z = underscan_active();
        rc = viSetLayerZ(&m_layer, m_underscan_z ? UNDERSCAN_Z : m_normal_z);
        if (R_FAILED(rc)) goto fail;
    }

    stage = KinestopStage_LayerStack;
    rc = add_to_layer_stack(ViLayerStack_Default);
    if (R_FAILED(rc)) goto fail;
    rc = add_to_layer_stack(ViLayerStack_Lcd);
    if (R_FAILED(rc)) goto fail;
    if (show_in_captures) {
        // Not fatal: the dots still show on screen without these.
        Result rc2 = add_to_layer_stack(ViLayerStack_Screenshot);
        if (R_FAILED(rc2)) m_soft_rc = rc2;
        rc2 = add_to_layer_stack(ViLayerStack_Recording);
        if (R_FAILED(rc2)) m_soft_rc = rc2;
    }
    m_in_captures = show_in_captures;

    stage = KinestopStage_Layer;
    rc = viSetLayerSize(&m_layer, LAYER_WIDTH, LAYER_HEIGHT);
    if (R_FAILED(rc)) goto fail;
    rc = viSetLayerPosition(&m_layer, 0.0f, 0.0f);
    if (R_FAILED(rc)) goto fail;

    stage = KinestopStage_NWindow;
    rc = nwindowCreateFromLayer(&m_window, &m_layer);
    if (R_FAILED(rc)) goto fail;
    m_nwin_ok = true;
    rc = nwindowSetDimensions(&m_window, FB_WIDTH, FB_HEIGHT);
    if (R_FAILED(rc)) goto fail;

    stage = KinestopStage_Framebuffer;
    rc = framebufferCreate(&m_fb, &m_window, FB_WIDTH, FB_HEIGHT, PIXEL_FORMAT_RGBA_4444, NUM_FBS);
    if (R_FAILED(rc)) goto fail;
    m_fb_ok = true;

    // framebufferCreate() does not clear the (heap) memory it hands to the GPU.
    std::memset(m_fb.buf, 0, (size_t)m_fb.fb_size * NUM_FBS);
    armDCacheFlush(m_fb.buf, (size_t)m_fb.fb_size * NUM_FBS);
    std::memset(m_dirty_count, 0, sizeof(m_dirty_count));

    stage = KinestopStage_None;
    return 0;

fail:
    teardown();
    return rc;
}

raster::Surface Renderer::surface_for_slot(s32 slot) {
    return raster::Surface{
        reinterpret_cast<u16*>(static_cast<u8*>(m_fb.buf) + (size_t)slot * m_fb.fb_size),
        FB_WIDTH,
        FB_HEIGHT,
    };
}

// Dequeues a slot, replaces last time's dots in it with `frame` (or with
// nothing when frame is null) and queues it. `presented` is false when no
// buffer was released within DEQUEUE_TIMEOUT_NS; nothing was touched then.
Result Renderer::draw_slot(const DotFrame* frame, bool& presented) {
    presented = false;

    // nwindowDequeueBuffer() loops on eventWait(UINT64_MAX) until a buffer
    // is free. Wait here first with a timeout so a compositor that stops
    // releasing our buffers cannot block the main loop (and with it Exit).
    // The event is remote and loaded with autoclear=false (binder.c), so
    // this wait does not consume the signal the dequeue then waits for.
    if (eventActive(&m_window.event) && R_FAILED(eventWait(&m_window.event, DEQUEUE_TIMEOUT_NS)))
        return 0;

    s32 slot = -1;
    Result rc = nwindowDequeueBuffer(&m_window, &slot, nullptr);
    if (R_FAILED(rc)) return rc;
    if (slot < 0 || (u32)slot >= NUM_FBS) {
        // Cannot happen with framebufferCreate(..., NUM_FBS); be defensive.
        return MAKERESULT(Module_Libnx, LibnxError_BadGfxDequeueBuffer);
    }

    const raster::Surface surface = surface_for_slot(slot);

    // Cleared areas are flushed right away; a dot drawn over the same GOBs
    // dirties those lines again and they are flushed again below.
    for (size_t i = 0; i < m_dirty_count[slot]; ++i) {
        raster::clear_rect(surface, m_dirty[slot][i]);
        flush_rect(surface, m_dirty[slot][i]);
    }
    m_dirty_count[slot] = 0;

    if (frame) {
        const size_t count = frame->count < dots::MAX_ANCHORS ? frame->count : dots::MAX_ANCHORS;
        for (size_t i = 0; i < count; ++i) {
            raster::Rect r = raster::draw_dot(surface, frame->centers[i].x, frame->centers[i].y, frame->style);
            if (!r.empty()) m_dirty[slot][m_dirty_count[slot]++] = r;
        }
        for (size_t i = 0; i < m_dirty_count[slot]; ++i)
            flush_rect(surface, m_dirty[slot][i]);
    }

    rc = nwindowQueueBuffer(&m_window, slot, nullptr);
    if (R_SUCCEEDED(rc)) presented = true;
    return rc;
}

Result Renderer::present(const DotFrame& frame, KinestopStage& stage, bool& presented) {
    presented = false;
    if (!m_fb_ok) {
        stage = KinestopStage_Framebuffer;
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
    }
    Result rc = draw_slot(&frame, presented);
    if (R_FAILED(rc)) stage = KinestopStage_Framebuffer;
    return rc;
}

void Renderer::exit() {
    if (m_fb_ok) {
        // Leave nothing on screen even for the instant before the layer goes.
        // Skipped (bounded wait) if the compositor is not releasing buffers.
        bool presented;
        draw_slot(nullptr, presented);
    }
    teardown();
}

// Releases whatever exists, in reverse creation order (libtesla's exit()
// order: framebufferClose, nwindowClose, viDestroyManagedLayer,
// viCloseDisplay, viExit).
void Renderer::teardown() {
    if (m_fb_ok) {
        framebufferClose(&m_fb);
        m_fb_ok = false;
    }
    if (m_nwin_ok) {
        nwindowClose(&m_window);
        m_nwin_ok = false;
    }
    if (m_managed_ok) {
        if (!m_layer_ok) {
            // viCreateLayer failed: destroy the managed layer by id.
            std::memset(&m_layer, 0, sizeof(m_layer));
            m_layer.layer_id = __nx_vi_layer_id;
        }
        viDestroyManagedLayer(&m_layer);
        m_managed_ok = false;
    }
    m_layer_ok = false;
    std::memset(&m_layer, 0, sizeof(m_layer));
    __nx_vi_layer_id = 0;
    if (m_display_ok) {
        viCloseDisplay(&m_display);
        m_display_ok = false;
    }
    if (m_vi_ok) {
        viExit();
        m_vi_ok = false;
    }
    m_in_captures = false;
    m_underscan_z = false;
    std::memset(m_dirty_count, 0, sizeof(m_dirty_count));
}

} // namespace kinestop
