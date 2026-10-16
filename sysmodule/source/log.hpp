// Kinestop sysmodule - tiny bounded log file, written asynchronously.
//
// Part of the sysmodule/overlay split of the original Kinestop by SertAy
// (https://github.com/sertay1/Kinestop).
//
// sdmc:/config/kinestop/sysmodule.log is truncated at startup and gets
// lifecycle events, errors (each distinct error once) and a periodic
// diagnostics line (1 Hz for the first minute, then every 10 s; 1 Hz with
// KINESTOP_DEBUG_VERBOSE_LOG). Writes stop after MAX_BYTES so a misbehaving
// loop can never fill the SD card. Thread-safe.
//
// write() never touches the SD card: it formats the line on the caller's
// stack and copies it into a fixed RING_BYTES ring buffer under a short
// mutex. A dedicated low-priority logger thread drains the ring to the file
// every DRAIN_PERIOD_NS (or sooner on flush()). A line that does not fit in
// the ring is dropped and counted; the drain writes "(N lines dropped)" in
// its place. Lines still in the ring when the process is killed (rather than
// exiting through exit()) are lost - at most DRAIN_PERIOD_NS worth.

#pragma once

namespace kinestop::log {

constexpr const char* PATH = "sdmc:/config/kinestop/sysmodule.log";
// About 850 diagnostics lines (~300 B each): ~14 min at 1 Hz, over 2 hours at the
// default 10 s cadence.
constexpr unsigned MAX_BYTES = 256 * 1024;

// Static ring (.bss, no heap): ~40 diagnostics lines, i.e. many drain
// periods' worth at the rates above.
constexpr unsigned RING_BYTES = 12 * 1024;
constexpr unsigned long long DRAIN_PERIOD_NS = 500000000ULL;  // 500 ms

// Logger thread: below the main loop (59) and the IPC thread (58), so it
// only runs while both wait. Stack from the heap (see the budget in main.cpp):
// newlib -> fsdev_write() can fall back to fsdev_write_safe(), which puts a
// 4 KiB bounce buffer on the stack.
constexpr int THREAD_PRIORITY = 62;
constexpr int THREAD_CORE = 3;
constexpr unsigned THREAD_STACK_SIZE = 0x3000;

// Creates sdmc:/config/kinestop if needed, truncates the log (synchronously,
// on the caller's thread: this is startup) and starts the logger thread. If
// the thread cannot be started, write() falls back to draining on the
// caller's thread so the log still works.
void init();

// Stops and joins the logger thread, then writes whatever is still queued
// on the caller's thread. Call last, before the SD card is unmounted.
void exit();

// printf-style, integers/strings only (backed by vsniprintf to keep the
// floating-point printf machinery out of the binary). A newline is appended.
void write(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

// Asks the logger thread to drain now instead of at the next period.
void flush();

// Longest single write() since the previous call, in microseconds (format +
// ring copy; no SD I/O); resets it.
unsigned take_max_write_us();

// Longest drain (open + append + close on the SD card, on the logger thread)
// since the previous call, in microseconds; resets it.
unsigned take_max_drain_us();

} // namespace kinestop::log
