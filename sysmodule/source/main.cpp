// Kinestop sysmodule - draws the motion-cue dots on its own VI layer.
//
// Original Kinestop by SertAy (https://github.com/sertay1/Kinestop). The
// sysmodule/overlay split is a modification of the original project: this
// process owns the IMU, the complementary filter and a VI layer with a
// 640x360 framebuffer (320x180 in the Quarter2x debug mode); the
// Tesla/Ultrahand overlay is only a settings panel that launches it with
// pmshellLaunchProgram and talks to it over the "kinestop" service
// (common/kinestop_ipc.h).
//
// Threads (all on core 3, all BELOW nx-ovlloader's main/render thread,
// which runs at priority 49 on core 3, so a Tesla menu always preempts us):
//   main   - sample IMU, filter, compute dot positions, present (paced to
//            poll_rate_hz); owns the renderer and the sensor. Priority 59
//            (kinestop.json main_thread_priority).
//   ipc    - serves the "kinestop" service; touches only g_state. Priority 58.
//   logger - drains the log ring buffer to the SD card (log.cpp); the only
//            thread that does SD I/O after startup. Priority 62.

#include <switch.h>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "../../common/kinestop_ipc.h"
#include "../../common/config.hpp"
#include "../../common/dots.hpp"
#include "../../common/math_types.hpp"
#include "filter.hpp"
#include "frame_scale.hpp"
#include "ipc_server.h"
#include "log.hpp"
#include "renderer.hpp"
#include "sensor.hpp"

#ifndef APP_VERSION
#define APP_VERSION "2.0.0-test.3"
#endif

// ---------------------------------------------------------------------------
// Memory budget (System pool; everything here is ours to pay for).
//
// The inner heap is a static array in .bss. Reading libnx (nx/source), these
// are the only allocations made from it in this process:
//   hidInitializeSevenSixAxisSensor()
//                         console IMU state buffer handed to hid as
//                         transfer memory, 0x80000 B,
//                         __libnx_aligned_alloc(0x1000)             524,288 B
//                         (allocated first, by SensorManager::init(), and
//                         kept for the life of the process, so the
//                         framebuffers below come and go after it and
//                         cannot fragment around it)
//   framebufferCreate()   640x360 RGBA4444: stride 1280 B, height padded to
//                         384 rows -> 491,520 B/slot x 2 =       983,040 B
//                         (Quarter2x debug mode: 320x180, stride 640 B,
//                         256 rows -> 163,840 B/slot x 2 = 327,680 B)
//                         (__libnx_aligned_alloc(0x1000), up to 4 KiB lost
//                         to alignment)
//   nvInitialize()        transfer memory, __nx_nv_transfermem_size
//                         (0x15000, libtesla's value)                86,016 B
//                         (tmemCreate -> __libnx_aligned_alloc(0x1000))
//   threadCreate()        IPC thread: 8 KiB stack + 576 B struct _reent
//                         + TLS image, page-aligned allocation      ~16,384 B
//                         logger thread (log.cpp): 12 KiB stack + the
//                         same, page-aligned                         ~16,384 B
//   newlib stdio          FILE glue (4 x 184 B) + one BUFSIZ buffer
//                         per open file (config.ini at startup; the
//                         log uses open/write/close, no FILE), fd
//                         table entries, strtof bigints             ~4,096 B
//   fsdev                 cwd buffer disabled (__nx_fsdev_support_cwd)   0 B
// vi, binder/parcels, nvmap, nvfence, hid and sm allocate nothing from the
// heap (parcels live on the stack; hid maps its 0x40000 shared memory into
// address space owned by hid). Sum ~1.61 MiB; the rest of the 1.75 MiB is
// slack for malloc headers, fragmentation across layer re-creation
// (framebuffers + nv transfer memory are freed and re-allocated on every
// suspend/resume, blink cycle or layer re-creation) and the 4 KiB memalign
// over-allocations.
// Not heap: main thread stack (npdm main_thread_stack_size 0x4000), the
// 12 KiB log ring buffer (.bss).
// ---------------------------------------------------------------------------
#define INNER_HEAP_SIZE 0x1C0000  // 1.75 MiB (1.25 MiB + 0x80000 for SevenSixAxis)

#define IPC_THREAD_STACK_SIZE 0x2000
#define IPC_THREAD_PRIORITY   58  // below nx-ovlloader's 49; main thread is 59
#define IPC_THREAD_CORE       3
#define IPC_MAX_SESSIONS      4

