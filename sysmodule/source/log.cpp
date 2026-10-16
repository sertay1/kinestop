// Kinestop sysmodule - tiny bounded log file, written asynchronously.
//
// Part of the sysmodule/overlay split of the original Kinestop by SertAy
// (https://github.com/sertay1/Kinestop).
//
// Two locks:
//   g_ring_mutex  - the ring buffer, drop counter and timing counters. Held
//                   only for memcpy-sized work, never across SD I/O, so
//                   write() costs the caller microseconds.
//   g_file_mutex  - serialises drains (logger thread, exit(), and the
//                   no-thread fallback) and guards the file-size budget.
//
// The drain snapshots the occupied part of the ring, releases the ring lock,
// writes that region straight from the ring and only then frees it. Writers
// only ever append into free space, so the region being written is never
// touched while the lock is not held. The ring and the size budget are in
// log_ring.hpp (host-tested in tests/test_log_ring.cpp).

#include "log.hpp"
#include "log_ring.hpp"

#include <switch.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace kinestop::log {

namespace {

constexpr char LIMIT_NOTE[] = "(log size limit reached)\n";
constexpr unsigned LIMIT_NOTE_LEN = sizeof(LIMIT_NOTE) - 1;

// --- ring (g_ring_mutex) ---
Mutex g_ring_mutex;
Ring<RING_BYTES> g_ring{};     // .bss
unsigned g_max_write_us = 0;   // longest write() since take_max_write_us()
unsigned g_max_drain_us = 0;   // longest drain since take_max_drain_us()
bool g_thread_running = false;
bool g_stop = false;

// --- file (g_file_mutex) ---
Mutex g_file_mutex;
unsigned g_bytes = 0;          // bytes written to the file since init()
bool g_full = false;

Thread g_thread;
UEvent g_wake;

unsigned elapsed_us(u64 start_tick) {
    const u64 us = armTicksToNs(armGetSystemTick() - start_tick) / 1000ULL;
    return us > 0xFFFFFFFFull ? 0xFFFFFFFFu : (unsigned)us;
}

// write(2) the ring bytes [start, start + len) (logical, may wrap).
void write_ring(int fd, unsigned start, unsigned len) {
    const auto sp = g_ring.spans(start, len);
    if (sp.a_len) ::write(fd, sp.a, sp.a_len);
    if (sp.b_len) ::write(fd, sp.b, sp.b_len);
}

// Writes everything queued so far to the file. Caller holds g_file_mutex.
void drain() {
    mutexLock(&g_ring_mutex);
    const auto snap = g_ring.snapshot();
    mutexUnlock(&g_ring_mutex);
    const unsigned start = snap.start;
    const unsigned used = snap.used;
    const unsigned dropped = snap.dropped;

    if (used == 0 && dropped == 0) return;

    if (!g_full) {
        char note[64];
        unsigned note_len = 0;
        if (dropped) {
            const int n = sniprintf(note, sizeof(note), "(%u log lines dropped: ring buffer full)\n", dropped);
            note_len = n < 0 ? 0 : ((unsigned)n >= sizeof(note) ? sizeof(note) - 1 : (unsigned)n);
        }

        // The final limit note is budgeted too, so the file never exceeds
        // MAX_BYTES. Invariant: g_bytes + LIMIT_NOTE_LEN <= MAX_BYTES.
        const DrainPlan plan = plan_drain(g_ring, start, used, note_len, g_bytes, MAX_BYTES, LIMIT_NOTE_LEN);
        const unsigned take = plan.take;
        const bool hit_limit = plan.hit_limit;
        if (!plan.with_note) note_len = 0;

        const u64 t0 = armGetSystemTick();
        // Open/append/close per drain (at most twice a second): nothing that
        // reached the file is lost if pm terminates the process, and no
        // stdio FILE buffer is allocated.
        const int fd = ::open(PATH, O_WRONLY | O_CREAT | O_APPEND, 0666);
        if (fd >= 0) {
            write_ring(fd, start, take);
            if (note_len) ::write(fd, note, note_len);
            if (hit_limit) ::write(fd, LIMIT_NOTE, LIMIT_NOTE_LEN);
            ::close(fd);
        }
        g_bytes += take + note_len + (hit_limit ? LIMIT_NOTE_LEN : 0);
        if (hit_limit) g_full = true;
        const unsigned us = elapsed_us(t0);

        mutexLock(&g_ring_mutex);
        if (us > g_max_drain_us) g_max_drain_us = us;
        mutexUnlock(&g_ring_mutex);
    }

    // Everything snapshotted is consumed: written, or past the size limit.
    mutexLock(&g_ring_mutex);
    g_ring.consume(used);
    mutexUnlock(&g_ring_mutex);
}

void drain_locked() {
    mutexLock(&g_file_mutex);
    drain();
    mutexUnlock(&g_file_mutex);
}

void thread_main(void* arg) {
    (void)arg;
    while (true) {
        // Times out every DRAIN_PERIOD_NS; flush() and exit() wake it early.
        waitSingle(waiterForUEvent(&g_wake), DRAIN_PERIOD_NS);
        drain_locked();

        mutexLock(&g_ring_mutex);
        const bool stop = g_stop;
        mutexUnlock(&g_ring_mutex);
        if (stop) break;
    }
}

} // namespace

