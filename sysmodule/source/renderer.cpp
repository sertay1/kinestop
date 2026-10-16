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
//     space, so VI upscales it 3x (FitToLayer). KINESTOP_DEBUG_UNSCALED_LAYER
//     shrinks the layer to 960x540 VI units (1:1 in handheld) instead.
//   * Buffers are dequeued/queued by hand instead of framebufferBegin/End,
//     which call diagAbortWithResult() on failure. Here a failure just tears
//     the layer down so the main loop can retry.
//   * The dequeue has a real deadline (dequeue_bounded(), a copy of
//     nwindowDequeueBuffer()'s bookkeeping): nwindowDequeueBuffer() loops on
//     eventWait(UINT64_MAX) around an async dequeue, and since the binder
//     event is loaded with autoclear=false and never reset by libnx, it can
//     both block forever and busy-spin IPCs; it then waits the release fence
//     with no timeout. The waiting policy lives in bounded_dequeue.hpp.
//   * No vsync wait before queueing (libtesla has one): GetDisplayVsyncEvent
//     is one more per-display vi resource next to Tesla's, which treats a
//     failure to get its own as fatal; presents are paced by poll_rate_hz.
//   * Only the GOBs that were written are flushed from the data cache, not
//     the whole slot.

#include "renderer.hpp"

#include <cstring>

#include "bounded_dequeue.hpp"

extern "C" u64 __nx_vi_layer_id;

