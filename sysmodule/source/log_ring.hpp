// Kinestop sysmodule - the byte ring and size budget behind the async log.
//
// Part of the sysmodule/overlay split of the original Kinestop by SertAy
// (https://github.com/sertay1/Kinestop).
//
// Header-only and free of libnx so the host tests (tests/test_log_ring.cpp)
// can check it; log.cpp adds the locking, the logger thread and the SD I/O.
// Nothing here locks: log.cpp calls push()/snapshot()/consume() under
// g_ring_mutex and reads queued bytes (at(), spans()) without it, which is
// safe because writers only ever append into free space and only the
// (serialised) drain frees bytes.

#pragma once

#include <cstring>

namespace kinestop::log {

template <unsigned N>
struct Ring {
    static_assert(N > 0, "empty ring");

    char buf[N];
    unsigned start;    // index of the oldest queued byte
    unsigned used;     // queued bytes
    unsigned dropped;  // lines dropped since the last snapshot()

    static constexpr unsigned capacity() { return N; }

    // Appends the whole line or nothing: a line that does not fit is
    // dropped and counted, never truncated or split across a drain.
    bool push(const char* line, unsigned n) {
        if (n > N - used) {
            dropped++;
            return false;
        }
        const unsigned pos = (start + used) % N;
        const unsigned first = n < N - pos ? n : N - pos;
        std::memcpy(buf + pos, line, first);
        if (n > first) std::memcpy(buf, line + first, n - first);
        used += n;
        return true;
    }

    struct Snapshot {
        unsigned start, used, dropped;
    };

    // What a drain will write; takes (resets) the drop counter.
    Snapshot snapshot() {
        const Snapshot s{start, used, dropped};
        dropped = 0;
        return s;
    }

    // Frees `len` bytes from the front (the ones a snapshot covered).
    void consume(unsigned len) {
        if (len > used) len = used;
        start = (start + len) % N;
        used -= len;
    }

    // Byte `i` of the queued data that begins at `from`.
    char at(unsigned from, unsigned i) const { return buf[(from + i) % N]; }

    // [from, from + len) as at most two contiguous pieces.
    struct Spans {
        const char* a;
        unsigned a_len;
        const char* b;
        unsigned b_len;
    };
    Spans spans(unsigned from, unsigned len) const {
        const unsigned first = len < N - from ? len : N - from;
        return Spans{buf + from, first, buf, len - first};
    }
};

// How much of a snapshot one drain writes, given the file-size budget.
struct DrainPlan {
    unsigned take;    // queued bytes to write (a whole-line prefix)
    bool with_note;   // write the "(N lines dropped)" note
    bool hit_limit;   // write the limit note; the file is full after this
};

// Budget rule: bytes_written + limit_note_len <= max_bytes always holds, so
// the closing limit note always fits and the file never exceeds max_bytes.
// When the queued bytes plus the drop note do not fit, the largest
// whole-line prefix that does is written, the drop note is skipped and the
// limit note closes the file.
template <unsigned N>
DrainPlan plan_drain(const Ring<N>& ring, unsigned from, unsigned used, unsigned note_len,
                     unsigned bytes_written, unsigned max_bytes, unsigned limit_note_len) {
    const unsigned room = max_bytes - limit_note_len - bytes_written;
    if (used + note_len <= room) return DrainPlan{used, note_len != 0, false};

    unsigned take = 0;
    for (unsigned i = used < room ? used : room; i > 0; --i) {
        if (ring.at(from, i - 1) == '\n') {
            take = i;
            break;
        }
    }
    return DrainPlan{take, false, true};
}

} // namespace kinestop::log
