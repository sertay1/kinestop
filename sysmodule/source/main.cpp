#include <switch.h>
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <sys/stat.h>

#include "../../common/config.hpp"
#include "../../common/math_types.hpp"
#include "filter.hpp"
#include "sensor.hpp"
#include "renderer.hpp"

#define INNER_HEAP_SIZE 0x480000 // 4.5 MB static heap

#ifdef __cplusplus
extern "C" {
#endif

u32 __nx_applet_type = AppletType_None;
u32 __nx_fs_num_sessions = 1;
u32 __nx_nv_transfermem_size = 0x400000;
u32 __nx_nv_service_type = 2; // NvServiceType_System

void __libnx_initheap(void) {
    static char inner_heap[INNER_HEAP_SIZE];
    void*  addr = inner_heap;
    size_t size = INNER_HEAP_SIZE;

    extern char* fake_heap_start;
    extern char* fake_heap_end;

    fake_heap_start = (char*)addr;
    fake_heap_end   = (char*)addr + size;
}

static void log_to_file(const char* fmt, ...) {
    mkdir("sdmc:/config", 0777);
    mkdir("sdmc:/config/kinestop", 0777);
    FILE* f = fopen("sdmc:/config/kinestop/sysmodule.log", "a");
    if (f) {
        va_list args;
        va_start(args, fmt);
        vfprintf(f, fmt, args);
        va_end(args);
        fprintf(f, "\n");
        fclose(f);
    }
}

void __appInit(void) {
    smInitialize();

    // Query Horizon OS firmware version
    Result rc = setsysInitialize();
    if (R_SUCCEEDED(rc)) {
        SetSysFirmwareVersion ver;
        if (R_SUCCEEDED(setsysGetFirmwareVersion(&ver))) {
            hosversionSet(MAKEHOSVERSION(ver.major, ver.minor, ver.micro));
        }
        setsysExit();
    }

    fsInitialize();
    fsdevMountSdmc();

    hidsysInitialize();
    hidInitialize();
}

void __appExit(void) {
    hidsysExit();
    hidExit();
    fsdevUnmountAll();
    fsExit();
    smExit();
}

#ifdef __cplusplus
}
#endif

int main(int argc, char* argv[]) {
    // Clear old log and write startup banner
    remove("sdmc:/config/kinestop/sysmodule.log");
    log_to_file("=== Kinestop Sysmodule v1.0.0 Started ===");

    kinestop::Config config;
    config.load();
    log_to_file("Config loaded (enabled=%d, style=%d, opacity=%.2f)", 
                config.general.enabled ? 1 : 0, config.visual.style, config.visual.opacity);

    kinestop::SensorManager sensor;
    bool sensor_ok = sensor.init();
    log_to_file("Sensor init: %s", sensor_ok ? "OK" : "FAILED");

    kinestop::ComplementaryFilter filter;

    kinestop::OverlayRenderer renderer;
    bool renderer_ok = renderer.init();
    log_to_file("Renderer init: %s", renderer_ok ? "OK" : "FAILED (will retry)");

    u64 last_tick = armGetSystemTick();
    u64 last_config_check_tick = last_tick;
    u64 loop_count = 0;

    kinestop::Orientation current_ori = {0.0f, 0.0f, 0.0f};

    while (true) {
        u64 current_tick = armGetSystemTick();
        loop_count++;

        // Periodic heartbeat log every ~10 seconds
        if (loop_count % 600 == 0) {
            log_to_file("Heartbeat: running (renderer_init=%d, enabled=%d, pitch=%.1f, roll=%.1f)",
                        renderer.is_initialized() ? 1 : 0, config.general.enabled ? 1 : 0,
                        current_ori.pitch_deg, current_ori.roll_deg);
        }

        // Check for config.ini changes every 500ms
        if (armTicksToNs(current_tick - last_config_check_tick) > 500000000ULL) {
            config.load();
            last_config_check_tick = current_tick;
        }

        // Calculate delta time
        float dt = (float)armTicksToNs(current_tick - last_tick) / 1000000000.0f;
        last_tick = current_tick;
        if (dt <= 0.0f || dt > 0.1f) dt = 1.0f / 60.0f;

        int target_hz = config.general.poll_rate_hz;
        if (target_hz < 10) target_hz = 10;
        if (target_hz > 120) target_hz = 120;
        u64 frame_time_ns = 1000000000ULL / target_hz;

        if (config.general.enabled) {
            // Lazy init renderer if VI display wasn't ready at early boot
            if (!renderer.is_initialized()) {
                if (renderer.init()) {
                    log_to_file("Renderer lazy init: SUCCEEDED!");
                }
            }

            kinestop::Vec3f accel(0.0f, -1.0f, 0.0f);
            kinestop::Vec3f gyro(0.0f, 0.0f, 0.0f);

            bool sampled = sensor.sample(accel, gyro);
            if (sampled) {
                current_ori = filter.update(accel, gyro, dt, config);
            }

            // Render frame on top of display
            renderer.render_frame(current_ori, config);
        } else {
            // Disabled: clear screen to transparent and sleep
            renderer.clear_transparent();
            svcSleepThread(50000000ULL); // 50ms
            continue;
        }

        u64 elapsed_ns = armTicksToNs(armGetSystemTick() - current_tick);
        if (elapsed_ns < frame_time_ns) {
            svcSleepThread(frame_time_ns - elapsed_ns);
        } else {
            svcSleepThread(1000000ULL); // 1ms
        }
    }

    log_to_file("=== Kinestop Sysmodule Exiting ===");
    renderer.exit();
    sensor.exit();
    return 0;
}