extern "C" {

u32 __nx_applet_type = AppletType_None;
u32 __nx_fs_num_sessions = 1;
bool __nx_fsdev_support_cwd = false;

// Same as libtesla (tesla.hpp sets __nx_nv_transfermem_size = 0x15000 for
// every overlay). We only use nvmap + nvhost-ctrl (no GPU channels), exactly
// like a Tesla overlay's framebuffer.
u32 __nx_nv_transfermem_size = 0x15000;

// libtesla leaves this at NvServiceType_Auto, which libnx resolves to
// NvServiceType_System ("nvdrv:s") for AppletType_None, i.e. what every
// overlay running inside nx-ovlloader uses. Spelled out here so it matches
// service_access in kinestop.json.
NvServiceType __nx_nv_service_type = NvServiceType_System;

void __libnx_initheap(void) {
    static char inner_heap[INNER_HEAP_SIZE] __attribute__((aligned(0x1000)));

    extern char* fake_heap_start;
    extern char* fake_heap_end;

    fake_heap_start = inner_heap;
    fake_heap_end   = inner_heap + sizeof(inner_heap);
}

void __appInit(void) {
    Result rc = smInitialize();
    if (R_FAILED(rc))
        diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_SM));

    // set:sys stays open: the renderer reads TV underscan settings from it.
    rc = setsysInitialize();
    if (R_SUCCEEDED(rc)) {
        SetSysFirmwareVersion fw;
        if (R_SUCCEEDED(setsysGetFirmwareVersion(&fw)))
            hosversionSet(MAKEHOSVERSION(fw.major, fw.minor, fw.micro));
    }

    rc = fsInitialize();
    if (R_FAILED(rc))
        diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_FS));
    fsdevMountSdmc();

    // Failures here are reported through KinestopStatus instead of aborting.
    hidInitialize();
    apmInitialize();  // docked detection for the underscan Z rule (as libtesla)
}

void __appExit(void) {
    apmExit();
    hidExit();
    fsdevUnmountAll();
    fsExit();
    setsysExit();
    smExit();
}

} // extern "C"

