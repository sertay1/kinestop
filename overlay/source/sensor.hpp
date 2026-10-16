// Kinestop - IMU access through HID.
//
// Original Kinestop by SertAy (https://github.com/sertay1/Kinestop). Reworked
// as part of a modified version of the original project: the npad six-axis
// handles are always opened, the SevenSixAxisSensor (console IMU, 0x80000
// bytes of transfer memory from the overlay heap) only when the "Console
// motion sensor" setting is on, and a source only counts when its sampling
// number has advanced since the previous read, so frozen data can no longer
// look like a healthy sensor. Nothing here can abort the process.

#pragma once

#include <switch.h>
#include "math_types.hpp"

namespace kinestop {

enum class SensorSource : u8 { None, SevenSixAxis, Npad };

// One fresh IMU sample.
struct SensorSample {
    Vec3f accel;          // in G's
    Vec3f gyro;           // passed through as HID reports it (as the original did)
    SensorSource source;
};

class SensorManager {
public:
    SensorManager() = default;

    // hid must already be initialized (__appInit). With use_seven, opens the
    // SevenSixAxis sensor first (its 0x80000-byte buffer comes from the heap,
    // so call this before the renderer allocates its framebuffers), then the
    // npad handles. Fails only when no source at all could be started; the
    // manager stays usable either way and retry_start() keeps trying.
    Result init(bool use_seven);

    // Must run before hidExit(): libnx aborts in hidExit() while a
    // SevenSixAxis buffer is still registered.
    void exit();

    bool initialized() const { return m_initialized; }

    // SevenSixAxis: the failing initialize/start Result, 0 when it runs or
    // was not requested.
    Result seven_rc() const { return m_seven_rc; }

    // Retries starting sources that failed to start (e.g. a controller that
    // was not connected yet). Call about once per second.
    void retry_start();

    // Reads every source once and returns true with a FRESH sample (sampling
    // number advanced since the last call) from the current source, which is
    // chosen like this:
    //   * npad six-axis is preferred (~200 Hz on handheld in games, vs ~10 Hz
    //     from SevenSixAxis on HOS 19; npad does not advance at the HOME
    //     menu). The npad sample comes from the highest-priority handle HID
    //     reports as connected that advanced (handheld first); a connected
    //     handle that advanced within the npad hold is held, and
    //     lower-priority or not-connected handles are then ignored. With no
    //     connected handle advancing, the first advancing handle is used.
    //   * SevenSixAxis (when opened) is the fallback, used only once npad has
    //     supplied nothing for the npad hold time (NPAD_HOLD_NS = 100 ms, or
    //     1.5 poll periods when that is longer).
    //   * An npad handle whose values look fake is not used while the
    //     SevenSixAxis values are changing: the stub pattern accel (0,0,-1)
    //     with zero gyro, or values bit-identical for NPAD_STUCK_NS.
    //   * Hysteresis, so the filter is never fed from two sources tick by
    //     tick: while a source is current, a tick on which it has no new
    //     sample returns false until that source has been silent for its hold
    //     time. Going back from SevenSixAxis to npad additionally needs npad
    //     to have streamed without a gap for NPAD_SETTLE_NS.
    bool sample(SensorSample& out);

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
        bool have = false;
        u64 last = 0;

        // Returns true when the sampling number moved since the previous
        // read. The first read only establishes the baseline.
        bool update(u64 sampling_number);
    };

    bool m_initialized{};
    bool m_seven_initialized{};  // buffer registered with hid: finalize before hidExit
    bool m_seven_started{};
    Result m_seven_rc{};
    Tracker m_seven{};
    u64 m_seven_fresh_tick{};    // last tick SevenSixAxis had a usable new sample, 0 = never
    u64 m_seven_change_tick{};   // last tick the SevenSixAxis values changed, 0 = never
    float m_seven_prev[6]{};     // previous SevenSixAxis accel + gyro (raw), for change detection
    u64 m_npad_last_tick{};      // last tick an npad handle had a usable sample (after the hold rule), 0 = never
    u64 m_npad_streak_tick{};    // start of the current gap-free (< npad hold) npad run
    u64 m_last_call_tick{};      // previous sample() call, 0 = none (the npad hold scales with the poll period)
    SensorSource m_current = SensorSource::None;    // source the samples currently come from

    HidSixAxisSensorHandle m_handles[MAX_HANDLES]{};
    bool m_valid[MAX_HANDLES]{};
    bool m_started[MAX_HANDLES]{};
    Tracker m_npad[MAX_HANDLES]{};
    u64 m_npad_fresh_tick[MAX_HANDLES]{};  // last tick the handle advanced while connected, 0 = never
    u64 m_npad_change_tick[MAX_HANDLES]{}; // last tick the handle's values changed, 0 = never
    float m_npad_prev[MAX_HANDLES][6]{};   // previous accel + gyro of each handle, for change detection
};

} // namespace kinestop
