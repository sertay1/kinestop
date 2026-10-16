// Kinestop sysmodule - deadline policy for getting a buffer from the
// compositor without ever blocking or spinning.
//
// Original Kinestop by SertAy (https://github.com/sertay1/Kinestop). The
// sysmodule/overlay split is a modification of the original project.
//
// Header-only and free of libnx so the host tests (tests/test_dequeue.cpp)
// can drive it with a virtual clock. Renderer::dequeue_bounded() plugs the
// real calls in (bqDequeueBuffer, the binder buffer event, svcSleepThread,
// nvFenceWait).
//
// Why: libnx's nwindowDequeueBuffer() retries an async dequeue around
// eventWait(UINT64_MAX), and the binder buffer event is loaded with
// autoclear=false and never reset. Once signalled, every later wait returns
// at once (a busy spin of binder IPCs); if never signalled, it blocks
// forever. It then waits the release fences with no timeout.

#pragma once

#include <cstdint>

namespace kinestop::bounded {

// Result of one non-blocking dequeue attempt.
enum class Try : uint8_t {
    Got,         // a slot was handed out
    WouldBlock,  // no free buffer right now
    Failed,      // any other error: give up, the caller re-creates the layer
};

enum class Outcome : uint8_t {
    Got,
    TimedOut,  // no buffer before the deadline: skip this frame
    Failed,
};

struct Timing {
    uint64_t deadline_ns;     // total budget for the whole dequeue
    uint64_t event_slice_ns;  // longest single wait on the release event
    uint64_t min_retry_ns;    // minimum spacing between attempts that follow a wait
};

struct Counters {
    uint32_t would_blocks;  // WouldBlock replies
    uint32_t event_wakes;   // event waits that returned signalled
};

// Retries ops.try_dequeue() until it stops answering WouldBlock or the
// deadline passes. Ops provides:
//   uint64_t now_ns();                     monotonic clock
//   Try      try_dequeue();
//   bool     has_event();                  release event available (constant)
//   void     clear_event();                reset the (autoclear=false) event
//   bool     wait_event(uint64_t ns);      true when it returned signalled
//   void     sleep_ns(uint64_t ns);
//
// Guarantees (tests/test_dequeue.cpp):
//   * returns once a WouldBlock is seen at or after start + deadline_ns, so
//     it never runs much past the deadline: no single wait or sleep extends
//     beyond it;
//   * before every event wait the event is reset and one more attempt is
//     made, so a release that signalled the event before the reset is still
//     picked up, and a stuck-signalled event cannot make it spin;
//   * consecutive wait rounds are at least min_retry_ns apart even when the
//     event wait returns at once, so attempts are bounded by about
//     2 * deadline_ns / min_retry_ns + 2.
template <class Ops>
Outcome dequeue_until_deadline(Ops& ops, const Timing& t, Counters& c) {
    const uint64_t deadline = ops.now_ns() + t.deadline_ns;
    const bool have_event = ops.has_event();
    bool rearmed = false;

    for (;;) {
        const Try r = ops.try_dequeue();
        if (r == Try::Got) return Outcome::Got;
        if (r == Try::Failed) return Outcome::Failed;
        c.would_blocks++;

        const uint64_t now = ops.now_ns();
        if (now >= deadline) return Outcome::TimedOut;

        if (have_event && !rearmed) {
            // Reset, then dequeue once more before waiting: a release between
            // the WouldBlock above and the reset is then picked up by that
            // dequeue instead of being lost with the cleared signal.
            ops.clear_event();
            rearmed = true;
            continue;
        }
        rearmed = false;

        const uint64_t remaining = deadline - now;
        uint64_t slept = 0;
        if (have_event) {
            const uint64_t slice = remaining < t.event_slice_ns ? remaining : t.event_slice_ns;
            if (ops.wait_event(slice)) c.event_wakes++;
            slept = ops.now_ns() - now;
        }
        if (slept < t.min_retry_ns) {
            const uint64_t after = ops.now_ns();
            const uint64_t left = deadline > after ? deadline - after : 0;
            const uint64_t pause = t.min_retry_ns - slept;
            ops.sleep_ns(pause < left ? pause : left);
        }
    }
}

// One release-fence wait, as reported by Ops::fence_wait().
enum class Fence : uint8_t {
    Signalled,
    TimedOut,  // not signalled within the timeout (only this means "not yet")
    Failed,    // any other error: the wait itself is broken, not the fence
};

// Waits `count` release fences with ONE shared budget (nvMultiFenceWait
// applies its timeout to each fence separately). Ops provides now_ns() and
//   Fence fence_wait(uint32_t index, int32_t timeout_us);
// Every fence is waited with what is left of the budget (0 once it is used
// up, i.e. a poll). Stops at the first fence that did not signal and returns
// how it ended (TimedOut or Failed); Signalled when all of them did.
//
// Failed is NOT a timeout: libnx's nwindowDequeueBuffer() ignores the
// nvMultiFenceWait() Result and draws anyway, and a caller that treated a
// wait that always fails as "not yet" would never present again.
template <class Ops>
Fence wait_fences(Ops& ops, uint32_t count, int64_t budget_us) {
    const uint64_t start = ops.now_ns();
    for (uint32_t i = 0; i < count; ++i) {
        const int64_t used_us = (int64_t)((ops.now_ns() - start) / 1000);
        const int64_t left_us = budget_us - used_us;
        const Fence r = ops.fence_wait(i, (int32_t)(left_us > 0 ? left_us : 0));
        if (r != Fence::Signalled) return r;
    }
    return Fence::Signalled;
}

} // namespace kinestop::bounded