namespace {

using kinestop::Config;
using kinestop::Orientation;

// State shared between the render loop and the IPC thread.
struct SharedState {
    Mutex mutex;
    Config config;
    u32 config_gen;          // bumped on every SetConfig/Calibrate
    Orientation raw;         // latest filter output, before calibration
    KinestopStatus status;
    bool exit_requested;
    bool suspended;          // SetSuspended: layer torn down, sampling continues
    UEvent wake;             // wakes the render loop early (Exit, SetConfig)
};

SharedState g_state;
IpcServer g_server;
Thread g_ipc_thread;

bool exit_requested() {
    mutexLock(&g_state.mutex);
    bool r = g_state.exit_requested;
    mutexUnlock(&g_state.mutex);
    return r;
}

void request_exit() {
    mutexLock(&g_state.mutex);
    g_state.exit_requested = true;
    mutexUnlock(&g_state.mutex);
    ueventSignal(&g_state.wake);
}

// --- IPC ------------------------------------------------------------------

Result ipc_handler(void* userdata, const IpcServerRequest* r, u8* out_data, size_t* out_size) {
    (void)userdata;

    switch (r->data.cmdId) {
        case KinestopCmd_GetApiVersion: {
            const u32 version = KINESTOP_API_VERSION;
            std::memcpy(out_data, &version, sizeof(version));
            *out_size = sizeof(version);
            return 0;
        }

        case KinestopCmd_GetStatus: {
            KinestopStatus status;
            mutexLock(&g_state.mutex);
            status = g_state.status;
            mutexUnlock(&g_state.mutex);
            std::memcpy(out_data, &status, sizeof(status));
            *out_size = sizeof(status);
            return 0;
        }

        case KinestopCmd_SetConfig: {
            if (!r->data.ptr || r->data.size < sizeof(KinestopWireConfig))
                return MAKERESULT(Module_Libnx, LibnxError_BadInput);
            KinestopWireConfig wire;
            std::memcpy(&wire, r->data.ptr, sizeof(wire));

            mutexLock(&g_state.mutex);
            g_state.config.from_wire(wire);  // sanitizes
            g_state.config_gen++;
            mutexUnlock(&g_state.mutex);
            ueventSignal(&g_state.wake);
            return 0;
        }

        case KinestopCmd_Exit:
            // The reply goes out when this returns (ipc_server replies after
            // the handler); the render loop then cleans up and main returns.
            kinestop::log::write("Exit requested over IPC");
            kinestop::log::flush();  // the overlay kills us 0.5 s after Exit if we hang
            request_exit();
            return 0;

        case KinestopCmd_SetSuspended: {
            if (!r->data.ptr || r->data.size < sizeof(u8))
                return MAKERESULT(Module_Libnx, LibnxError_BadInput);
            const bool suspend = *static_cast<const u8*>(r->data.ptr) != 0;
            // Acted on (and logged) by the render loop on its next tick.
            mutexLock(&g_state.mutex);
            g_state.suspended = suspend;
            mutexUnlock(&g_state.mutex);
            ueventSignal(&g_state.wake);
            return 0;
        }

        case KinestopCmd_Calibrate: {
            KinestopCalibration cal;
            mutexLock(&g_state.mutex);
            cal.pitch_offset = g_state.raw.pitch_deg;
            cal.roll_offset  = g_state.raw.roll_deg;
            g_state.config.calibration.pitch_offset = cal.pitch_offset;
            g_state.config.calibration.roll_offset  = cal.roll_offset;
            g_state.config_gen++;
            mutexUnlock(&g_state.mutex);
            ueventSignal(&g_state.wake);
            std::memcpy(out_data, &cal, sizeof(cal));
            *out_size = sizeof(cal);
            return 0;
        }

        default:
            return MAKERESULT(Module_Libnx, LibnxError_NotFound);
    }
}

void ipc_thread_main(void* arg) {
    (void)arg;
    while (!exit_requested()) {
        Result rc = ipcServerProcess(&g_server, ipc_handler, nullptr, UINT64_MAX);
        // KERNELRESULT(Cancelled) is main asking us to stop; closed client
        // sessions also surface here and are normal. Back off a little on
        // anything else so a persistent error cannot spin a core.
        if (R_FAILED(rc) && rc != KERNELRESULT(Cancelled) && !exit_requested())
            svcSleepThread(1000000ULL);
    }
}

// --- error bookkeeping ----------------------------------------------------

struct ErrorLatch {
    u32 stage = 0;
    Result rc = 0;
};

ErrorLatch g_last_logged;

void record_error(KinestopStage stage, Result rc) {
    mutexLock(&g_state.mutex);
    g_state.status.last_error_stage = stage;
    g_state.status.last_error_rc = rc;
    mutexUnlock(&g_state.mutex);

    // Each distinct (stage, rc) is logged once in a row: a 1 Hz retry that
    // keeps failing the same way stays quiet.
    if (g_last_logged.stage != (u32)stage || g_last_logged.rc != rc) {
        g_last_logged.stage = stage;
        g_last_logged.rc = rc;
        kinestop::log::write("error: stage %u rc 0x%X (%u-%04u)", (unsigned)stage, (unsigned)rc,
                             (unsigned)(2000 + R_MODULE(rc)), (unsigned)R_DESCRIPTION(rc));
    }
}

// Clears the reported error once the stage that failed has recovered, so the
// overlay's status line shows a current problem rather than a stale one
// (e.g. a sensor that started late, or a VI init that succeeded on retry).
void clear_error_if(bool (*matches)(u32 stage)) {
    bool cleared = false;
    mutexLock(&g_state.mutex);
    if (g_state.status.last_error_rc != 0 && matches(g_state.status.last_error_stage)) {
        g_state.status.last_error_stage = KinestopStage_None;
        g_state.status.last_error_rc = 0;
        cleared = true;
    }
    mutexUnlock(&g_state.mutex);
    if (cleared) g_last_logged = ErrorLatch{};  // a recurrence is logged again
}

bool is_vi_stage(u32 stage) {
    return stage >= KinestopStage_ViInit && stage <= KinestopStage_Framebuffer;
}

bool is_sensor_stage(u32 stage) {
    return stage == KinestopStage_Sensor;
}

bool is_layer_stage(u32 stage) {
    return stage == KinestopStage_Layer;
}

// --- frame building -------------------------------------------------------

// Everything a frame depends on. When this is unchanged, the queued buffer
// already shows the right picture and presenting again is skipped.
struct FrameKey {
    u32 config_gen;
    s32 qx, qy;  // shared dot offset in 1/frame_scale::OFFSET_QUANTUM framebuffer pixels
    bool operator!=(const FrameKey& o) const {
        return config_gen != o.config_gen || qx != o.qx || qy != o.qy;
    }
};

static_assert(kinestop::frame_scale::LOGICAL_WIDTH == kinestop::dots::LOGICAL_WIDTH,
              "frame_scale.hpp and dots.hpp disagree on the logical width");

// Logical px -> framebuffer px, offset quantization and the dot core/ring
// sizes live in frame_scale.hpp (host-tested in tests/test_scale.cpp).
void build_frame(const Config& cfg, const FrameKey& key, float fb_scale, kinestop::DotFrame& frame) {
    namespace dots = kinestop::dots;
    namespace fs = kinestop::frame_scale;

    // Back to logical px from the quantized framebuffer offset.
    const float dx = fs::dequantize_offset(key.qx, fb_scale);
    const float dy = fs::dequantize_offset(key.qy, fb_scale);

    dots::Point anchors[dots::MAX_ANCHORS];
    frame.count = dots::get_anchors(cfg.visual.style, cfg.visual.density, anchors, dots::MAX_ANCHORS);

    for (size_t i = 0; i < frame.count; ++i) {
        float px = anchors[i].x + dx;
        float py = anchors[i].y + dy;

        // Per-dot half of the original draw_motion_dots(): keep every dot on
        // screen when not free floating.
        if (!cfg.visual.free_floating) {
            px = kinestop::clamp(px, 10.0f, 1270.0f);
            py = kinestop::clamp(py, 10.0f, 710.0f);
        }

        frame.centers[i].x = fs::center_fb(px, fb_scale);
        frame.centers[i].y = fs::center_fb(py, fb_scale);
    }

    int theme_idx = cfg.visual.color;
    theme_idx = theme_idx < 0 ? 0 : (theme_idx >= (int)dots::NUM_THEMES ? (int)dots::NUM_THEMES - 1 : theme_idx);
    const dots::Theme& theme = dots::THEMES[theme_idx];

    int alpha_255 = (int)(cfg.visual.opacity * 255.0f);
    alpha_255 = alpha_255 < 25 ? 25 : (alpha_255 > 255 ? 255 : alpha_255);
    const int border_alpha = alpha_255 + 50 > 255 ? 255 : alpha_255 + 50;

    // Core: ceil of the original's radius, never below 0.5 fb px. Ring: a
    // solid 2-logical-px band, never below 1 fb px (see frame_scale.hpp).
    frame.style.core_radius = fs::core_radius_fb(dots::dot_radius(cfg), fb_scale);
    frame.style.ring_width = fs::ring_width_fb(fb_scale);
    frame.style.core = {theme.cr, theme.cg, theme.cb, (u8)alpha_255};
    frame.style.border = {theme.br, theme.bg, theme.bb, (u8)border_alpha};
}

int clamp_rate(int hz) {
    return hz < 10 ? 10 : (hz > 120 ? 120 : hz);
}

// --- diagnostics ----------------------------------------------------------

// log::write() formats with vsniprintf (no floating point, keeps the binary
// small), so floats are logged as fixed point text.
struct FixedText {
    char text[16];
};

FixedText fixed(float v, int decimals) {
    FixedText f;
    if (!std::isfinite(v)) {
        std::strcpy(f.text, "nan");
        return f;
    }
    long scale = 1;
    for (int i = 0; i < decimals; ++i) scale *= 10;
    const float scaled = v * (float)scale;
    if (scaled > 2e9f || scaled < -2e9f) {
        std::strcpy(f.text, scaled > 0 ? "big" : "-big");
        return f;
    }
    long q = lroundf(scaled);
    const bool neg = q < 0;
    if (neg) q = -q;
    sniprintf(f.text, sizeof(f.text), "%s%ld.%0*ld", neg ? "-" : "", q / scale, decimals, q % scale);
    return f;
}

// Seconds since start with one decimal, for log timestamps.
FixedText seconds(u64 ms) {
    FixedText f;
    sniprintf(f.text, sizeof(f.text), "%lu.%lu", (unsigned long)(ms / 1000), (unsigned long)((ms % 1000) / 100));
    return f;
}

} // namespace

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    const u64 start_tick = armGetSystemTick();

    kinestop::log::init();  // starts the logger thread
    kinestop::log::write("Kinestop sysmodule " APP_VERSION " started (hos %u.%u.%u)",
                         HOSVER_MAJOR(hosversionGet()), HOSVER_MINOR(hosversionGet()), HOSVER_MICRO(hosversionGet()));

    Config config;
    if (!config.load())
        kinestop::log::write("config.ini not found, using defaults");
    if (config.general.debug_flags)
        kinestop::log::write("debug_flags %d (blink %d, unscaled %d, verbose %d, no_suspend %d, scaled_small %d)",
                             config.general.debug_flags,
                             (config.general.debug_flags & KINESTOP_DEBUG_BLINK_TEST) ? 1 : 0,
                             (config.general.debug_flags & KINESTOP_DEBUG_UNSCALED_LAYER) ? 1 : 0,
                             (config.general.debug_flags & KINESTOP_DEBUG_VERBOSE_LOG) ? 1 : 0,
                             (config.general.debug_flags & KINESTOP_DEBUG_NO_SUSPEND) ? 1 : 0,
                             (config.general.debug_flags & KINESTOP_DEBUG_SCALED_SMALL) ? 1 : 0);

    // general.enabled is not checked: it is the overlay's master-toggle
    // state. Being launched at all (overlay, boot2.flag, or a sysmodule
    // manager such as ovl-sysmodules via toolbox.json) means "show dots";
    // the way to not have them at boot is to remove boot2.flag.

    std::memset(&g_state.status, 0, sizeof(g_state.status));
    g_state.status.api_version = KINESTOP_API_VERSION;
    g_state.config = config;
    g_state.config_gen = 1;
    g_state.exit_requested = false;
    g_state.suspended = false;  // a fresh process always starts unsuspended
    ueventCreate(&g_state.wake, true);

    Result rc = ipcServerInit(&g_server, KINESTOP_SERVICE_NAME, IPC_MAX_SESSIONS);
    if (R_FAILED(rc)) {
        // Without the service nobody could stop us gracefully or change
        // settings; do not leave an unmanageable layer on screen.
        kinestop::log::write("ipcServerInit failed: rc 0x%X, exiting", (unsigned)rc);
        kinestop::log::exit();
        return 0;
    }

    rc = threadCreate(&g_ipc_thread, ipc_thread_main, nullptr, nullptr, IPC_THREAD_STACK_SIZE,
                      IPC_THREAD_PRIORITY, IPC_THREAD_CORE);
    if (R_SUCCEEDED(rc)) {
        rc = threadStart(&g_ipc_thread);
        if (R_FAILED(rc)) threadClose(&g_ipc_thread);
    }
    if (R_FAILED(rc)) {
        kinestop::log::write("IPC thread failed: rc 0x%X, exiting", (unsigned)rc);
        ipcServerExit(&g_server);
        kinestop::log::exit();
        return 0;
    }

    // The sensor goes before the renderer: SevenSixAxis takes a long-lived
    // 0x80000-byte block of the heap, the framebuffers come and go after it.
    kinestop::SensorManager sensor;
    rc = sensor.init();
    if (R_FAILED(rc)) record_error(KinestopStage_Sensor, rc);
    mutexLock(&g_state.mutex);
    g_state.status.seven_init_rc = sensor.seven_init_rc();
    mutexUnlock(&g_state.mutex);

    kinestop::ComplementaryFilter filter;
    kinestop::Renderer renderer;

    Config local = config;
    u32 local_gen = 0;
    Orientation ori{};
    FrameKey last_key{};
    bool have_key = false;
    u32 frames = 0;
    u32 iterations = 0;
    u32 layer_inits = 0;

    const u64 tick_freq = armGetSystemTickFreq();
    const u64 retry_ticks = tick_freq;        // VI init retry period: 1 s
    const u64 slow_ticks = tick_freq;         // underscan / sensor restart: 1 s
    const u64 sensor_fresh_ticks = tick_freq / 2;
    const u64 sparse_log_ticks = tick_freq * 10;
    constexpr u64 DENSE_LOG_MS = 60000;       // 1 Hz diagnostics for the first minute
    constexpr u64 BLINK_PERIOD_MS = 10000;    // KINESTOP_DEBUG_BLINK_TEST: layer down
    constexpr u64 BLINK_UP_MS = 7000;         // for the last 3 s of every 10 s

    auto ms_since_start = [&](u64 tick) -> u64 { return armTicksToNs(tick - start_tick) / 1000000ULL; };

    u64 next_vi_retry = 0;
    u64 next_slow = start_tick + slow_ticks;
    u64 next_second = start_tick + tick_freq;
    u64 next_sparse_log = 0;
    u64 last_sample_tick = 0;
    bool have_sample = false;
    bool had_sensor_ok = false;
    bool z_failed = false;

    bool suspended = false;
    bool blink_down = false;
    KinestopSensorSource source = KinestopSensor_None;
    s8 npad_index = -1;                       // npad handle of the last NpadSixAxis sample
    u32 seen_sources = 0;                     // bit per KinestopSensorSource already logged

    // Per-second accumulators and the values of the last full second.
    u32 advance_acc = 0;
    u32 switches_acc = 0;
    u32 max_present_acc = 0;
    u32 max_busy_acc = 0;                     // longest iteration before the pacing wait (incl. log I/O)
    u32 advance_1s = 0;
    u32 switches_1s = 0;
    u32 max_present_1s = 0;
    u32 max_busy_1s = 0;
    kinestop::SensorWindow window_1s{0, 0, -1};

    bool sensor_ok = false;

    // One diagnostics line: everything needed to tell a frozen sensor, a
    // stalled present and a layer that is not composited apart. The newest
    // values of BOTH sources are logged (a7/g7 SevenSixAxis, anN/gn npad
    // handle N), whichever is current, so a log shows whether npad carries
    // real IMU data. STUB: the npad values are the placeholder pattern;
    // SUSPECT: the sensor is ignoring that npad handle as fake (sensor.hpp).
    auto log_diag = [&](u64 now) {
        const kinestop::PresentStats& ps = renderer.stats();
        const kinestop::SensorRaw& raw = sensor.raw();
        const bool stub = raw.npad_index >= 0 && raw.npad_accel.x == 0.0f && raw.npad_accel.y == 0.0f &&
                          raw.npad_accel.z == -1.0f && raw.npad_gyro.x == 0.0f &&
                          raw.npad_gyro.y == 0.0f && raw.npad_gyro.z == 0.0f;
        const char* layer = renderer.ready() ? "up" : suspended ? "suspended" : blink_down ? "blink" : "down";
        kinestop::log::write(
            "diag t=%s it=%u src=%s ok=%d adv=%u s7=%u np=%u/%d sw=%u a7=%s,%s,%s g7=%s,%s,%s an%d=%s,%s,%s "
            "gn=%s,%s,%s%s%s p=%s r=%s "
            "fr=%u dqto=%u fto=%u fe=%u/0x%X wb=%u ew=%u maxp=%uus maxit=%uus logw=%uus lwr=%uus layer=%s",
            seconds(ms_since_start(now)).text, (unsigned)iterations,
            kinestop::SensorManager::source_name(source), sensor_ok ? 1 : 0, (unsigned)advance_1s,
            (unsigned)window_1s.seven_advance, (unsigned)window_1s.npad_advance, (int)window_1s.npad_index,
            (unsigned)switches_1s,
            fixed(raw.seven_accel.x, 3).text, fixed(raw.seven_accel.y, 3).text, fixed(raw.seven_accel.z, 3).text,
            fixed(raw.seven_gyro.x, 3).text, fixed(raw.seven_gyro.y, 3).text, fixed(raw.seven_gyro.z, 3).text,
            (int)raw.npad_index,
            fixed(raw.npad_accel.x, 3).text, fixed(raw.npad_accel.y, 3).text, fixed(raw.npad_accel.z, 3).text,
            fixed(raw.npad_gyro.x, 3).text, fixed(raw.npad_gyro.y, 3).text, fixed(raw.npad_gyro.z, 3).text,
            stub ? " STUB" : "", raw.npad_suspect ? " SUSPECT" : "",
            fixed(ori.pitch_deg, 1).text, fixed(ori.roll_deg, 1).text,
            (unsigned)frames, (unsigned)ps.dequeue_timeouts, (unsigned)ps.fence_timeouts,
            (unsigned)ps.fence_errors, (unsigned)ps.last_fence_rc,
            (unsigned)ps.would_blocks, (unsigned)ps.event_wakes, (unsigned)max_present_1s,
            (unsigned)max_busy_1s, kinestop::log::take_max_write_us(), kinestop::log::take_max_drain_us(), layer);
    };

    // Takes the layer down on purpose (suspend, blink test, settings change).
    auto layer_down = [&](u64 now, const char* why) {
        renderer.exit();
        have_key = false;
        kinestop::log::write("t=%s layer down (%s)", seconds(ms_since_start(now)).text, why);
        log_diag(now);
    };

    kinestop::log::write("running");

    while (true) {
        const u64 frame_start = armGetSystemTick();
        const u64 t_ms = ms_since_start(frame_start);

        // Heartbeat first, so a loop stuck further down (e.g. in present)
        // shows up as a counter that stops. Then snapshot what the IPC
        // thread may have changed.
        ++iterations;
        mutexLock(&g_state.mutex);
        g_state.status.loop_iterations = iterations;
        const bool quit = g_state.exit_requested;
        const bool want_suspend = g_state.suspended;
        if (g_state.config_gen != local_gen) {
            local = g_state.config;
            local_gen = g_state.config_gen;
        }
        mutexUnlock(&g_state.mutex);
        if (quit) break;

        const u32 dbg = (u32)local.general.debug_flags;

        // --- deliberate layer-down states -----------------------------------
        if (want_suspend != suspended) {
            suspended = want_suspend;
            kinestop::log::write("t=%s %s", seconds(t_ms).text,
                                 suspended ? "suspend: tearing the layer down, sampling continues"
                                           : "resume: re-creating the layer");
            if (!suspended) next_vi_retry = 0;
        }

        const bool blink_now = (dbg & KINESTOP_DEBUG_BLINK_TEST) && (t_ms % BLINK_PERIOD_MS) >= BLINK_UP_MS;
        if (blink_now != blink_down) {
            blink_down = blink_now;
            kinestop::log::write("t=%s blink: layer %s", seconds(t_ms).text, blink_down ? "down for 3 s" : "back up");
            if (!blink_down) next_vi_retry = 0;
        }

        const bool want_layer = !suspended && !blink_down;

        // --- IMU -> complementary filter ------------------------------------
        kinestop::SensorSample smp;
        if (sensor.sample(smp)) {
            if (smp.source != source || smp.npad_index != npad_index) {
                // A change of source, or of npad handle, is a switch.
                if (source != KinestopSensor_None) switches_acc++;
                npad_index = smp.npad_index;
                if (!(seen_sources & (1u << smp.source))) {
                    seen_sources |= 1u << smp.source;
                    kinestop::log::write("t=%s first fresh sample from %s", seconds(t_ms).text,
                                         kinestop::SensorManager::source_name(smp.source));
                }
                source = smp.source;
            }
            // dt since the previous fresh sample, so gyro integration covers
            // ticks without one (clamped inside update()).
            const float dt = have_sample ? (float)armTicksToNs(frame_start - last_sample_tick) / 1e9f : 0.0f;
            ori = filter.update(smp.accel, smp.gyro, dt, local);
            last_sample_tick = frame_start;
            have_sample = true;
            advance_acc += smp.advance;
        }
        sensor_ok = have_sample && (frame_start - last_sample_tick) < sensor_fresh_ticks;
        if (sensor_ok && !had_sensor_ok) clear_error_if(is_sensor_stage);
        had_sensor_ok = sensor_ok;

        // --- layer lifecycle ------------------------------------------------
        if (!want_layer && renderer.ready())
            layer_down(frame_start, suspended ? "suspended" : "blink test");

        // Layer stack membership and sizes are fixed when the layer is
        // created. UNSCALED_LAYER wins over SCALED_SMALL.
        const kinestop::LayerMode want_mode =
            (dbg & KINESTOP_DEBUG_UNSCALED_LAYER) ? kinestop::LayerMode::Quarter1x
            : (dbg & KINESTOP_DEBUG_SCALED_SMALL) ? kinestop::LayerMode::Quarter2x
                                                  : kinestop::LayerMode::Full2x;
        if (renderer.ready() && (renderer.in_captures() != local.visual.show_in_captures ||
                                 renderer.mode() != want_mode)) {
            kinestop::log::write("show_in_captures %d / mode %s changed, re-creating layer",
                                 local.visual.show_in_captures ? 1 : 0, kinestop::layer_mode_name(want_mode));
            layer_down(frame_start, "settings");
            next_vi_retry = 0;
        }

        if (want_layer && !renderer.ready() && frame_start >= next_vi_retry) {
            KinestopStage stage = KinestopStage_None;
            rc = renderer.init(local.visual.show_in_captures, want_mode, stage);
            z_failed = false;  // a new layer gets its Z from init()
            if (R_FAILED(rc)) {
                record_error(stage, rc);
                next_vi_retry = frame_start + retry_ticks;
            } else {
                layer_inits++;
                const kinestop::LayerInfo& li = renderer.info();
                kinestop::log::write("t=%s layer up #%u (%s): %dx%d VI units at 0,0 (fb %ux%u), z %d (min %d max %d), "
                                     "underscan %d, captures %d",
                                     seconds(t_ms).text, (unsigned)layer_inits, kinestop::layer_mode_name(li.mode),
                                     (int)li.width, (int)li.height, (unsigned)li.fb_width, (unsigned)li.fb_height,
                                     (int)li.z, (int)li.z_min, (int)li.z_max, li.underscan ? 1 : 0,
                                     li.in_captures ? 1 : 0);
                clear_error_if(is_vi_stage);
                if (R_FAILED(renderer.last_soft_error()))
                    record_error(KinestopStage_LayerStack, renderer.last_soft_error());
                have_key = false;  // fresh buffers: present unconditionally
                log_diag(frame_start);
            }
        }

        if (frame_start >= next_slow) {
            next_slow = frame_start + slow_ticks;
            // A failed Z switch (e.g. docking with TV underscan set) is not
            // fatal - the dots still show - but it is reported, and cleared
            // once a retry succeeds.
            const s32 z_before = renderer.info().z;
            const Result zrc = renderer.update_z();
            if (R_FAILED(zrc)) {
                record_error(KinestopStage_Layer, zrc);
                z_failed = true;
            } else if (z_failed) {
                clear_error_if(is_layer_stage);
                z_failed = false;
            }
            if (renderer.ready() && renderer.info().z != z_before)
                kinestop::log::write("t=%s z -> %d (underscan %d)", seconds(t_ms).text, (int)renderer.info().z,
                                     renderer.info().underscan ? 1 : 0);
            sensor.retry_start();
        }

        // --- present --------------------------------------------------------
        if (renderer.ready()) {
            const float fb_scale = kinestop::frame_scale::fb_scale_for(renderer.fb_width());
            const kinestop::dots::Point offset = kinestop::dots::compute_offset(ori, local);
            // A layer re-creation (the only way fb_scale changes) clears
            // have_key, so keys of different scales are never compared.
            const FrameKey key{
                local_gen,
                kinestop::frame_scale::quantize_offset(offset.x, fb_scale),
                kinestop::frame_scale::quantize_offset(offset.y, fb_scale),
            };

            if (!have_key || key != last_key) {
                static kinestop::DotFrame frame;  // ~0.6 KiB: keep it off the stack
                build_frame(local, key, fb_scale, frame);

                KinestopStage stage = KinestopStage_None;
                bool presented = false;
                const u64 present_start = armGetSystemTick();
                rc = renderer.present(frame, stage, presented);
                const u64 present_us = armTicksToNs(armGetSystemTick() - present_start) / 1000ULL;
                const u32 present_us32 = present_us > 0xFFFFFFFFull ? 0xFFFFFFFFu : (u32)present_us;
                if (present_us32 > max_present_acc) max_present_acc = present_us32;

                if (R_FAILED(rc)) {
                    record_error(stage, rc);
                    renderer.exit();
                    kinestop::log::write("t=%s layer down (present failed, retry in 1 s)", seconds(t_ms).text);
                    log_diag(frame_start);
                    next_vi_retry = frame_start + retry_ticks;
                    have_key = false;
                } else if (!presented) {
                    // No usable buffer in time: last_key is unchanged, so
                    // the next iteration presents again.
                } else {
                    last_key = key;
                    have_key = true;
                    frames++;
                }
            }
        }

        // --- once per second: windows, periodic diagnostics -----------------
        const u64 now = armGetSystemTick();
        if (now >= next_second) {
            next_second += tick_freq;
            if (next_second <= now) next_second = now + tick_freq;  // fell behind (long stall)

            advance_1s = advance_acc;
            switches_1s = switches_acc;
            max_present_1s = max_present_acc;
            max_busy_1s = max_busy_acc;
            window_1s = sensor.take_window();
            advance_acc = 0;
            switches_acc = 0;
            max_present_acc = 0;
            max_busy_acc = 0;

            const bool dense = (dbg & KINESTOP_DEBUG_VERBOSE_LOG) || ms_since_start(now) < DENSE_LOG_MS;
            if (dense || now >= next_sparse_log) {
                log_diag(now);
                next_sparse_log = now + sparse_log_ticks;
            }
        }

        // --- publish status -------------------------------------------------
        mutexLock(&g_state.mutex);
        g_state.raw = ori;
        g_state.status.flags = (renderer.ready() ? KINESTOP_STATUS_LAYER_OK : 0) |
                               (sensor_ok ? KINESTOP_STATUS_SENSOR_OK : 0) |
                               ((suspended || blink_down) ? KINESTOP_STATUS_SUSPENDED : 0);
        g_state.status.pitch_deg = ori.pitch_deg;
        g_state.status.roll_deg = ori.roll_deg;
        g_state.status.frames_presented = frames;
        g_state.status.sensor_source = sensor_ok ? (u32)source : (u32)KinestopSensor_None;
        g_state.status.sensor_advance_1s = advance_1s;
        g_state.status.dequeue_timeouts = renderer.stats().dequeue_timeouts;
        g_state.status.max_present_us = max_present_1s;
        g_state.status.layer_inits = layer_inits;
        g_state.status.seven_init_rc = sensor.seven_init_rc();  // retry_start() may change it
        mutexUnlock(&g_state.mutex);

        // Pace to poll_rate_hz; Exit/SetConfig/Calibrate/SetSuspended wake us
        // early.
        const u64 period_ns = 1000000000ULL / (u64)clamp_rate(local.general.poll_rate_hz);
        const u64 elapsed_ns = armTicksToNs(armGetSystemTick() - frame_start);
        const u64 busy_us = elapsed_ns / 1000ULL;
        const u32 busy_us32 = busy_us > 0xFFFFFFFFull ? 0xFFFFFFFFu : (u32)busy_us;
        if (busy_us32 > max_busy_acc) max_busy_acc = busy_us32;
        if (elapsed_ns < period_ns)
            waitSingle(waiterForUEvent(&g_state.wake), period_ns - elapsed_ns);
    }

    kinestop::log::write("shutting down");
    // Get it onto the SD card now: if the teardown below hangs, the overlay
    // terminates us 0.5 s after Exit, before the next periodic drain.
    kinestop::log::flush();

    // IPC goes first. Once the IPC thread has replied to Exit it stops
    // serving, so a client's serviceClose() (a synchronous Close request)
    // would block until our session handles are closed. Closing them before
    // the VI teardown lets the overlay / ovl-sysmodules return at once and
    // still reach their terminate fallback if the teardown below hangs.
    //
    // If the IPC thread is blocked waiting for a request, make the wait
    // return Cancelled; it then sees exit_requested and leaves its loop. A
    // reply in flight is unaffected: svcReplyAndReceive sends the reply
    // before it waits.
    request_exit();
    svcCancelSynchronization(g_ipc_thread.handle);
    threadWaitForExit(&g_ipc_thread);
    threadClose(&g_ipc_thread);
    ipcServerExit(&g_server);

    renderer.exit();
    // Before __appExit's hidExit(), which aborts while a SevenSixAxis buffer
    // is still registered.
    sensor.exit();
    kinestop::log::write("exited cleanly");
    // Joins the logger thread, then writes what is still queued.
    kinestop::log::exit();
    return 0;
}
