// Kinestop sysmodule - tiny bounded log file.
//
// Part of the sysmodule/overlay split of the original Kinestop by SertAy
// (https://github.com/sertay1/Kinestop).
//
// sdmc:/config/kinestop/sysmodule.log is truncated at startup and gets
// lifecycle events, errors (each distinct error once) and a periodic
// diagnostics line (1 Hz for the first minute, then every 10 s; 1 Hz with
// KINESTOP_DEBUG_VERBOSE_LOG). Writes stop after MAX_BYTES so a misbehaving
// loop can never fill the SD card. Thread-safe.

#pragma once

namespace kinestop::log {

constexpr const char* PATH = "sdmc:/config/kinestop/sysmodule.log";
// About 1,100 diagnostics lines: ~18 min at 1 Hz, about 3 hours at the
// default 10 s cadence.
constexpr unsigned MAX_BYTES = 256 * 1024;

// Creates sdmc:/config/kinestop if needed and truncates the log.
void init();

// printf-style, integers/strings only (backed by vsniprintf to keep the
// floating-point printf machinery out of the binary). A newline is appended.
void write(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

// Longest single write() (open + append + close on the SD card) since the
// previous call, in microseconds; resets it. Writes are synchronous on the
// calling thread, so this is how long a log line stalled it.
unsigned take_max_write_us();

} // namespace kinestop::log
