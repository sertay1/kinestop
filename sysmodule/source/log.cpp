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
    char line[160];
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
        const char* text = line;
        if (g_bytes + (unsigned)n > MAX_BYTES) {
            text = "(log size limit reached)\n";
            g_full = true;
        }
        // Open/append/close per line: lines are rare, and nothing is lost if
        // the process is terminated by pm.
        FILE* f = std::fopen(PATH, "a");
        if (f) {
            std::fputs(text, f);
            std::fclose(f);
        }
        g_bytes += (unsigned)n;
    }
    mutexUnlock(&g_mutex);
}

} // namespace kinestop::log
