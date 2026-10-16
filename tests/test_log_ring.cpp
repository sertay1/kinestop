// Kinestop host tests - sysmodule/source/log_ring.hpp, the ring buffer and
// file-size budget behind the async log (log.cpp): whole-line push or drop,
// drop counting, wraparound, appends that land while a drain is writing,
// and the MAX_BYTES budget with its closing limit note.
//
// Part of the sysmodule/overlay split of the original Kinestop by SertAy
// (https://github.com/sertay1/Kinestop).

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>

#include "check.hpp"
#include "log.hpp"
#include "log_ring.hpp"

namespace lg = kinestop::log;

namespace {

constexpr char LIMIT_NOTE[] = "(log size limit reached)\n";
constexpr unsigned LIMIT_NOTE_LEN = sizeof(LIMIT_NOTE) - 1;

// Deterministic PRNG (xorshift32) so failures reproduce.
struct Rng {
    uint32_t s;
    uint32_t next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    uint32_t below(uint32_t n) { return next() % n; }
};

// A line of `len` bytes (>= 1) ending in '\n', content tagged by `id`.
std::string make_line(unsigned id, unsigned len) {
    std::string s;
    for (unsigned i = 0; i + 1 < len; ++i) s.push_back((char)('a' + (id + i) % 26));
    s.push_back('\n');
    return s;
}

// The file side of log.cpp's drain(), on a std::string instead of the SD
// card. Mirrors drain() step by step: snapshot under the lock, decide with
// plan_drain(), write the spans, then consume what the snapshot covered.
template <unsigned N>
struct Sink {
    lg::Ring<N>& ring;
    unsigned max_bytes;
    std::string file;
    unsigned bytes = 0;
    bool full = false;

    Sink(lg::Ring<N>& r, unsigned cap) : ring(r), max_bytes(cap) {}

