// Kinestop sysmodule - draws the motion-cue dots on its own VI layer.
//
// Original Kinestop by SertAy (https://github.com/sertay1/Kinestop). The
// sysmodule/overlay split is a modification of the original project: this
// process owns the IMU, the complementary filter and a 640x360 VI layer; the
// Tesla/Ultrahand overlay is only a settings panel that launches it with
// pmshellLaunchProgram and talks to it over the "kinestop" service
// (common/kinestop_ipc.h).
//
// Threads:
//   main  - sample IMU, filter, compute dot positions, present (paced to
//           poll_rate_hz); owns the renderer and the sensor.
//   ipc   - serves the "kinestop" service; touches only g_state.

#include <switch.h>
#include <cmath>
#include <cstring>

#include "../../common/kinestop_ipc.h"
#include "../../common/config.hpp"
#include "../../common/dots.hpp"
#include "../../common/math_types.hpp"
#include "filter.hpp"
#include "ipc_server.h"
#include "log.hpp"
#include "renderer.hpp"
#include "sensor.hpp"

#ifndef APP_VERSION
#define APP_VERSION "2.0.0"
#endif

// ---------------------------------------------------------------------------
// Memory budget (System pool; everything here is ours to pay for).
//
// The inner heap is a static array in .bss. Reading libnx (nx/source), these
// are the only allocations made from it in this process:
//   framebufferCreate()   640x360 RGBA4444: stride 1280 B, height padded to
//                         384 rows -> 491,520 B/slot x 2 =       983,040 B
//                         (__libnx_aligned_alloc(0x1000), up to 4 KiB lost
//                         to alignment)
//   nvInitialize()        transfer memory, __nx_nv_transfermem_size
//                         (0x15000, libtesla's value)                86,016 B
//                         (tmemCreate -> __libnx_aligned_alloc(0x1000))
//   threadCreate()        IPC thread: 8 KiB stack + 576 B struct _reent
//                         + TLS image, page-aligned allocation      ~16,384 B
//   newlib stdio          FILE glue (4 x 184 B) + one BUFSIZ buffer
//                         per open file, strtof bigints              ~4,096 B
//   fsdev                 cwd buffer disabled (__nx_fsdev_support_cwd)   0 B
// vi, binder/parcels, nvmap, nvfence, hid and sm allocate nothing from the
// heap (parcels live on the stack; hid maps its 0x40000 shared memory into
// address space owned by hid). Sum ~1.09 MiB; the rest of the 1.25 MiB is
// slack for malloc headers, fragmentation across layer re-creation
// (framebuffers + nv transfer memory are freed and re-allocated) and the
// 4 KiB memalign over-allocation.
// Not heap: main thread stack (npdm main_thread_stack_size 0x4000).
// ---------------------------------------------------------------------------
#define INNER_HEAP_SIZE 0x140000  // 1.25 MiB

#define IPC_THREAD_STACK_SIZE 0x2000
#define IPC_THREAD_PRIORITY   44
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
            request_exit();
            return 0;

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
    s32 qx, qy;  // shared dot offset in quarter framebuffer pixels
    bool operator!=(const FrameKey& o) const {
        return config_gen != o.config_gen || qx != o.qx || qy != o.qy;
    }
};

constexpr float FB_SCALE = (float)kinestop::Renderer::FB_WIDTH / kinestop::dots::LOGICAL_WIDTH;  // 0.5
constexpr float OFFSET_QUANTUM = 4.0f;  // steps per framebuffer pixel

