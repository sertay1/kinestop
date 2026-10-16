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

// Newest raw values of both sources, whichever is current, so the
// diagnostics line shows whether npad carries real IMU data.
struct SensorRaw {
    Vec3f seven_accel;   // newest usable SevenSixAxis sample (in G's), zero until one arrives
    Vec3f seven_gyro;
    Vec3f npad_accel;    // newest advanced read of the first advancing npad handle
    Vec3f npad_gyro;
    s8 npad_index;       // that handle (index into the priority list), -1 until one advanced
    bool npad_suspect;   // that handle is ignored as stuck/stub (see sample())
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

    // Reads every source once and returns true with a FRESH sample (sampling
    // number advanced since the last call) from the current source, which is
    // chosen like this:
    //   * npad six-axis is preferred (~200 Hz on handheld, vs ~10 Hz from
    //     SevenSixAxis on HOS 19). The npad sample comes from the
    //     highest-priority handle HID reports as connected that advanced
    //     (handheld first); a connected handle that advanced within
    //     NPAD_HOLD_NS is held, and lower-priority or not-connected handles
    //     are then ignored. With no connected handle advancing, the first
    //     advancing handle is used.
    //   * SevenSixAxis is the fallback, used only once npad has supplied
    //     nothing for the npad hold time (NPAD_HOLD_NS = 100 ms, or 1.5 poll
    //     periods when that is longer, so it also works at 10 Hz).
    //   * An npad handle whose values look fake is not used while the
    //     SevenSixAxis values are changing: the stub pattern accel (0,0,-1)
    //     with zero gyro, or values bit-identical for NPAD_STUCK_NS. A real
    //     IMU always has some noise; test.1 (npad only) had dots that did
    //     not move, test.2 proved SevenSixAxis works.
    //   * Hysteresis, so the filter is never fed from two sources tick by
    //     tick: while a source is current, a tick on which it has no new
    //     sample returns false (instead of being filled from the other
    //     source) until that source has been silent for its hold time
    //     (npad hold / SEVEN_HOLD_NS). Going back from SevenSixAxis to npad
    //     additionally needs npad to have streamed without a gap for
    //     NPAD_SETTLE_NS.
    bool sample(SensorSample& out);

    // Newest values of both sources (updated by sample()).
    const SensorRaw& raw() const { return m_raw; }

    // Per-source advance accumulated since the previous call; resets it.
    SensorWindow take_window();

    // SevenSixAxis: the failing hidInitializeSevenSixAxisSensor() Result,
    // else the latest hidStartSevenSixAxisSensor() Result (retry_start()
    // keeps updating it); 0 = started.
    Result seven_init_rc() const { return m_seven_rc; }

    static const char* source_name(KinestopSensorSource source);

private:
    static constexpr int MAX_HANDLES = 6;
    // SevenSixAxis only advances ~10 times a second on HOS 19, so its hold
    // has to cover more than one 100 ms gap.
    static constexpr u64 SEVEN_HOLD_NS = 250000000ULL;  // 250 ms
    static constexpr u64 NPAD_HOLD_NS = 100000000ULL;   // 100 ms
    static constexpr u64 NPAD_SETTLE_NS = 250000000ULL; // npad must stream this long to take over from SevenSixAxis
    static constexpr u64 NPAD_STUCK_NS = 2000000000ULL; // npad values unchanged this long (while SevenSixAxis moves) = not real

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
    u64 m_seven_fresh_tick;    // last tick SevenSixAxis had a usable new sample, 0 = never
    u64 m_npad_last_tick;      // last tick an npad handle had a usable sample (after the hold rule), 0 = never
    u64 m_npad_streak_tick;    // start of the current gap-free (< npad hold) npad run
    u64 m_last_call_tick;      // previous sample() call, 0 = none (the npad hold scales with the poll period)
    u64 m_seven_change_tick;   // last tick the SevenSixAxis values changed, 0 = never
    float m_seven_prev[6];     // previous SevenSixAxis accel + gyro (raw), for change detection
    KinestopSensorSource m_current;  // source the samples currently come from
    SensorRaw m_raw;

    HidSixAxisSensorHandle m_handles[MAX_HANDLES];
    bool m_valid[MAX_HANDLES];
    bool m_started[MAX_HANDLES];
    Tracker m_npad[MAX_HANDLES];
    u64 m_npad_fresh_tick[MAX_HANDLES];  // last tick the handle advanced while connected, 0 = never
    u64 m_npad_change_tick[MAX_HANDLES]; // last tick the handle's values changed, 0 = never
    float m_npad_prev[MAX_HANDLES][6];   // previous accel + gyro of each handle, for change detection
};

} // namespace kinestop