    // `mid` runs between the snapshot and consume(), i.e. while the drain
    // is "writing" with the ring lock released (writers keep appending).
    template <class Mid>
    void drain(Mid mid) {
        const auto snap = ring.snapshot();
        if (snap.used == 0 && snap.dropped == 0) {
            mid();
            return;
        }
        if (!full) {
            char note[64];
            unsigned note_len = 0;
            if (snap.dropped) {
                const int n = std::snprintf(note, sizeof(note), "(%u log lines dropped: ring buffer full)\n",
                                            snap.dropped);
                note_len = (unsigned)n;
            }
            const lg::DrainPlan plan =
                lg::plan_drain(ring, snap.start, snap.used, note_len, bytes, max_bytes, LIMIT_NOTE_LEN);
            if (!plan.with_note) note_len = 0;
            const auto sp = ring.spans(snap.start, plan.take);
            mid();
            file.append(sp.a, sp.a_len);
            file.append(sp.b, sp.b_len);
            file.append(note, note_len);
            if (plan.hit_limit) file.append(LIMIT_NOTE, LIMIT_NOTE_LEN);
            bytes += plan.take + note_len + (plan.hit_limit ? LIMIT_NOTE_LEN : 0);
            if (plan.hit_limit) full = true;
        } else {
            mid();
        }
        ring.consume(snap.used);
    }
    void drain() {
        drain([] {});
    }
};

template <unsigned N>
lg::Ring<N> empty_ring() {
    return lg::Ring<N>{};
}

void test_push_and_drop() {
    auto r = empty_ring<64>();
    CHECK_EQ(r.used, 0u);
    CHECK_EQ(r.dropped, 0u);

    const std::string a = make_line(0, 40), b = make_line(1, 24), c = make_line(2, 1);
    CHECK(r.push(a.data(), (unsigned)a.size()));
    CHECK(r.push(b.data(), (unsigned)b.size()));  // exactly full: 64 of 64
    CHECK_EQ(r.used, 64u);
    CHECK(!r.push(c.data(), (unsigned)c.size()));  // even 1 byte is dropped, never split
    CHECK(!r.push(a.data(), (unsigned)a.size()));
    CHECK_EQ(r.used, 64u);
    CHECK_EQ(r.dropped, 2u);

    // A line longer than the whole ring is dropped too.
    auto big = empty_ring<16>();
    const std::string huge = make_line(3, 17);
    CHECK(!big.push(huge.data(), (unsigned)huge.size()));
    CHECK_EQ(big.used, 0u);
    CHECK_EQ(big.dropped, 1u);

    // snapshot() hands the drop count over and resets it.
    const auto s = r.snapshot();
    CHECK_EQ(s.start, 0u);
    CHECK_EQ(s.used, 64u);
    CHECK_EQ(s.dropped, 2u);
    CHECK_EQ(r.dropped, 0u);
    r.consume(s.used);
    CHECK_EQ(r.used, 0u);
    CHECK_EQ(r.start, 0u);  // 64 % 64
    CHECK(r.push(a.data(), (unsigned)a.size()));

    // consume() never frees more than is queued.
    r.consume(1000);
    CHECK_EQ(r.used, 0u);
}

void test_wraparound_spans() {
    auto r = empty_ring<32>();
    const std::string x = make_line(0, 20);
    CHECK(r.push(x.data(), 20));
    r.consume(r.snapshot().used);
    CHECK_EQ(r.start, 20u);

    // 20 bytes starting at 20 wrap: 12 at the end, 8 at the front.
    const std::string y = make_line(7, 20);
    CHECK(r.push(y.data(), 20));
    const auto sp = r.spans(r.start, r.used);
    CHECK_EQ(sp.a_len, 12u);
    CHECK_EQ(sp.b_len, 8u);
    CHECK(sp.a == r.buf + 20);
    CHECK(sp.b == r.buf);
    std::string got(sp.a, sp.a_len);
    got.append(sp.b, sp.b_len);
    CHECK(got == y);
    for (unsigned i = 0; i < 20; ++i) CHECK_ALL(r.at(r.start, i) == y[i], "at(%u)", i);

    // Not wrapping: a single piece, empty second one.
    const auto sp2 = r.spans(0, 5);
    CHECK_EQ(sp2.a_len, 5u);
    CHECK_EQ(sp2.b_len, 0u);
    // Starting exactly at the end of the buffer cannot happen (start < N),
    // but a span ending exactly at it is one piece.
    const auto sp3 = r.spans(20, 12);
    CHECK_EQ(sp3.a_len, 12u);
    CHECK_EQ(sp3.b_len, 0u);
}

// Random pushes and drains against a model: every accepted line reaches the
// file once, in order, intact; every rejected one is counted exactly once
// in a drop note; the ring wraps many times.
// About one step in `drain_one_in` is a drain, the rest are pushes.
template <unsigned N>
void run_model(uint32_t seed, unsigned max_line, unsigned drain_one_in, unsigned steps) {
    auto r = empty_ring<N>();
    Sink<N> sink{r, 0xFFFFFFFFu};
    Rng rng{seed};

    std::string expected;
    unsigned model_used = 0;       // what the model thinks is queued
    unsigned pending_drops = 0;    // drops since the last snapshot
    unsigned id = 0;
    unsigned total_dropped = 0, wraps = 0;
    bool ok = true;

    auto push_one = [&] {
        const unsigned len = 1 + rng.below(max_line);
        const std::string line = make_line(id++, len);
        const bool fits = model_used + len <= N;
        const bool pushed = r.push(line.data(), len);
        if (pushed != fits) ok = false;
        if (pushed) {
            model_used += len;
        } else {
            pending_drops++;
            total_dropped++;
        }
        return std::pair<bool, std::string>(pushed, line);
    };

    // Lines accepted but not yet drained, in order.
    std::deque<std::string> queued;

    for (unsigned step = 0; step < steps; ++step) {
        if (rng.below(drain_one_in) != 0) {
            auto [pushed, line] = push_one();
            if (pushed) queued.push_back(line);
            continue;
        }

        // A drain, with writers appending in the middle of it.
        const unsigned snap_used = model_used;
        const unsigned snap_drops = pending_drops;
        pending_drops = 0;
        std::deque<std::string> during;
        const unsigned start_before = r.start;
        sink.drain([&] {
            const unsigned k = rng.below(3);
            for (unsigned i = 0; i < k; ++i) {
                auto [pushed, line] = push_one();
                if (pushed) during.push_back(line);  // a drop is counted in pending_drops
            }
        });
        if (r.start < start_before) wraps++;
        // The snapshot covered exactly the lines queued before it.
        unsigned covered = 0;
        while (covered < snap_used) {
            if (queued.empty()) { ok = false; break; }
            covered += (unsigned)queued.front().size();
            expected += queued.front();
            queued.pop_front();
        }
        if (covered != snap_used) ok = false;
        if (snap_drops) {
            char note[64];
            std::snprintf(note, sizeof(note), "(%u log lines dropped: ring buffer full)\n", snap_drops);
            expected += note;
        }
        model_used -= snap_used;
        // Lines pushed mid-drain stay queued for the next one; their drops
        // are reported by the next drain.
        for (auto& l : during) queued.push_back(l);
        if (r.used != model_used) ok = false;
    }
    // Final drain (log::exit()).
    const unsigned final_drops = pending_drops;
    sink.drain();
    for (auto& l : queued) expected += l;
    if (final_drops) {
        char note[64];
        std::snprintf(note, sizeof(note), "(%u log lines dropped: ring buffer full)\n", final_drops);
        expected += note;
    }

    CHECK_ALL(ok, "model mismatch, seed %u N %u", seed, N);
    CHECK_ALL(sink.file == expected, "file content, seed %u N %u (%zu vs %zu bytes)", seed, N, sink.file.size(),
              expected.size());
    CHECK_EQ(r.used, 0u);
    CHECK_EQ(r.dropped, 0u);
    CHECK_ALL(wraps > 10, "only %u wraps, seed %u N %u", wraps, seed, N);
    CHECK_ALL(total_dropped > 0, "no drops exercised, seed %u N %u", seed, N);
}

void test_model() {
    for (uint32_t seed = 1; seed <= 20; ++seed) {
        run_model<64>(seed * 2654435761u, 40, 4, 4000);
        run_model<97>(seed * 40503u + 7u, 60, 4, 4000);          // N not a power of two
        run_model<lg::RING_BYTES>(seed + 1000u, 320, 100, 30000);  // the real ring, write()'s 320-byte line cap
    }
}

// The diagnostics cadence the sysmodule actually produces (one ~280 byte
// line per second plus a few events, a drain every 500 ms) never drops.
void test_real_rate_never_drops() {
    auto r = empty_ring<lg::RING_BYTES>();
    Sink<lg::RING_BYTES> sink{r, lg::MAX_BYTES};
    const std::string diag = make_line(1, 280), event = make_line(2, 90);
    unsigned dropped = 0;
    for (int half_seconds = 0; half_seconds < 600; ++half_seconds) {
        if (!r.push(diag.data(), (unsigned)diag.size())) dropped++;
        if (half_seconds % 7 == 0 && !r.push(event.data(), (unsigned)event.size())) dropped++;
        sink.drain();
    }
    CHECK_EQ(dropped, 0u);
    // A stalled logger thread (SD card busy) for 20 s at 1 Hz still fits.
    for (int s = 0; s < 20; ++s)
        if (!r.push(diag.data(), (unsigned)diag.size())) dropped++;
    CHECK_EQ(dropped, 0u);
}

// MAX_BYTES budget: the file never exceeds the cap, only whole lines are
// written, the limit note is the last thing in the file and nothing
// follows it.
void test_budget() {
    // Hand-checked plans.
    {
        auto r = empty_ring<64>();
        const std::string a = make_line(0, 10), b = make_line(1, 10);
        r.push(a.data(), 10);
        r.push(b.data(), 10);
        // Plenty of room.
        auto p = lg::plan_drain(r, r.start, r.used, 5, 0, 1000, LIMIT_NOTE_LEN);
        CHECK_EQ(p.take, 20u);
        CHECK(p.with_note);
        CHECK(!p.hit_limit);
        // Exactly fits (with the limit note reserved).
        p = lg::plan_drain(r, r.start, r.used, 0, 100, 100 + 20 + LIMIT_NOTE_LEN, LIMIT_NOTE_LEN);
        CHECK_EQ(p.take, 20u);
        CHECK(!p.hit_limit);
        // The drop note tips it over: whole lines only, no drop note.
        p = lg::plan_drain(r, r.start, r.used, 1, 100, 100 + 20 + LIMIT_NOTE_LEN, LIMIT_NOTE_LEN);
        CHECK_EQ(p.take, 20u);
        CHECK(!p.with_note);
        CHECK(p.hit_limit);
        // Room for 15: only the first line.
        p = lg::plan_drain(r, r.start, r.used, 0, 0, 15 + LIMIT_NOTE_LEN, LIMIT_NOTE_LEN);
        CHECK_EQ(p.take, 10u);
        CHECK(p.hit_limit);
        // Room for 9: nothing but the limit note.
        p = lg::plan_drain(r, r.start, r.used, 0, 0, 9 + LIMIT_NOTE_LEN, LIMIT_NOTE_LEN);
        CHECK_EQ(p.take, 0u);
        CHECK(p.hit_limit);
        // No room at all, only drops pending: the limit note still fits.
        p = lg::plan_drain(r, r.start, 0, 40, 0, LIMIT_NOTE_LEN, LIMIT_NOTE_LEN);
        CHECK_EQ(p.take, 0u);
        CHECK(!p.with_note);
        CHECK(p.hit_limit);
    }

    // Random lines into a small cap, with wraparound and drops.
    for (uint32_t seed = 1; seed <= 50; ++seed) {
        auto r = empty_ring<128>();
        const unsigned cap = 400 + seed * 37;
        Sink<128> sink{r, cap};
        Rng rng{seed * 977u};
        unsigned id = 0;
        for (int step = 0; step < 3000; ++step) {
            if (rng.below(3)) {
                const std::string line = make_line(id++, 1 + rng.below(50));
                r.push(line.data(), (unsigned)line.size());
            } else {
                sink.drain();
            }
        }
        sink.drain();

        const std::string& f = sink.file;
        CHECK_ALL(f.size() <= cap, "seed %u: %zu > cap %u", seed, f.size(), cap);
        CHECK_ALL(sink.bytes == f.size(), "seed %u: byte count", seed);
        CHECK_ALL(sink.full, "seed %u: cap never reached", seed);
        const size_t at = f.find(LIMIT_NOTE);
        CHECK_ALL(at != std::string::npos && at + LIMIT_NOTE_LEN == f.size(), "seed %u: limit note not last", seed);
        CHECK_ALL(f.empty() || f.back() == '\n', "seed %u: partial line", seed);
        // Every line before the note is one make_line() produced, whole.
        bool whole = true;
        size_t pos = 0;
        while (pos < at) {
            const size_t nl = f.find('\n', pos);
            if (nl == std::string::npos || nl >= at) { whole = false; break; }
            const std::string line = f.substr(pos, nl + 1 - pos);
            if (line[0] != '(') {
                // Content is a run of consecutive letters.
                for (size_t i = 1; i + 1 < line.size(); ++i)
                    if ((line[i] - 'a') != (line[i - 1] - 'a' + 1) % 26) whole = false;
            }
            pos = nl + 1;
        }
        CHECK_ALL(whole, "seed %u: torn line", seed);
        CHECK_EQ(r.used, 0u);  // past the cap, drains still free the ring
    }
}

} // namespace

int main() {
    test_push_and_drop();
    test_wraparound_spans();
    test_model();
    test_real_rate_never_drops();
    test_budget();
    return check::summary("log_ring");
}