void build_frame(const Config& cfg, const FrameKey& key, kinestop::DotFrame& frame) {
    namespace dots = kinestop::dots;

    // Back to logical px from the quantized framebuffer offset.
    const float dx = (float)key.qx / (OFFSET_QUANTUM * FB_SCALE);
    const float dy = (float)key.qy / (OFFSET_QUANTUM * FB_SCALE);

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

        // Logical pixel x covers [x, x+1); its centre maps to (x + 0.5) * scale.
        frame.centers[i].x = (px + 0.5f) * FB_SCALE;
        frame.centers[i].y = (py + 0.5f) * FB_SCALE;
    }

    int theme_idx = cfg.visual.color;
    theme_idx = theme_idx < 0 ? 0 : (theme_idx >= (int)dots::NUM_THEMES ? (int)dots::NUM_THEMES - 1 : theme_idx);
    const dots::Theme& theme = dots::THEMES[theme_idx];

    int alpha_255 = (int)(cfg.visual.opacity * 255.0f);
    alpha_255 = alpha_255 < 25 ? 25 : (alpha_255 > 255 ? 255 : alpha_255);
    const int border_alpha = alpha_255 + 50 > 255 ? 255 : alpha_255 + 50;

    // The original's draw_filled_circle() fills every pixel within
    // ceil(radius), so the visible core is ceil(SIZES[i] * 0.9) logical px:
    // 1, 2, 3, ... 9, 11 - ten distinct sizes, the smallest 0.5 fb px.
    frame.style.core_radius = std::ceil(dots::dot_radius(cfg)) * FB_SCALE;

    // The original outlines the core with a ~1-logical-px circle at
    // radius + 2, leaving a ~1 px gap. At half resolution that ring and gap
    // would be 0.5 fb px each and blur into a faint smear, so the ring here
    // is a solid 1 fb px (2 logical px) band directly around the core,
    // ending at about the same outer radius as the original's.
    frame.style.ring_width = 1.0f;
    frame.style.core = {theme.cr, theme.cg, theme.cb, (u8)alpha_255};
    frame.style.border = {theme.br, theme.bg, theme.bb, (u8)border_alpha};
}

