// Kinestop - IMU access through HID.
//
// Original Kinestop by SertAy (https://github.com/sertay1/Kinestop). Reworked
// as part of a modified version of the original project (see sensor.hpp).

#include "sensor.hpp"
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
// with zero angular velocity.
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

bool SensorManager::Tracker::update(u64 sampling_number) {
    if (!have) {
        have = true;
        last = sampling_number;
        return false;
    }
    // A sampling number that went backwards (sensor restarted) is new data too.
    if (sampling_number == last) return false;
    last = sampling_number;
    return true;
}

Result SensorManager::init(bool use_seven) {
    if (m_initialized) return 0;

    // Fresh state: a re-init (power-button HID restart, settings toggle)
    // starts over.
    *this = SensorManager();

    // libtesla's power-button handler re-initializes hid and ignores a
    // failing hidInitialize(); every hid read below would then dereference a
    // missing shared memory mapping.
    if (!hidGetSharedmemAddr())
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);

    Result first_rc = 0;
    auto note = [&first_rc](Result rc) {
        if (R_FAILED(rc) && R_SUCCEEDED(first_rc)) first_rc = rc;
    };

    // 1. SevenSixAxisSensor, as the original v1 sensor did. Done first so its
    //    0x80000-byte, page-aligned buffer is carved out of an unfragmented
    //    heap. Fusion and timestamp-reset failures do not stop the stream.
    if (use_seven) {
        m_seven_rc = hidInitializeSevenSixAxisSensor();
        if (R_SUCCEEDED(m_seven_rc)) {
            m_seven_initialized = true;
            m_seven_rc = hidStartSevenSixAxisSensor();
            m_seven_started = R_SUCCEEDED(m_seven_rc);
            hidSetSevenSixAxisSensorFusionStrength(1.0f);
            hidResetSevenSixAxisSensorTimestamp();
        }
    }

    // 2. Npad six-axis. Same npad setup libtesla does (player 1 + handheld,
    //    standard styles), done by hand rather than hidInitializeNpad() /
    //    padConfigureInput(), which abort on failure.
    note(activate_npad());
    const HidNpadIdType ids[] = {HidNpadIdType_No1, HidNpadIdType_Handheld};
    note(hidSetSupportedNpadIdType(ids, 2));
    note(hidSetSupportedNpadStyleSet(HidNpadStyleSet_NpadStandard | HidNpadStyleTag_NpadSystemExt));

    int n = 0;
    for (const HandleSpec& spec : SPECS) {
        const Result rc = hidGetSixAxisSensorHandles(&m_handles[n], spec.count, spec.id, spec.style);
        note(rc);
        for (int i = 0; i < spec.count; ++i) m_valid[n + i] = R_SUCCEEDED(rc);
        n += spec.count;
    }

    bool any_started = m_seven_started;
    for (int i = 0; i < MAX_HANDLES; ++i) {
        if (!m_valid[i]) continue;
        const Result rc = hidStartSixAxisSensor(m_handles[i]);
        m_started[i] = R_SUCCEEDED(rc);
        any_started |= m_started[i];
        note(rc);
    }

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
        m_seven_rc = hidStartSevenSixAxisSensor();
        m_seven_started = R_SUCCEEDED(m_seven_rc);
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

bool SensorManager::sample(SensorSample& out) {
    if (!m_initialized) return false;

    const u64 now = armGetSystemTick();
    auto within = [now](u64 tick, u64 ns) { return tick != 0 && armTicksToNs(now - tick) < ns; };

    // npad hold: NPAD_HOLD_NS, but at least 1.5 x the time since the previous
    // call, so at low poll rates consecutive ticks with fresh npad data still
    // count as one gap-free run, while a tick without npad data breaks it.
    u64 npad_hold = NPAD_HOLD_NS;
    if (m_last_call_tick != 0) {
        const u64 gap = armTicksToNs(now - m_last_call_tick);
        if (gap + gap / 2 > npad_hold) npad_hold = gap + gap / 2;
    }
    m_last_call_tick = now;

    // 1. SevenSixAxisSensor (Console Hardware IMU).
    // Primary and continuous 60 FPS source, exactly like original Kinestop.
    if (m_seven_initialized) {
        HidSevenSixAxisSensorState st;
        std::memset(&st, 0, sizeof(st));
        size_t total = 0;
        if (R_SUCCEEDED(hidGetSevenSixAxisSensorStates(&st, 1, &total)) && total > 0) {
            float ax = st.unk_x18[0];
            float ay = st.unk_x18[1];
            float az = st.unk_x18[2];
            const float mag = std::sqrt(ax * ax + ay * ay + az * az);
            if (mag > 0.05f) {
                // If reported in m/s^2 (~9.8G), normalize to standard G's
                if (mag > 4.5f) {
                    ax /= 9.80665f;
                    ay /= 9.80665f;
                    az /= 9.80665f;
                }
                out.accel = Vec3f(ax, ay, az);
                out.gyro = Vec3f(st.unk_x18[3], st.unk_x18[4], st.unk_x18[5]);
                out.source = SensorSource::SevenSixAxis;
                m_current = SensorSource::SevenSixAxis;
                m_seven_fresh_tick = now;
                return true;
            }
        }
    }
    const bool seven_moving = false;

    // 2. Npad handles, handheld first. All of them are read (and tracked)
    //    first; then the sample comes from the highest-priority handle HID
    //    reports as connected that advanced, and while such a handle has
    //    advanced within the npad hold, a tick on which it has no new sample
    //    is skipped instead of being filled from a lower-priority or
    //    not-connected handle (e.g. a stale handheld LIFO once docked), so
    //    the filter is never fed from two devices with different axes.
    HidSixAxisSensorState states[MAX_HANDLES];
    bool usable[MAX_HANDLES];
    bool connected[MAX_HANDLES];
    for (int i = 0; i < MAX_HANDLES; ++i) {
        usable[i] = false;
        connected[i] = false;
        if (!m_started[i]) continue;

        std::memset(&states[i], 0, sizeof(states[i]));
        if (hidGetSixAxisSensorStates(m_handles[i], &states[i], 1) == 0) continue;
        if (!m_npad[i].update(states[i].sampling_number)) continue;

        const HidSixAxisSensorState& st = states[i];
        const float v[6] = {st.acceleration.x, st.acceleration.y, st.acceleration.z,
                            st.angular_velocity.x, st.angular_velocity.y, st.angular_velocity.z};
        if (values_changed(m_npad_prev[i], v)) m_npad_change_tick[i] = now;
        const bool suspect = seven_moving && (is_stub(st) || !within(m_npad_change_tick[i], NPAD_STUCK_NS));
        if (suspect || magnitude(st.acceleration) <= 0.05f) continue;

        usable[i] = true;
        connected[i] = (st.attributes & HidSixAxisSensorAttribute_IsConnected) != 0;
        if (connected[i]) m_npad_fresh_tick[i] = now;
    }

    int held = -1;  // highest-priority connected handle that advanced within the npad hold
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

    // Fallback: If SevenSixAxis wasn't available, use best connected Npad controller.
    if (best >= 0) {
        const HidSixAxisSensorState& st = states[best];
        out.accel = Vec3f(st.acceleration.x, st.acceleration.y, st.acceleration.z);
        out.gyro = Vec3f(st.angular_velocity.x, st.angular_velocity.y, st.angular_velocity.z);
        out.source = SensorSource::Npad;
        m_current = SensorSource::Npad;
        return true;
    }
    return false;
}

} // namespace kinestop
