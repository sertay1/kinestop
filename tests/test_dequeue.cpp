// Kinestop host tests - sysmodule/source/bounded_dequeue.hpp, the deadline
// policy Renderer::dequeue_bounded() uses to get a buffer from the
// compositor. Driven with a simulated clock and a simulated buffer queue /
// binder event, including the failure modes suspected on hardware: an event
// that never signals, one that stays signalled (autoclear=false, never
// reset), and a release that lands between a WouldBlock and the reset.
//
// Part of the sysmodule/overlay split of the original Kinestop by SertAy
// (https://github.com/sertay1/Kinestop).

#include <cstdint>
#include <cstdio>
#include <vector>

#include "check.hpp"
#include "bounded_dequeue.hpp"

namespace bq = kinestop::bounded;

namespace {

constexpr uint64_t MS = 1000000ULL;
constexpr uint64_t NEVER = UINT64_MAX;

// The renderer's real values (renderer.cpp).
constexpr bq::Timing REAL = {20 * MS, 4 * MS, 1 * MS};

enum class Op { Try, Clear, Wait, Sleep };

// Simulated buffer queue + binder event + clock.
struct Sim {
    uint64_t t = 5000 * MS;      // arbitrary non-zero start
    uint64_t try_cost = 30000;   // one binder IPC: 30 us
    uint64_t release_at = NEVER; // a buffer is free from this time on
    int fail_at_try = -1;        // this attempt (1-based) returns Failed

    bool event = true;            // has_event()
    bool signal_on_release = true;
    bool clear_works = true;      // false: event stays signalled forever once set
    bool stuck_signalled = false; // signalled from the start, clear never works

    bool signalled = false;
    bool release_signalled = false;

    std::vector<Op> ops;
    int tries = 0, clears = 0, waits = 0, sleeps = 0;
    uint64_t max_wait_ns = 0;
    uint64_t latest_end = 0;  // latest time any wait/sleep ended
    bool wait_without_rearm = false;

    void tick(uint64_t ns) {
        t += ns;
        if (!release_signalled && t >= release_at) {
            release_signalled = true;
            if (signal_on_release) signalled = true;
        }
    }

    // --- Ops contract ---
    uint64_t now_ns() { return t; }

    bq::Try try_dequeue() {
        ops.push_back(Op::Try);
        ++tries;
        const bool free_now = t >= release_at;  // state when the IPC is served
        tick(try_cost);
        if (tries == fail_at_try) return bq::Try::Failed;
        return free_now ? bq::Try::Got : bq::Try::WouldBlock;
    }

    bool has_event() { return event; }

    void clear_event() {
        ops.push_back(Op::Clear);
        ++clears;
        if (clear_works && !stuck_signalled) signalled = false;
    }

    bool wait_event(uint64_t ns) {
        // Every wait must follow "reset, then one more attempt".
        const size_t n = ops.size();
        if (n < 2 || ops[n - 1] != Op::Try || ops[n - 2] != Op::Clear) wait_without_rearm = true;
        ops.push_back(Op::Wait);
        ++waits;
        if (ns > max_wait_ns) max_wait_ns = ns;

        bool result;
        if (signalled || stuck_signalled) {
            result = true;  // returns at once
        } else if (signal_on_release && !release_signalled && release_at > t && release_at - t <= ns) {
            tick(release_at - t);
            result = true;
        } else {
            tick(ns);
            result = false;
        }
        if (t > latest_end) latest_end = t;
        return result;
    }