int clamp_rate(int hz) {
    return hz < 10 ? 10 : (hz > 120 ? 120 : hz);
}

} // namespace

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    kinestop::log::init();
    kinestop::log::write("Kinestop sysmodule " APP_VERSION " started (hos %u.%u.%u)",
                         HOSVER_MAJOR(hosversionGet()), HOSVER_MINOR(hosversionGet()), HOSVER_MICRO(hosversionGet()));

    Config config;
    if (!config.load())
        kinestop::log::write("config.ini not found, using defaults");

    // general.enabled is not checked: it is the overlay's master-toggle
    // state. Being launched at all (overlay, boot2.flag, or a sysmodule
    // manager such as ovl-sysmodules via toolbox.json) means "show dots";
    // the way to not have them at boot is to remove boot2.flag.

    std::memset(&g_state.status, 0, sizeof(g_state.status));
    g_state.status.api_version = KINESTOP_API_VERSION;
    g_state.config = config;
    g_state.config_gen = 1;
    g_state.exit_requested = false;
    ueventCreate(&g_state.wake, true);

    Result rc = ipcServerInit(&g_server, KINESTOP_SERVICE_NAME, IPC_MAX_SESSIONS);
    if (R_FAILED(rc)) {
        // Without the service nobody could stop us gracefully or change
        // settings; do not leave an unmanageable layer on screen.
        kinestop::log::write("ipcServerInit failed: rc 0x%X, exiting", (unsigned)rc);
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
        return 0;
    }

    kinestop::SensorManager sensor;
    rc = sensor.init();
    if (R_FAILED(rc)) record_error(KinestopStage_Sensor, rc);

    kinestop::ComplementaryFilter filter;
    kinestop::Renderer renderer;

    Config local = config;
    u32 local_gen = 0;
    Orientation ori{};
    FrameKey last_key{};
    bool have_key = false;
    u32 frames = 0;

    const u64 tick_freq = armGetSystemTickFreq();
    const u64 retry_ticks = tick_freq;        // VI init retry period: 1 s
    const u64 slow_ticks = tick_freq;         // underscan / sensor restart: 1 s
    const u64 sensor_fresh_ticks = tick_freq / 2;

    u64 last_tick = armGetSystemTick();
    u64 next_vi_retry = 0;
    u64 next_slow = last_tick + slow_ticks;
    u64 last_sample_tick = 0;
    bool have_sample = false;
    bool had_sensor_ok = false;
    bool z_failed = false;

    kinestop::log::write("running");

    while (true) {
        const u64 frame_start = armGetSystemTick();
        const float dt = (float)armTicksToNs(frame_start - last_tick) / 1e9f;
        last_tick = frame_start;

        // Snapshot the config the IPC thread may have replaced.
        mutexLock(&g_state.mutex);
        const bool quit = g_state.exit_requested;
        if (g_state.config_gen != local_gen) {
            local = g_state.config;
            local_gen = g_state.config_gen;
        }
        mutexUnlock(&g_state.mutex);
        if (quit) break;

        // IMU -> complementary filter (dt is clamped inside update()).
        kinestop::Vec3f accel, gyro;
        if (sensor.sample(accel, gyro)) {
            ori = filter.update(accel, gyro, dt, local);
            last_sample_tick = frame_start;
            have_sample = true;
        }
        const bool sensor_ok = have_sample && (frame_start - last_sample_tick) < sensor_fresh_ticks;
        if (sensor_ok && !had_sensor_ok) clear_error_if(is_sensor_stage);
        had_sensor_ok = sensor_ok;

        // Layer stack membership is fixed when the layer is created.
        if (renderer.ready() && renderer.in_captures() != local.visual.show_in_captures) {
            kinestop::log::write("show_in_captures -> %d, re-creating layer", local.visual.show_in_captures ? 1 : 0);
            renderer.exit();
            next_vi_retry = 0;
        }

        if (!renderer.ready() && frame_start >= next_vi_retry) {
            KinestopStage stage = KinestopStage_None;
            rc = renderer.init(local.visual.show_in_captures, stage);
            z_failed = false;  // a new layer gets its Z from init()
            if (R_FAILED(rc)) {
                record_error(stage, rc);
                next_vi_retry = frame_start + retry_ticks;
            } else {
                kinestop::log::write("layer up (captures=%d)", local.visual.show_in_captures ? 1 : 0);
                clear_error_if(is_vi_stage);
                if (R_FAILED(renderer.last_soft_error()))
                    record_error(KinestopStage_LayerStack, renderer.last_soft_error());
                have_key = false;  // fresh buffers: present unconditionally
            }
        }

        if (frame_start >= next_slow) {
            next_slow = frame_start + slow_ticks;
            // A failed Z switch (e.g. docking with TV underscan set) is not
            // fatal - the dots still show - but it is reported, and cleared
            // once a retry succeeds.
            const Result zrc = renderer.update_z();
            if (R_FAILED(zrc)) {
                record_error(KinestopStage_Layer, zrc);
                z_failed = true;
            } else if (z_failed) {
                clear_error_if(is_layer_stage);
                z_failed = false;
            }
            sensor.retry_start();
        }

        if (renderer.ready()) {
            const kinestop::dots::Point offset = kinestop::dots::compute_offset(ori, local);
            const FrameKey key{
                local_gen,
                (s32)lroundf(offset.x * FB_SCALE * OFFSET_QUANTUM),
                (s32)lroundf(offset.y * FB_SCALE * OFFSET_QUANTUM),
            };

            if (!have_key || key != last_key) {
                static kinestop::DotFrame frame;  // ~0.6 KiB: keep it off the stack
                build_frame(local, key, frame);

                KinestopStage stage = KinestopStage_None;
                bool presented = false;
                rc = renderer.present(frame, stage, presented);
                if (R_FAILED(rc)) {
                    record_error(stage, rc);
                    renderer.exit();
                    next_vi_retry = frame_start + retry_ticks;
                    have_key = false;
                } else if (!presented) {
                    // No buffer released in time: last_key is unchanged, so
                    // the next iteration presents again.
                } else {
                    last_key = key;
                    have_key = true;
                    frames++;
                }
            }
        }

        // Publish status.
        mutexLock(&g_state.mutex);
        g_state.raw = ori;
        g_state.status.flags = (renderer.ready() ? KINESTOP_STATUS_LAYER_OK : 0) |
                               (sensor_ok ? KINESTOP_STATUS_SENSOR_OK : 0);
        g_state.status.pitch_deg = ori.pitch_deg;
        g_state.status.roll_deg = ori.roll_deg;
        g_state.status.frames_presented = frames;
        mutexUnlock(&g_state.mutex);

        // Pace to poll_rate_hz; Exit/SetConfig/Calibrate wake us early.
        const u64 period_ns = 1000000000ULL / (u64)clamp_rate(local.general.poll_rate_hz);
        const u64 elapsed_ns = armTicksToNs(armGetSystemTick() - frame_start);
        if (elapsed_ns < period_ns)
            waitSingle(waiterForUEvent(&g_state.wake), period_ns - elapsed_ns);
    }

    kinestop::log::write("shutting down");

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
    sensor.exit();
    kinestop::log::write("exited cleanly");
    return 0;
}
