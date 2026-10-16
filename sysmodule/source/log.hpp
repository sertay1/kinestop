// Kinestop sysmodule - tiny bounded log file.
//
// Part of the sysmodule/overlay split of the original Kinestop by SertAy
// (https://github.com/sertay1/Kinestop).
//
// sdmc:/config/kinestop/sysmodule.log is truncated at startup and only gets
// lifecycle events and errors (each distinct error once). Writes stop after
// MAX_BYTES so a misbehaving loop can never fill the SD card. Thread-safe.

#pragma once

namespace kinestop::log {

constexpr const char* PATH = "sdmc:/config/kinestop/sysmodule.log";
constexpr unsigned MAX_BYTES = 16 * 1024;

// Creates sdmc:/config/kinestop if needed and truncates the log.
void init();

// printf-style, integers/strings only (backed by vsniprintf to keep the
// floating-point printf machinery out of the binary). A newline is appended.
void write(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

} // namespace kinestop::log