    void sleep_ns(uint64_t ns) {
        ops.push_back(Op::Sleep);
        ++sleeps;
        tick(ns);
        if (t > latest_end) latest_end = t;
    }
};

struct Run {
    bq::Outcome outcome;
    bq::Counters counters;
    uint64_t start;
    uint64_t elapsed;
};

Run run(Sim& s, const bq::Timing& timing = REAL) {
    Run r;
    r.counters = {0, 0};
    r.start = s.t;
    r.outcome = bq::dequeue_until_deadline(s, timing, r.counters);
    r.elapsed = s.t - r.start;
    return r;
}

// Upper bound on attempts promised by the header.
int max_tries(const bq::Timing& t) {
    return (int)(2 * t.deadline_ns / t.min_retry_ns + 2);
}

void test_immediate() {
    Sim s;
    s.release_at = 0;
    const Run r = run(s);
    CHECK(r.outcome == bq::Outcome::Got);
    CHECK_EQ(s.tries, 1);
    CHECK_EQ(s.clears + s.waits + s.sleeps, 0);
    CHECK_EQ(r.counters.would_blocks, 0u);
}

// Compositor never releases, event never signals: gives up right at the
// deadline after a handful of 4 ms waits.
void test_never_released() {
    Sim s;
    const Run r = run(s);
    CHECK(r.outcome == bq::Outcome::TimedOut);
    CHECK(r.elapsed >= REAL.deadline_ns);
    CHECK(r.elapsed <= REAL.deadline_ns + s.try_cost);
    CHECK(s.latest_end <= r.start + REAL.deadline_ns);  // no wait/sleep runs past it
    CHECK(s.max_wait_ns <= REAL.event_slice_ns);
    CHECK(!s.wait_without_rearm);
    CHECK_EQ(r.counters.would_blocks, (uint32_t)s.tries);
    CHECK_EQ(r.counters.event_wakes, 0u);
    CHECK(s.waits >= 5 && s.waits <= 6);  // 20 ms in 4 ms slices
    CHECK(s.tries <= max_tries(REAL));
}

// The libnx failure mode: event signalled once and never reset (here the
// reset does not even work). Must not spin: attempts stay bounded and are
// paced at >= 1 ms per wait round.
void test_stuck_signalled_event() {
    Sim s;
    s.stuck_signalled = true;
    const Run r = run(s);
    CHECK(r.outcome == bq::Outcome::TimedOut);
    CHECK(r.elapsed >= REAL.deadline_ns);
    CHECK(r.elapsed <= REAL.deadline_ns + s.try_cost);
    CHECK(s.latest_end <= r.start + REAL.deadline_ns);
    CHECK(s.tries <= max_tries(REAL));
    CHECK(s.tries >= 20);  // still keeps trying for the whole budget
    CHECK(s.sleeps > 0);
    CHECK_EQ(r.counters.event_wakes, (uint32_t)s.waits);
    CHECK(!s.wait_without_rearm);

    // Same with an event that only sticks after the first signal.
    Sim s2;
    s2.clear_works = false;
    s2.signalled = true;
    const Run r2 = run(s2);
    CHECK(r2.outcome == bq::Outcome::TimedOut);
    CHECK(s2.tries <= max_tries(REAL));
    CHECK(r2.elapsed <= REAL.deadline_ns + s2.try_cost);
}

// No event at all: plain 1 ms polling.
void test_no_event() {
    Sim s;
    s.event = false;
    const Run r = run(s);
    CHECK(r.outcome == bq::Outcome::TimedOut);
    CHECK_EQ(s.clears, 0);
    CHECK_EQ(s.waits, 0);
    CHECK(s.tries <= (int)(REAL.deadline_ns / REAL.min_retry_ns) + 2);
    CHECK(s.tries >= 15);
    CHECK(r.elapsed <= REAL.deadline_ns + s.try_cost);
    CHECK(s.latest_end <= r.start + REAL.deadline_ns);
}

// Normal case: a buffer is released (and signalled) 7 ms in; the wait wakes
// on it and the next attempt gets it.
void test_release_signalled() {
    Sim s;
    s.release_at = s.t + 7 * MS;
    const Run r = run(s);
    CHECK(r.outcome == bq::Outcome::Got);
    CHECK(r.elapsed >= 7 * MS);
    CHECK(r.elapsed <= 7 * MS + 2 * s.try_cost + REAL.min_retry_ns);
    CHECK(r.counters.event_wakes >= 1u);
    CHECK(!s.wait_without_rearm);
}

// HOS never signals the release: picked up by the next slice anyway.
void test_release_unsignalled() {
    Sim s;
    s.release_at = s.t + 7 * MS;
    s.signal_on_release = false;
    const Run r = run(s);
    CHECK(r.outcome == bq::Outcome::Got);
    CHECK(r.elapsed <= 7 * MS + REAL.event_slice_ns + 3 * s.try_cost);
    CHECK_EQ(r.counters.event_wakes, 0u);
}

// The release (and its signal) lands while the WouldBlock reply is in
// flight, i.e. before the reset: the reset wipes the signal, but the extra
// attempt after the reset gets the buffer, without any wait.
void test_release_before_reset() {
    Sim s;
    s.release_at = s.t + s.try_cost / 2;
    const Run r = run(s);
    CHECK(r.outcome == bq::Outcome::Got);
    CHECK_EQ(s.tries, 2);
    CHECK_EQ(s.clears, 1);
    CHECK_EQ(s.waits, 0);
    CHECK(r.elapsed <= 2 * s.try_cost);
}

// Release at various points across the budget: always Got, never later than
// one slice after the release, never a wait without the reset + retry.
void test_release_sweep() {
    for (uint64_t at = 0; at < REAL.deadline_ns - REAL.event_slice_ns; at += 250000) {
        for (int sig = 0; sig < 2; ++sig) {
            Sim s;
            s.release_at = s.t + at;
            s.signal_on_release = sig != 0;
            const Run r = run(s);
            CHECK_ALL(r.outcome == bq::Outcome::Got, "release at %llu ns, signal %d",
                      (unsigned long long)at, sig);
            CHECK_ALL(r.elapsed <= at + REAL.event_slice_ns + REAL.min_retry_ns + 3 * s.try_cost,
                      "release at %llu ns, signal %d: took %llu ns", (unsigned long long)at, sig,
                      (unsigned long long)r.elapsed);
            CHECK_ALL(!s.wait_without_rearm, "release at %llu ns", (unsigned long long)at);
        }
    }
}

void test_failure_stops_at_once() {
    Sim s;
    s.fail_at_try = 3;
    const Run r = run(s);
    CHECK(r.outcome == bq::Outcome::Failed);
    CHECK_EQ(s.tries, 3);
    CHECK_EQ(r.counters.would_blocks, 2u);
    CHECK(s.ops.back() == Op::Try);  // nothing after the failing attempt
}

void test_zero_deadline() {
    Sim s;
    const bq::Timing t = {0, 4 * MS, 1 * MS};
    const Run r = run(s, t);
    CHECK(r.outcome == bq::Outcome::TimedOut);
    CHECK_EQ(s.tries, 1);
    CHECK_EQ(s.waits + s.sleeps + s.clears, 0);
}

// A slow binder (each attempt 3 ms): still bounded by the deadline plus one
// attempt.
void test_slow_ipc() {
    Sim s;
    s.try_cost = 3 * MS;
    const Run r = run(s);
    CHECK(r.outcome == bq::Outcome::TimedOut);
    CHECK(r.elapsed <= REAL.deadline_ns + s.try_cost);
    CHECK(s.tries <= max_tries(REAL));
}

// --- wait_fences -----------------------------------------------------------

struct FenceSim {
    uint64_t t = 1000 * MS;
    std::vector<uint64_t> signal_after_ns;  // per fence, from when it is waited
    std::vector<int32_t> timeouts;
    int fail_at = -1;  // this fence's wait fails at once (not a timeout)

