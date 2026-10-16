// Kinestop sysmodule - tiny bounded log file.
//
// Part of the sysmodule/overlay split of the original Kinestop by SertAy
// (https://github.com/sertay1/Kinestop).

#include "log.hpp"

#include <switch.h>
#include <cstdarg>
#include <cstdio>
#include <sys/stat.h>

namespace kinestop::log {

namespace {
    Mutex g_mutex;
    unsigned g_bytes = 0;
    bool g_full = false;
    unsigned g_max_write_us = 0;  // longest file write since take_max_write_us()
}

void init() {
    mutexLock(&g_mutex);
    mkdir("sdmc:/config", 0777);
    mkdir("sdmc:/config/kinestop", 0777);
    FILE* f = std::fopen(PATH, "w");
    if (f) std::fclose(f);
    g_bytes = 0;
    g_full = false;
    mutexUnlock(&g_mutex);
}

void write(const char* fmt, ...) {
    char line[320];
    va_list args;
    va_start(args, fmt);
    int n = vsniprintf(line, sizeof(line) - 1, fmt, args);
    va_end(args);
    if (n < 0) return;
    if ((size_t)n > sizeof(line) - 2) n = sizeof(line) - 2;
    line[n++] = '\n';
    line[n] = '\0';

    mutexLock(&g_mutex);
    if (!g_full) {
        static const char LIMIT_NOTE[] = "(log size limit reached)\n";
        const char* text = line;
        // The final note is budgeted too, so the file never exceeds MAX_BYTES.
        if (g_bytes + (unsigned)n + (sizeof(LIMIT_NOTE) - 1) > MAX_BYTES) {
            text = LIMIT_NOTE;
            n = (int)(sizeof(LIMIT_NOTE) - 1);
            g_full = true;
        }
        // Open/append/close per line: at most a line or two per second, and
        // nothing is lost if the process is terminated by pm.
        const u64 start = armGetSystemTick();
        FILE* f = std::fopen(PATH, "a");
        if (f) {
            std::fputs(text, f);
            std::fclose(f);
        }
        g_bytes += (unsigned)n;
        const u64 us = armTicksToNs(armGetSystemTick() - start) / 1000ULL;
        const unsigned us32 = us > 0xFFFFFFFFull ? 0xFFFFFFFFu : (unsigned)us;
        if (us32 > g_max_write_us) g_max_write_us = us32;
    }
    mutexUnlock(&g_mutex);
}

unsigned take_max_write_us() {
    mutexLock(&g_mutex);
    const unsigned us = g_max_write_us;
    g_max_write_us = 0;
    mutexUnlock(&g_mutex);
    return us;
}

} // namespace kinestop::log
