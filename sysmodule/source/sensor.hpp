// Kinestop sysmodule - IMU access through HID.
//
// Original Kinestop by SertAy (https://github.com/sertay1/Kinestop). Reworked
// for the sysmodule/overlay split (a modification of the original project):
// both of the original's sources are opened - the SevenSixAxisSensor (console
// IMU through 0x80000 bytes of transfer memory from our heap) and the npad
// six-axis handles - and a source only counts when its sampling number has
// advanced since the previous read, so frozen data can no longer look like a
// healthy sensor. Nothing here can abort the process.

#pragma once

#include <switch.h>
#include "../../common/kinestop_ipc.h"
#include "../../common/math_types.hpp"

namespace kinestop {

// One fresh IMU sample.
struct SensorSample {
    Vec3f accel;                  // in G's
    Vec3f gyro;                   // passed through as HID reports it (as the original did)
    KinestopSensorSource source;
    u32 advance;                  // sampling-number advance of `source` since its previous read
    s8 npad_index;                // NpadSixAxis: which handle (index into the priority list), else -1
};

// Sampling-number advance of every source since the last take_window().
struct SensorWindow {
    u32 seven_advance;  // SevenSixAxis
    u32 npad_advance;   // the npad handle that advanced most
    s32 npad_index;     // which handle that was (index into the priority list), -1 if none advanced
};

class SensorManager {
public:
    SensorManager();

    // hid must already be initialized (__appInit). Opens the SevenSixAxis
    // sensor first (its 0x80000-byte buffer comes from our heap, so call this
    // before the renderer allocates its framebuffers), then the npad handles.
    // Fails only when no source at all could be started (returning the first
    // failing Result); the manager stays usable either way and retry_start()
    // keeps trying.
    Result init();

    // Must run before hidExit(): libnx aborts in hidExit() while a
    // SevenSixAxis buffer is still registered.
    void exit();

    // Retries starting sources that failed to start (e.g. a controller that
    // was not connected yet). Call about once per second.
    void retry_start();

    // Reads every source once. Returns true with the preferred FRESH sample
    // (sampling number advanced since the last call): SevenSixAxis first,
    // then the npad handles in priority order (handheld first), connected
    // ones before the rest. Returns false when nothing advanced, and also
    // while a preferred stream advanced recently but not on this call: the
    // SevenSixAxis stream within SEVEN_HOLD_NS, or a connected npad handle
    // within NPAD_HOLD_NS (lower-priority or not-connected handles are then
    // ignored). So one filter is never fed from two sources tick by tick.
    bool sample(SensorSample& out);

    // Per-source advance accumulated since the previous call; resets it.
    SensorWindow take_window();

    // SevenSixAxis: the failing hidInitializeSevenSixAxisSensor() Result,
    // else the latest hidStartSevenSixAxisSensor() Result (retry_start()
    // keeps updating it); 0 = started.
    Result seven_init_rc() const { return m_seven_rc; }

    static const char* source_name(KinestopSensorSource source);

private:
    static constexpr int MAX_HANDLES = 6;
    static constexpr u64 SEVEN_HOLD_NS = 100000000ULL;  // 100 ms
    static constexpr u64 NPAD_HOLD_NS = 100000000ULL;   // 100 ms

    // Sampling-number tracking for one source.
    struct Tracker {
        bool have;
        u64 last;
        u32 window;  // advance since take_window()

        // Returns the advance since the previous read (0 = not fresh). The
        // first read only establishes the baseline.
        u32 update(u64 sampling_number);
    };

    bool m_initialized;
    bool m_seven_initialized;  // buffer registered with hid: finalize before hidExit
    bool m_seven_started;
    Result m_seven_rc;
    Tracker m_seven;
    u64 m_seven_fresh_tick;    // last tick SevenSixAxis supplied the sample, 0 = never

    HidSixAxisSensorHandle m_handles[MAX_HANDLES];
    bool m_valid[MAX_HANDLES];
    bool m_started[MAX_HANDLES];
    Tracker m_npad[MAX_HANDLES];
    u64 m_npad_fresh_tick[MAX_HANDLES];  // last tick the handle advanced while connected, 0 = never
};

} // namespace kinestop
