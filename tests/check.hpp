// Kinestop host tests - minimal assertion helpers.
//
// Part of the sysmodule/overlay split of the original Kinestop by SertAy
// (https://github.com/sertay1/Kinestop).

#pragma once

#include <cmath>
#include <cstdio>

namespace check {

inline int failures = 0;
inline int checks = 0;

inline void fail_at(const char* file, int line, const char* expr) {
    ++failures;
    if (failures <= 50)
        std::fprintf(stderr, "  FAIL %s:%d: %s\n", file, line, expr);
}

inline bool near(double a, double b, double tol) {
    if (std::isnan(a) || std::isnan(b)) return false;
    return std::fabs(a - b) <= tol;
}

// Prints a summary and returns the process exit code.
inline int summary(const char* suite) {
    if (failures)
        std::printf("[FAIL] %s: %d of %d checks failed\n", suite, failures, checks);
    else
        std::printf("[ OK ] %s: %d checks\n", suite, checks);
    return failures ? 1 : 0;
}

} // namespace check

#define CHECK(expr) \
    do { ++check::checks; if (!(expr)) check::fail_at(__FILE__, __LINE__, #expr); } while (0)

#define CHECK_EQ(a, b) \
    do { ++check::checks; if (!((a) == (b))) check::fail_at(__FILE__, __LINE__, #a " == " #b); } while (0)

#define CHECK_NEAR(a, b, tol) \
    do { ++check::checks; if (!check::near((double)(a), (double)(b), (double)(tol))) \
        check::fail_at(__FILE__, __LINE__, #a " ~= " #b); } while (0)

// For loops over many cases: counts a single check, reports the first
// mismatch with a caller-formatted context string.
#define CHECK_ALL(ok, ...) \
    do { ++check::checks; if (!(ok)) { std::fprintf(stderr, "  context: " __VA_ARGS__); \
        std::fprintf(stderr, "\n"); check::fail_at(__FILE__, __LINE__, #ok); } } while (0)