namespace kinestop {

namespace {

// How long a present waits for the compositor to release a buffer before it
// skips the frame (the main loop tries again next iteration). With two
// buffers a release is normally at most one refresh (~17 ms) away.
constexpr u64 DEQUEUE_DEADLINE_NS = 20000000ULL;  // 20 ms

// Longest single wait on the release event inside that deadline. Short, so a
// release that HOS does not signal (the event's exact semantics are not
// documented) is still picked up by the next dequeue a few ms later.
constexpr u64 EVENT_SLICE_NS = 4000000ULL;        // 4 ms

// Minimum spacing between dequeue attempts whenever a wait returned at once,
// so the loop can never turn into a busy spin of binder IPCs.
constexpr u64 MIN_RETRY_NS = 1000000ULL;          // 1 ms

// Budget for the release fence of a dequeued buffer, all fences together.
constexpr s64 FENCE_TIMEOUT_US = 20000;           // 20 ms

constexpr Result RESULT_WOULD_BLOCK = MAKERESULT(Module_LibnxBinder, LibnxBinderError_WouldBlock);

// What nvFenceWait() returns when the fence did not signal in time (libnx
// fence.c). Any other failure means the wait itself is broken.
constexpr Result RESULT_FENCE_TIMEOUT = MAKERESULT(Module_LibnxNvidia, LibnxNvidiaError_Timeout);

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
    , m_unscaled(false)
    , m_underscan_z(false)
    , m_normal_z(0)
    , m_soft_rc(0)
    , m_info{}
    , m_stats{}
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
    const s32 z = active ? UNDERSCAN_Z : m_normal_z;
    Result rc = viSetLayerZ(&m_layer, z);
    if (R_SUCCEEDED(rc)) {
        m_underscan_z = active;
        m_info.z = z;
        m_info.underscan = active;
    }
    return rc;
}

Result Renderer::init(bool show_in_captures, bool unscaled, KinestopStage& stage) {
    if (m_fb_ok) return 0;
    teardown();  // clean slate if a previous attempt got partway
    m_soft_rc = 0;
    m_info = LayerInfo{};

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
        s32 z_min = 0, z_max = 0;
        m_info.z_min = R_SUCCEEDED(viGetZOrderCountMin(&m_display, &z_min)) ? z_min : -1;
        if (R_SUCCEEDED(viGetZOrderCountMax(&m_display, &z_max)) && z_max > 0) {
            m_normal_z = z_max - 1;
            m_info.z_max = z_max;
        } else {
            m_normal_z = 254;  // libtesla falls back to 255 for itself
            m_info.z_max = -1;
        }
        m_underscan_z = underscan_active();
        m_info.z = m_underscan_z ? UNDERSCAN_Z : m_normal_z;
        m_info.underscan = m_underscan_z;
        rc = viSetLayerZ(&m_layer, m_info.z);
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
    m_info.width = unscaled ? UNSCALED_LAYER_WIDTH : LAYER_WIDTH;
    m_info.height = unscaled ? UNSCALED_LAYER_HEIGHT : LAYER_HEIGHT;
    rc = viSetLayerSize(&m_layer, m_info.width, m_info.height);
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

    m_unscaled = unscaled;
    m_info.in_captures = show_in_captures;
    m_info.unscaled = unscaled;
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

// nwindowDequeueBuffer() with a deadline. Same NWindow bookkeeping (mutex,
// cur_slot, slots_requested/bqRequestBuffer, cancel on failure), but the
// waiting follows bounded_dequeue.hpp (host-tested in tests/test_dequeue.cpp):
//   * the async dequeue is retried until DEQUEUE_DEADLINE_NS, waiting on the
//     release event in slices (never longer than what is left), resetting it
//     before each wait, and never faster than once per MIN_RETRY_NS;
//   * the release fences share one FENCE_TIMEOUT_US budget, and a buffer
//     whose fence does not signal in time is cancelled again (with that
//     fence, so the next dequeue of the slot waits for it). A fence wait that
//     fails with anything but a timeout is counted and the buffer is used
//     anyway, as libnx does.
// Returns 0 with out_slot >= 0 (the slot is now nw->cur_slot and must be
// queued or cancelled), 0 with out_slot = -1 when it gave up (counted in
// m_stats.dequeue_timeouts), or a failure Result.
namespace {

// The libnx side of bounded::dequeue_until_deadline() / wait_fences().
// Called with nw->mutex held.
struct DequeueOps {
    NWindow* nw;
    s32 slot;
    NvMultiFence fence;
    Result rc;        // last bqDequeueBuffer() Result
    Result fence_rc;  // nvFenceWait() Result when it failed with something other than a timeout

    u64 now_ns() { return armTicksToNs(armGetSystemTick()); }

    bounded::Try try_dequeue() {
        rc = bqDequeueBuffer(&nw->bq, true, nw->width, nw->height, nw->format, nw->usage, &slot, &fence);
        if (R_SUCCEEDED(rc)) return bounded::Try::Got;
        return R_VALUE(rc) == RESULT_WOULD_BLOCK ? bounded::Try::WouldBlock : bounded::Try::Failed;
    }

    bool has_event() { return eventActive(&nw->event); }
    void clear_event() { eventClear(&nw->event); }
    bool wait_event(u64 ns) { return R_SUCCEEDED(eventWait(&nw->event, ns)); }
    void sleep_ns(u64 ns) { svcSleepThread((s64)ns); }

    bounded::Fence fence_wait(u32 index, s32 timeout_us) {
        const Result r = nvFenceWait(&fence.fences[index], timeout_us);
        if (R_SUCCEEDED(r)) return bounded::Fence::Signalled;
        if (R_VALUE(r) == RESULT_FENCE_TIMEOUT) return bounded::Fence::TimedOut;
        fence_rc = r;
        return bounded::Fence::Failed;
    }
};

constexpr bounded::Timing DEQUEUE_TIMING = {DEQUEUE_DEADLINE_NS, EVENT_SLICE_NS, MIN_RETRY_NS};

} // namespace

Result Renderer::dequeue_bounded(s32& out_slot) {
    out_slot = -1;
    NWindow* nw = &m_window;

    mutexLock(&nw->mutex);

    if (!nw->slots_configured || nw->cur_slot >= 0) {
        mutexUnlock(&nw->mutex);
        return MAKERESULT(Module_Libnx, LibnxError_BadGfxDequeueBuffer);
    }

    DequeueOps ops;
    ops.nw = nw;
    ops.slot = -1;
    std::memset(&ops.fence, 0, sizeof(ops.fence));
    ops.rc = 0;
    ops.fence_rc = 0;

    bounded::Counters counters = {0, 0};
    const bounded::Outcome outcome = bounded::dequeue_until_deadline(ops, DEQUEUE_TIMING, counters);
    m_stats.would_blocks += counters.would_blocks;
    m_stats.event_wakes += counters.event_wakes;

    if (outcome == bounded::Outcome::TimedOut) {
        m_stats.dequeue_timeouts++;
        mutexUnlock(&nw->mutex);
        return 0;
    }
    if (outcome == bounded::Outcome::Failed) {
        mutexUnlock(&nw->mutex);
        return ops.rc;
    }

    const s32 slot = ops.slot;
    if (slot < 0 || (u32)slot >= NUM_FBS) {
        // Cannot happen with framebufferCreate(..., NUM_FBS); be defensive.
        if (slot >= 0 && slot < 64) bqCancelBuffer(&nw->bq, slot, &ops.fence);
        mutexUnlock(&nw->mutex);
        return MAKERESULT(Module_Libnx, LibnxError_BadGfxDequeueBuffer);
    }

    if (!(nw->slots_requested & (1UL << slot))) {
        Result rc = bqRequestBuffer(&nw->bq, slot, NULL);
        if (R_FAILED(rc)) {
            bqCancelBuffer(&nw->bq, slot, &ops.fence);
            mutexUnlock(&nw->mutex);
            return rc;
        }
        nw->slots_requested |= 1UL << slot;
    }

    const u32 num_fences = ops.fence.num_fences < 4 ? ops.fence.num_fences : 4;
    const bounded::Fence fence_state = bounded::wait_fences(ops, num_fences, FENCE_TIMEOUT_US);
    if (fence_state == bounded::Fence::Failed) {
        // The wait itself failed (not a timeout). Do what libnx's
        // nwindowDequeueBuffer() does - ignore it and use the buffer - but
        // count it and keep the Result for the diagnostics line.
        m_stats.fence_errors++;
        m_stats.last_fence_rc = ops.fence_rc;
    } else if (fence_state == bounded::Fence::TimedOut) {
        // Abandon this present. The slot goes back to the queue with the
        // fence still pending; nw->cur_slot was never set.
        m_stats.fence_timeouts++;
        m_stats.dequeue_timeouts++;
        const Result rc = bqCancelBuffer(&nw->bq, slot, &ops.fence);
        mutexUnlock(&nw->mutex);
        return rc;  // 0 = skipped; a failed cancel leaves the queue unknown: re-create
    }

    nw->cur_slot = slot;
    out_slot = slot;
    mutexUnlock(&nw->mutex);
    return 0;
}

// Dequeues a slot (bounded), replaces last time's dots in it with `frame` (or
// with nothing when frame is null) and queues it. `presented` is false when no
// usable buffer was handed out in time; nothing was touched then.
Result Renderer::draw_slot(const DotFrame* frame, bool& presented) {
    presented = false;

    s32 slot = -1;
    Result rc = dequeue_bounded(slot);
    if (R_FAILED(rc) || slot < 0) return rc;

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
    if (R_FAILED(rc)) {
        // Hand the slot back so the window stays consistent (cur_slot = -1);
        // otherwise every later dequeue fails with BadGfxDequeueBuffer. The
        // dirty list above already describes what is in the slot now.
        nwindowCancelBuffer(&m_window, slot, nullptr);
        return rc;
    }
    presented = true;
    return 0;
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
        // Skipped (bounded) if the compositor is not releasing buffers.
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
    m_unscaled = false;
    m_underscan_z = false;
    std::memset(m_dirty_count, 0, sizeof(m_dirty_count));
}

} // namespace kinestop