    uint64_t now_ns() { return t; }
    bq::Fence fence_wait(uint32_t i, int32_t timeout_us) {
        timeouts.push_back(timeout_us);
        if ((int)i == fail_at) return bq::Fence::Failed;
        const uint64_t need = signal_after_ns[i];
        const uint64_t limit = (uint64_t)(timeout_us < 0 ? 0 : timeout_us) * 1000;
        if (need <= limit) {
            t += need;
            return bq::Fence::Signalled;
        }
        t += limit;
        return bq::Fence::TimedOut;
    }
};

void test_fences() {
    // Quick fences: all waited, each with what is left.
    FenceSim a;
    a.signal_after_ns = {1 * MS, 2 * MS, 0};
    CHECK(bq::wait_fences(a, 3, 20000) == bq::Fence::Signalled);
    CHECK_EQ(a.timeouts.size(), 3u);
    CHECK_EQ(a.timeouts[0], 20000);
    CHECK_EQ(a.timeouts[1], 19000);
    CHECK_EQ(a.timeouts[2], 17000);

    // One shared budget: 8 + 8 ms leaves 4 ms for the third, which needs 8.
    FenceSim b;
    b.signal_after_ns = {8 * MS, 8 * MS, 8 * MS, 8 * MS};
    const uint64_t start = b.t;
    CHECK(bq::wait_fences(b, 4, 20000) == bq::Fence::TimedOut);
    CHECK_EQ(b.timeouts.size(), 3u);  // stops at the first failure
    CHECK_EQ(b.timeouts[2], 4000);
    CHECK(b.t - start <= 20 * MS);

    // Budget used up: later fences are polled with 0, never a negative
    // (= infinite in libnx) timeout.
    FenceSim c;
    c.signal_after_ns = {20 * MS, 0, 0};
    CHECK(bq::wait_fences(c, 3, 20000) == bq::Fence::Signalled);
    CHECK_EQ(c.timeouts.size(), 3u);
    CHECK_EQ(c.timeouts[1], 0);
    CHECK_EQ(c.timeouts[2], 0);
    for (int32_t to : c.timeouts) CHECK(to >= 0);

    FenceSim d;
    CHECK(bq::wait_fences(d, 0, 20000) == bq::Fence::Signalled);
    CHECK(d.timeouts.empty());

    // A wait that fails (not a timeout) is reported as Failed, not TimedOut,
    // so the caller can use the buffer instead of skipping forever; it stops
    // there, like libnx's nvMultiFenceWait().
    FenceSim e;
    e.signal_after_ns = {1 * MS, 0, 0};
    e.fail_at = 1;
    CHECK(bq::wait_fences(e, 3, 20000) == bq::Fence::Failed);
    CHECK_EQ(e.timeouts.size(), 2u);
}

} // namespace

int main() {
    test_immediate();
    test_never_released();
    test_stuck_signalled_event();
    test_no_event();
    test_release_signalled();
    test_release_unsignalled();
    test_release_before_reset();
    test_release_sweep();
    test_failure_stops_at_once();
    test_zero_deadline();
    test_slow_ipc();
    test_fences();
    return check::summary("dequeue");
}
