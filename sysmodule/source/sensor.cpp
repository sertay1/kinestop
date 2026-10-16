// Kinestop sysmodule - IMU access through HID.
//
// Original Kinestop by SertAy (https://github.com/sertay1/Kinestop). Reworked
// for the sysmodule/overlay split (a modification of the original project).

#include "sensor.hpp"
#include "log.hpp"
#include <cmath>
#include <cstring>

namespace kinestop {

namespace {

struct HandleSpec {
    HidNpadIdType id;
    HidNpadStyleTag style;
    int count;
};

// Priority order: console IMU first, then player 1's controllers.
constexpr HandleSpec SPECS[] = {
    {HidNpadIdType_Handheld, HidNpadStyleTag_NpadHandheld, 1},
    {HidNpadIdType_No1,      HidNpadStyleTag_NpadFullKey,  1},
    {HidNpadIdType_No1,      HidNpadStyleTag_NpadJoyDual,  2},  // left, right
    {HidNpadIdType_No1,      HidNpadStyleTag_NpadJoyLeft,  1},
    {HidNpadIdType_No1,      HidNpadStyleTag_NpadJoyRight, 1},
};

float magnitude(const HidVector& v) {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

// Copies v into prev and returns true when it differs bit for bit (a real
// IMU's noise changes at least one value on nearly every sample).
bool values_changed(float (&prev)[6], const float (&v)[6]) {
    const bool changed = std::memcmp(prev, v, sizeof(prev)) != 0;
    std::memcpy(prev, v, sizeof(prev));
    return changed;
}

// The pattern a placeholder six-axis state carries: accel exactly (0,0,-1)
// with zero angular velocity (also flagged " STUB" in the diag line).
bool is_stub(const HidSixAxisSensorState& st) {
    return st.acceleration.x == 0.0f && st.acceleration.y == 0.0f && st.acceleration.z == -1.0f &&
           st.angular_velocity.x == 0.0f && st.angular_velocity.y == 0.0f && st.angular_velocity.z == 0.0f;
}

// hidInitializeNpad() sends exactly this, but calls diagAbortWithResult() when
// it fails (libnx hid.c). Same revision table as libnx's _hidActivateNpad().
Result activate_npad() {
    const u64 aruid = appletGetAppletResourceUserId();
    if (hosversionBefore(5, 0, 0))
        return serviceDispatchIn(hidGetServiceSession(), 103, aruid, .in_send_pid = true);  // ActivateNpad

    u32 revision = 0x1;                                // [5.0.0+]
    if (hosversionAtLeast(6, 0, 0)) revision = 0x2;    // [6.0.0+]
    if (hosversionAtLeast(8, 0, 0)) revision = 0x3;    // [8.0.0+]
    if (hosversionAtLeast(18, 0, 0)) revision = 0x5;   // [18.0.0+]

    const struct {
        u32 revision;
        u32 pad;
        u64 AppletResourceUserId;
    } in = {revision, 0, aruid};
    return serviceDispatchIn(hidGetServiceSession(), 109, in, .in_send_pid = true);  // ActivateNpadWithRevision
}

} // namespace

u32 SensorManager::Tracker::update(u64 sampling_number) {
    if (!have) {
        have = true;
        last = sampling_number;
        return 0;
    }
    if (sampling_number == last) return 0;

    // A sampling number that went backwards (sensor restarted, timestamp
    // reset) is still new data; count it as one step.
    const u64 diff = sampling_number > last ? sampling_number - last : 1;
    last = sampling_number;
    const u32 advance = diff > 0xFFFFFFFFull ? 0xFFFFFFFFu : (u32)diff;
    window = window + advance < window ? 0xFFFFFFFFu : window + advance;
    return advance;
}

SensorManager::SensorManager()
    : m_initialized(false)
    , m_seven_initialized(false)
    , m_seven_started(false)
    , m_seven_rc(0)
    , m_seven{}
    , m_seven_fresh_tick(0)
    , m_npad_last_tick(0)
    , m_npad_streak_tick(0)
    , m_last_call_tick(0)
    , m_seven_change_tick(0)
    , m_current(KinestopSensor_None)
    , m_raw{}
{
    m_raw.npad_index = -1;
    std::memset(m_seven_prev, 0, sizeof(m_seven_prev));
    std::memset(m_npad_change_tick, 0, sizeof(m_npad_change_tick));
    std::memset(m_npad_prev, 0, sizeof(m_npad_prev));
    std::memset(m_handles, 0, sizeof(m_handles));
    std::memset(m_valid, 0, sizeof(m_valid));
    std::memset(m_started, 0, sizeof(m_started));
    std::memset(m_npad, 0, sizeof(m_npad));
    std::memset(m_npad_fresh_tick, 0, sizeof(m_npad_fresh_tick));
}

const char* SensorManager::source_name(KinestopSensorSource source) {
    switch (source) {
        case KinestopSensor_SevenSixAxis: return "seven";
        case KinestopSensor_NpadSixAxis:  return "npad";
        default:                          return "none";
    }
}

Result SensorManager::init() {
    if (m_initialized) return 0;

    // __appInit ignores a failing hidInitialize(); every hid read below would
    // then dereference a missing shared memory mapping.
    if (!hidGetSharedmemAddr()) {
        m_seven_rc = MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
        return m_seven_rc;
    }

    Result first_rc = 0;
    auto note = [&first_rc](Result rc) {
        if (R_FAILED(rc) && R_SUCCEEDED(first_rc)) first_rc = rc;
    };

    // 1. SevenSixAxisSensor, as the original v1 sensor did (it ignored every
    //    rc; here each one is logged, and an initialize or start failure is
    //    published as KinestopStatus.seven_init_rc). Done first so its 0x80000-byte,
    //    page-aligned buffer is carved out of an unfragmented heap.
    {
        Result rc_init = hidInitializeSevenSixAxisSensor();
        Result rc_start = 0, rc_fusion = 0, rc_reset = 0;
        if (R_SUCCEEDED(rc_init)) {
            m_seven_initialized = true;
            rc_start = hidStartSevenSixAxisSensor();
            m_seven_started = R_SUCCEEDED(rc_start);
            rc_fusion = hidSetSevenSixAxisSensorFusionStrength(1.0f);
            rc_reset = hidResetSevenSixAxisSensorTimestamp();
        }
        // Only initialize/start decide whether the stream runs; fusion and
        // timestamp-reset failures are logged below but not published.
        m_seven_rc = R_FAILED(rc_init) ? rc_init : rc_start;
        log::write("sevensixaxis: init 0x%X start 0x%X fusion 0x%X reset 0x%X",
                   (unsigned)rc_init, (unsigned)rc_start, (unsigned)rc_fusion, (unsigned)rc_reset);
    }

    // 2. Npad six-axis. Same npad setup libtesla does from inside
    //    nx-ovlloader (also an AppletType_None process): player 1 + handheld,
    //    standard styles. Done by hand rather than hidInitializeNpad() /
    //    padConfigureInput(), which abort on failure.
    const Result rc_npad = activate_npad();
    note(rc_npad);
    const HidNpadIdType ids[] = {HidNpadIdType_No1, HidNpadIdType_Handheld};
    note(hidSetSupportedNpadIdType(ids, 2));
    note(hidSetSupportedNpadStyleSet(HidNpadStyleSet_NpadStandard | HidNpadStyleTag_NpadSystemExt));

    int n = 0;
    for (const HandleSpec& spec : SPECS) {
        Result rc = hidGetSixAxisSensorHandles(&m_handles[n], spec.count, spec.id, spec.style);
        note(rc);
        for (int i = 0; i < spec.count; ++i) m_valid[n + i] = R_SUCCEEDED(rc);
        n += spec.count;
    }

    bool any_started = m_seven_started;
    unsigned started_mask = 0;
    for (int i = 0; i < MAX_HANDLES; ++i) {
        if (!m_valid[i]) continue;
        Result rc = hidStartSixAxisSensor(m_handles[i]);
        m_started[i] = R_SUCCEEDED(rc);
        if (m_started[i]) started_mask |= 1u << i;
        any_started |= m_started[i];
        note(rc);
    }
    log::write("npad: activate 0x%X, six-axis handles started mask 0x%X (bit0 = handheld)",
               (unsigned)rc_npad, started_mask);

    m_initialized = true;
    // A controller that is simply not connected is not an error worth
    // reporting; only complain when no source at all could be started.
    if (any_started) return 0;
    if (R_FAILED(first_rc)) return first_rc;
    if (R_FAILED(m_seven_rc)) return m_seven_rc;
    return MAKERESULT(Module_Libnx, LibnxError_NotFound);
}

void SensorManager::retry_start() {
    if (!m_initialized) return;
    if (m_seven_initialized && !m_seven_started) {
        const Result rc = hidStartSevenSixAxisSensor();
        m_seven_started = R_SUCCEEDED(rc);
        // Logged on change only: this runs once a second.
        if (rc != m_seven_rc)
            log::write("sevensixaxis: start retry 0x%X", (unsigned)rc);
        m_seven_rc = rc;  // 0 once it started
    }
    for (int i = 0; i < MAX_HANDLES; ++i) {
        if (m_valid[i] && !m_started[i])
            m_started[i] = R_SUCCEEDED(hidStartSixAxisSensor(m_handles[i]));
    }
}

void SensorManager::exit() {
    if (!m_initialized) return;

    for (int i = 0; i < MAX_HANDLES; ++i) {
        if (m_started[i]) {
            hidStopSixAxisSensor(m_handles[i]);
            m_started[i] = false;
        }
    }

    if (m_seven_started) {
        hidStopSevenSixAxisSensor();
        m_seven_started = false;
    }
    if (m_seven_initialized) {
        // Frees the buffer even when the IPC fails, which is what hidExit()
        // checks for.
        hidFinalizeSevenSixAxisSensor();
        m_seven_initialized = false;
    }
    m_initialized = false;
}

SensorWindow SensorManager::take_window() {
    SensorWindow w{m_seven.window, 0, -1};
    m_seven.window = 0;
    for (int i = 0; i < MAX_HANDLES; ++i) {
        if (m_npad[i].window > w.npad_advance) {
            w.npad_advance = m_npad[i].window;
            w.npad_index = i;
        }
        m_npad[i].window = 0;
    }
    return w;
}

bool SensorManager::sample(SensorSample& out) {
    if (!m_initialized) return false;

    // Every source is read on every call so each one's advance is tracked
    // (and logged) even while the other is current.
    const u64 now = armGetSystemTick();
    auto within = [now](u64 tick, u64 ns) { return tick != 0 && armTicksToNs(now - tick) < ns; };

    // npad hold: NPAD_HOLD_NS, but at least 1.5 x the time since the previous
    // call, so at low poll rates (10 Hz = 100 ms between calls) consecutive
    // ticks with fresh npad data still count as one gap-free run, while a
    // tick without npad data still breaks it.
    u64 npad_hold = NPAD_HOLD_NS;
    if (m_last_call_tick != 0) {
        const u64 gap = armTicksToNs(now - m_last_call_tick);
        if (gap + gap / 2 > npad_hold) npad_hold = gap + gap / 2;
    }
    m_last_call_tick = now;

    // 1. SevenSixAxisSensor. Field mapping exactly as in the original v1
    //    sensor: unk_x18[0..2] acceleration, [3..5] angular velocity,
    //    normalised to G's when it looks like m/s^2.
    bool seven_fresh = false;
    SensorSample seven{};
    if (m_seven_initialized) {
        HidSevenSixAxisSensorState st;
        std::memset(&st, 0, sizeof(st));
        size_t total = 0;
        if (R_SUCCEEDED(hidGetSevenSixAxisSensorStates(&st, 1, &total)) && total > 0) {
            const u32 advance = m_seven.update(st.sampling_number);
            float ax = st.unk_x18[0];
            float ay = st.unk_x18[1];
            float az = st.unk_x18[2];
            const float mag = std::sqrt(ax * ax + ay * ay + az * az);
            if (advance > 0 && mag > 0.05f) {
                // If reported in m/s^2 (~9.8G), normalize to standard G's
                if (mag > 4.5f) {
                    ax /= 9.80665f;
                    ay /= 9.80665f;
                    az /= 9.80665f;
                }
                seven.accel = Vec3f(ax, ay, az);
                seven.gyro = Vec3f(st.unk_x18[3], st.unk_x18[4], st.unk_x18[5]);
                seven.source = KinestopSensor_SevenSixAxis;
                seven.advance = advance;
                seven.npad_index = -1;
                seven_fresh = true;
                m_seven_fresh_tick = now;
                const float v[6] = {st.unk_x18[0], st.unk_x18[1], st.unk_x18[2],
                                    st.unk_x18[3], st.unk_x18[4], st.unk_x18[5]};
                if (values_changed(m_seven_prev, v)) m_seven_change_tick = now;
                m_raw.seven_accel = seven.accel;
                m_raw.seven_gyro = seven.gyro;
            }
        }
    }

    // SevenSixAxis is streaming values that change: an npad handle that
    // looks fake (stub pattern, or frozen) must not shut it out.
    const bool seven_alive = within(m_seven_fresh_tick, SEVEN_HOLD_NS);
    const bool seven_moving = seven_alive && within(m_seven_change_tick, NPAD_STUCK_NS);

    // 2. Npad handles, handheld first. All of them are read (and tracked)
    //    first; then the sample comes from the highest-priority handle HID
    //    reports as connected that advanced, and while such a handle has
    //    advanced within NPAD_HOLD_NS, a tick on which it has no new sample
    //    is skipped instead of being filled from a lower-priority or
    //    not-connected handle (e.g. a stale handheld LIFO once docked), so
    //    the filter is never fed from two devices with different axes.
    //    Only the newest state of the chosen handle is used; the caller
    //    integrates over the time since the previous consumed sample.
    HidSixAxisSensorState states[MAX_HANDLES];
    u32 advances[MAX_HANDLES];
    bool usable[MAX_HANDLES];
    bool connected[MAX_HANDLES];
    int held = -1;  // highest-priority connected handle that advanced within the npad hold
    bool raw_done = false;
    for (int i = 0; i < MAX_HANDLES; ++i) {
        usable[i] = false;
        connected[i] = false;
        advances[i] = 0;
        if (!m_started[i]) continue;

        std::memset(&states[i], 0, sizeof(states[i]));
        if (hidGetSixAxisSensorStates(m_handles[i], &states[i], 1) == 0) continue;

        advances[i] = m_npad[i].update(states[i].sampling_number);
        if (advances[i] == 0) continue;

        const HidSixAxisSensorState& st = states[i];
        const float v[6] = {st.acceleration.x, st.acceleration.y, st.acceleration.z,
                            st.angular_velocity.x, st.angular_velocity.y, st.angular_velocity.z};
        if (values_changed(m_npad_prev[i], v)) m_npad_change_tick[i] = now;
        const bool suspect = seven_moving && (is_stub(st) || !within(m_npad_change_tick[i], NPAD_STUCK_NS));
        if (!raw_done) {
            raw_done = true;
            m_raw.npad_accel = Vec3f(v[0], v[1], v[2]);
            m_raw.npad_gyro = Vec3f(v[3], v[4], v[5]);
            m_raw.npad_index = (s8)i;
            m_raw.npad_suspect = suspect;
        }

        if (suspect || magnitude(st.acceleration) <= 0.05f) continue;
        usable[i] = true;
        connected[i] = (st.attributes & HidSixAxisSensorAttribute_IsConnected) != 0;
        if (connected[i]) m_npad_fresh_tick[i] = now;
    }
    for (int i = 0; i < MAX_HANDLES && held < 0; ++i) {
        if (within(m_npad_fresh_tick[i], npad_hold))
            held = i;
    }

    int best = -1;
    if (held >= 0) {
        // Only the held handle counts (it was refreshed above if it advanced
        // on this call; otherwise this tick has no npad sample).
        if (usable[held] && connected[held]) best = held;
    } else {
        // Nothing connected advanced recently: first advancing handle.
        for (int i = 0; i < MAX_HANDLES && best < 0; ++i)
            if (usable[i]) best = i;
    }

    if (best >= 0) {
        // A gap longer than the hold time starts a new run.
        if (!within(m_npad_last_tick, npad_hold)) m_npad_streak_tick = now;
        m_npad_last_tick = now;
    }

    // 3. Pick the source (see sensor.hpp): npad preferred, SevenSixAxis as
    //    fallback, each held through short gaps.
    const bool npad_alive = within(m_npad_last_tick, npad_hold);
    const bool npad_settled = npad_alive && armTicksToNs(now - m_npad_streak_tick) >= NPAD_SETTLE_NS;
    switch (m_current) {
        case KinestopSensor_NpadSixAxis:
            if (!npad_alive)
                m_current = seven_alive ? KinestopSensor_SevenSixAxis : KinestopSensor_None;
            break;
        case KinestopSensor_SevenSixAxis:
            if (npad_settled || (npad_alive && !seven_alive))
                m_current = KinestopSensor_NpadSixAxis;
            else if (!seven_alive)
                m_current = KinestopSensor_None;
            break;
        default:
            // Nothing to stay with: take whatever streams, npad first.
            m_current = npad_alive ? KinestopSensor_NpadSixAxis
                      : seven_alive ? KinestopSensor_SevenSixAxis : KinestopSensor_None;
            break;
    }

    if (m_current == KinestopSensor_NpadSixAxis && best >= 0) {
        const HidSixAxisSensorState& st = states[best];
        out.accel = Vec3f(st.acceleration.x, st.acceleration.y, st.acceleration.z);
        out.gyro = Vec3f(st.angular_velocity.x, st.angular_velocity.y, st.angular_velocity.z);
        out.source = KinestopSensor_NpadSixAxis;
        out.advance = advances[best];
        out.npad_index = (s8)best;
        return true;
    }
    if (m_current == KinestopSensor_SevenSixAxis && seven_fresh) {
        out = seven;
        return true;
    }
    return false;
}

} // namespace kinestop