void init() {
    mutexLock(&g_file_mutex);
    mkdir("sdmc:/config", 0777);
    mkdir("sdmc:/config/kinestop", 0777);
    const int fd = ::open(PATH, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd >= 0) ::close(fd);
    g_bytes = 0;
    g_full = false;
    mutexUnlock(&g_file_mutex);

    ueventCreate(&g_wake, true);
    Result rc = threadCreate(&g_thread, thread_main, nullptr, nullptr, THREAD_STACK_SIZE, THREAD_PRIORITY, THREAD_CORE);
    if (R_SUCCEEDED(rc)) {
        rc = threadStart(&g_thread);
        if (R_FAILED(rc)) threadClose(&g_thread);
    }
    mutexLock(&g_ring_mutex);
    g_thread_running = R_SUCCEEDED(rc);
    g_stop = false;
    mutexUnlock(&g_ring_mutex);
    if (R_FAILED(rc))
        write("logger thread failed: rc 0x%X, logging synchronously", (unsigned)rc);
}

void exit() {
    mutexLock(&g_ring_mutex);
    const bool running = g_thread_running;
    g_stop = true;
    mutexUnlock(&g_ring_mutex);

    if (running) {
        ueventSignal(&g_wake);
        threadWaitForExit(&g_thread);
        threadClose(&g_thread);
        mutexLock(&g_ring_mutex);
        g_thread_running = false;  // later write()s drain synchronously
        mutexUnlock(&g_ring_mutex);
    }
    drain_locked();
}

void write(const char* fmt, ...) {
    const u64 t0 = armGetSystemTick();

    char line[384];  // a diag line is ~300 chars
    va_list args;
    va_start(args, fmt);
    int n = vsniprintf(line, sizeof(line) - 1, fmt, args);
    va_end(args);
    if (n < 0) return;
    if ((size_t)n > sizeof(line) - 2) n = sizeof(line) - 2;
    line[n++] = '\n';

    mutexLock(&g_ring_mutex);
    g_ring.push(line, (unsigned)n);  // whole line, or dropped and counted
    const bool sync = !g_thread_running;
    const unsigned us = elapsed_us(t0);
    if (us > g_max_write_us) g_max_write_us = us;
    mutexUnlock(&g_ring_mutex);

    // Only without a logger thread (it failed to start, or exit() ran).
    if (sync) drain_locked();
}

void flush() {
    mutexLock(&g_ring_mutex);
    const bool running = g_thread_running;
    mutexUnlock(&g_ring_mutex);
    if (running) ueventSignal(&g_wake);
}

unsigned take_max_write_us() {
    mutexLock(&g_ring_mutex);
    const unsigned us = g_max_write_us;
    g_max_write_us = 0;
    mutexUnlock(&g_ring_mutex);
    return us;
}

unsigned take_max_drain_us() {
    mutexLock(&g_ring_mutex);
    const unsigned us = g_max_drain_us;
    g_max_drain_us = 0;
    mutexUnlock(&g_ring_mutex);
    return us;
}

} // namespace kinestop::log
