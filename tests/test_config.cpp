// Kinestop host tests - common/config.{hpp,cpp}.
//
// Part of the sysmodule/overlay split of the original Kinestop by SertAy
// (https://github.com/sertay1/Kinestop).

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "check.hpp"
#include "config.hpp"

using kinestop::Config;

namespace {

std::string g_tmp;  // per-run temp directory

void write_file(const std::string& path, const char* text) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { std::perror(path.c_str()); std::exit(2); }
    std::fputs(text, f);
    std::fclose(f);
}

bool file_exists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

// Field-by-field equality; floats within `tol` (save() prints %g).
bool same(const Config& a, const Config& b, double tol) {
    auto f = [tol](float x, float y) { return check::near(x, y, tol); };
    return a.general.enabled == b.general.enabled && a.general.poll_rate_hz == b.general.poll_rate_hz &&
           a.general.language == b.general.language && a.general.debug_flags == b.general.debug_flags &&
           a.visual.style == b.visual.style && a.visual.color == b.visual.color &&
           a.visual.density == b.visual.density && f(a.visual.opacity, b.visual.opacity) &&
           a.visual.line_thickness == b.visual.line_thickness && a.visual.free_floating == b.visual.free_floating &&
           a.visual.show_in_captures == b.visual.show_in_captures &&
           f(a.filter.alpha, b.filter.alpha) && f(a.filter.deadzone_deg, b.filter.deadzone_deg) &&
           f(a.calibration.pitch_offset, b.calibration.pitch_offset) &&
           f(a.calibration.roll_offset, b.calibration.roll_offset) &&
           a.calibration.invert_pitch == b.calibration.invert_pitch &&
           a.calibration.invert_roll == b.calibration.invert_roll &&
           f(a.calibration.sensitivity_pitch, b.calibration.sensitivity_pitch) &&
           f(a.calibration.sensitivity_roll, b.calibration.sensitivity_roll);
}

Config non_default() {
    Config c;
    c.general.enabled = false;
    c.general.poll_rate_hz = 90;
    c.general.language = 1;
    c.general.debug_flags = KINESTOP_DEBUG_BLINK_TEST | KINESTOP_DEBUG_VERBOSE_LOG;  // 5
    c.visual.style = 4;
    c.visual.color = 7;
    c.visual.density = 3;
    c.visual.opacity = 0.35f;
    c.visual.line_thickness = 8;
    c.visual.free_floating = false;
    c.visual.show_in_captures = true;
    c.filter.alpha = 0.92f;
    c.filter.deadzone_deg = 1.5f;
    c.calibration.pitch_offset = -12.375f;
    c.calibration.roll_offset = 3.1415927f;
    c.calibration.invert_pitch = true;
    c.calibration.invert_roll = true;
    c.calibration.sensitivity_pitch = 2.0f;
    c.calibration.sensitivity_roll = 4.0f;
    return c;
}

// The original project's defaults (git show main:common/config.hpp).
void check_original_defaults(const Config& c) {
    CHECK_EQ(c.general.enabled, true);
    CHECK_EQ(c.general.poll_rate_hz, 60);
    CHECK_EQ(c.general.language, 0);
    CHECK_EQ(c.general.debug_flags, 0);  // test-build field, off by default
    CHECK_EQ(c.visual.style, 0);
    CHECK_EQ(c.visual.color, 0);
    CHECK_EQ(c.visual.density, 1);
    CHECK_EQ(c.visual.opacity, 0.80f);
    CHECK_EQ(c.visual.line_thickness, 3);
    CHECK_EQ(c.visual.free_floating, true);
    CHECK_EQ(c.visual.show_in_captures, false);  // new field, off by default
    CHECK_EQ(c.filter.alpha, 0.85f);
    CHECK_EQ(c.filter.deadzone_deg, 0.5f);
    CHECK_EQ(c.calibration.pitch_offset, 0.0f);
    CHECK_EQ(c.calibration.roll_offset, 0.0f);
    CHECK_EQ(c.calibration.invert_pitch, false);
    CHECK_EQ(c.calibration.invert_roll, false);
    CHECK_EQ(c.calibration.sensitivity_pitch, 1.0f);
    CHECK_EQ(c.calibration.sensitivity_roll, 1.0f);
}

void test_defaults() {
    Config a;
    check_original_defaults(a);

    Config b = non_default();
    b.set_defaults();
    check_original_defaults(b);

    // Defaults are already in range.
    Config c;
    c.sanitize();
    CHECK(same(a, c, 0.0));

    CHECK(!std::strcmp(Config::DEFAULT_PATH, "sdmc:/config/kinestop/config.ini"));
    CHECK(!std::strncmp(Config::DEFAULT_PATH, Config::DEFAULT_DIR, std::strlen(Config::DEFAULT_DIR)));
}

void test_roundtrip() {
    // Parent directories that do not exist yet are created by save().
    const std::string path = g_tmp + "/nested/dir/config.ini";
    const Config a = non_default();
    CHECK(a.save(path.c_str()));
    CHECK(file_exists(path));

    Config b;
    CHECK(b.load(path.c_str()));
    CHECK(same(a, b, 1e-4));
    CHECK_EQ(b.visual.show_in_captures, true);
    CHECK_EQ(b.general.debug_flags, 5);

    // Loading again from the saved-and-loaded copy is a fixed point.
    const std::string path2 = g_tmp + "/config2.ini";
    CHECK(b.save(path2.c_str()));
    Config c;
    CHECK(c.load(path2.c_str()));
    CHECK(same(b, c, 0.0));

    // The "sdmc:" device prefix falls back to the plain path on a host.
    const std::string sdmc_path = "sdmc:" + g_tmp + "/sdmc/config.ini";
    CHECK(a.save(sdmc_path.c_str()));
    CHECK(file_exists(g_tmp + "/sdmc/config.ini"));
    Config d;
    CHECK(d.load(sdmc_path.c_str()));
    CHECK(same(a, d, 1e-4));
    CHECK(!file_exists("sdmc:"));  // no stray directory in the cwd

    // Missing file: returns false and leaves defaults.
    Config e = non_default();
    CHECK(!e.load((g_tmp + "/does/not/exist.ini").c_str()));
    check_original_defaults(e);
}

void test_legacy_file() {
    // Verbatim default config.ini written by the original packager
    // (git show main:scripts/package_sd.py). Contains the removed
    // draw_center_reticle/draw_roll_indicators keys and lacks the newer ones.
    static const char* LEGACY =
        "; Kinestop Configuration File\n"
        "; Motion sickness countermeasure overlay for Nintendo Switch CFW\n"
        "\n"
        "[general]\n"
        "enabled = true\n"
        "poll_rate_hz = 60\n"
        "\n"
        "[visual]\n"
        "style = 0\n"
        "opacity = 0.70\n"
        "line_thickness = 3\n"
        "draw_center_reticle = true\n"
        "draw_roll_indicators = true\n"
        "\n"
        "[filter]\n"
        "alpha = 0.96\n"
        "deadzone_deg = 0.5\n"
        "\n"
        "[calibration]\n"
        "pitch_offset = 0.0\n"
        "roll_offset = 0.0\n"
        "invert_pitch = false\n"
        "invert_roll = false\n"
        "sensitivity_pitch = 1.0\n"
        "sensitivity_roll = 1.0\n";

    const std::string path = g_tmp + "/legacy.ini";
    write_file(path, LEGACY);

    Config c = non_default();
    CHECK(c.load(path.c_str()));
    CHECK_EQ(c.general.enabled, true);
    CHECK_EQ(c.general.poll_rate_hz, 60);
    CHECK_EQ(c.general.language, 0);          // absent -> default
    CHECK_EQ(c.general.debug_flags, 0);       // absent -> default
    CHECK_EQ(c.visual.style, 0);
    CHECK_EQ(c.visual.color, 0);              // absent -> default
    CHECK_EQ(c.visual.density, 1);            // absent -> default
    CHECK_NEAR(c.visual.opacity, 0.70f, 1e-6);
    CHECK_EQ(c.visual.line_thickness, 3);
    CHECK_EQ(c.visual.free_floating, true);   // absent -> default
    CHECK_EQ(c.visual.show_in_captures, false);
    CHECK_NEAR(c.filter.alpha, 0.96f, 1e-6);  // not a UI preset, but kept
    CHECK_NEAR(c.filter.deadzone_deg, 0.5f, 1e-6);
    CHECK_EQ(c.calibration.pitch_offset, 0.0f);
    CHECK_EQ(c.calibration.roll_offset, 0.0f);
    CHECK_EQ(c.calibration.invert_pitch, false);
    CHECK_EQ(c.calibration.invert_roll, false);
    CHECK_EQ(c.calibration.sensitivity_pitch, 1.0f);
    CHECK_EQ(c.calibration.sensitivity_roll, 1.0f);

    // Re-saving drops the legacy keys and adds show_in_captures.
    CHECK(c.save(path.c_str()));
    FILE* f = std::fopen(path.c_str(), "r");
    CHECK(f != nullptr);
    if (f) {
        char buf[4096];
        size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
        buf[n] = '\0';
        std::fclose(f);
        CHECK(std::strstr(buf, "draw_center_reticle") == nullptr);
        CHECK(std::strstr(buf, "draw_roll_indicators") == nullptr);
        CHECK(std::strstr(buf, "show_in_captures = false") != nullptr);
        CHECK(std::strstr(buf, "debug_flags = 0") != nullptr);
        CHECK(std::strstr(buf, "SertAy") != nullptr);
    }
}

void test_parser_robustness() {
    // CRLF, odd spacing and case, comments, unknown sections/keys, junk values
    // (keep the default), an over-long line that must be skipped whole.
    std::string text =
        "# comment\r\n"
        "  [ VISUAL ]  \r\n"
        "  STYLE=  2 \r\n"
        "color = notanumber\r\n"
        "free_floating = OFF\r\n"
        "show_in_captures = yes\r\n"
        "opacity = \r\n"
        "no_equals_sign_here\r\n"
        "[unknown]\r\n"
        "style = 5\r\n"
        "[filter]\r\n"
        "deadzone_deg = 2.5 ; trailing comment is not stripped -> parsed prefix\r\n"
        "[calibration]\r\n"
        "roll_offset = ";
    text += std::string(400, '9');  // longer than the parser's line buffer
    text += "\r\nsensitivity_roll = 3\r\n";

    const std::string path = g_tmp + "/robust.ini";
    write_file(path, text.c_str());

    Config c;
    CHECK(c.load(path.c_str()));
    CHECK_EQ(c.visual.style, 2);
    CHECK_EQ(c.visual.color, 0);
    CHECK_EQ(c.visual.free_floating, false);
    CHECK_EQ(c.visual.show_in_captures, true);
    CHECK_EQ(c.visual.opacity, 0.80f);
    CHECK_NEAR(c.filter.deadzone_deg, 2.5f, 1e-6);
    CHECK_EQ(c.calibration.roll_offset, 0.0f);         // over-long line ignored
    CHECK_EQ(c.calibration.sensitivity_roll, 3.0f);    // ...and the next line still parsed
}

// [general] debug_flags: parsed as an int, junk keeps the default, each bit
// is independent.
void test_debug_flags_parse() {
    const std::string path = g_tmp + "/debug.ini";

    write_file(path, "[general]\ndebug_flags = 6\n");
    Config a;
    CHECK(a.load(path.c_str()));
    CHECK_EQ(a.general.debug_flags, (int)(KINESTOP_DEBUG_UNSCALED_LAYER | KINESTOP_DEBUG_VERBOSE_LOG));

    write_file(path, "[general]\r\n  DEBUG_FLAGS=1 \r\n");
    Config b;
    CHECK(b.load(path.c_str()));
    CHECK_EQ(b.general.debug_flags, (int)KINESTOP_DEBUG_BLINK_TEST);

    write_file(path, "[general]\ndebug_flags = lots\n");
    Config c;
    CHECK(c.load(path.c_str()));
    CHECK_EQ(c.general.debug_flags, 0);

    write_file(path, "[general]\ndebug_flags = -1\n");
    Config d;
    CHECK(d.load(path.c_str()));
    CHECK_EQ(d.general.debug_flags, 0);

    // Only under [general].
    write_file(path, "[visual]\ndebug_flags = 7\n");
    Config e;
    CHECK(e.load(path.c_str()));
    CHECK_EQ(e.general.debug_flags, 0);

    // save() writes it under [general]; it survives a round trip.
    Config f;
    f.general.debug_flags = 3;
    CHECK(f.save(path.c_str()));
    Config g;
    CHECK(g.load(path.c_str()));
    CHECK_EQ(g.general.debug_flags, 3);

    // Every defined bit survives sanitize(); anything above KINESTOP_DEBUG_ALL
    // is dropped.
    write_file(path, "[general]\ndebug_flags = 63\n");
    Config h;
    CHECK(h.load(path.c_str()));
    CHECK_EQ(h.general.debug_flags, (int)KINESTOP_DEBUG_ALL);
    CHECK_EQ(h.general.debug_flags, (int)(KINESTOP_DEBUG_BLINK_TEST | KINESTOP_DEBUG_UNSCALED_LAYER |
                                          KINESTOP_DEBUG_VERBOSE_LOG | KINESTOP_DEBUG_NO_SUSPEND |
                                          KINESTOP_DEBUG_SCALED_SMALL | KINESTOP_DEBUG_LOW_Z));

    write_file(path, "[general]\ndebug_flags = 255\n");
    Config i;
    CHECK(i.load(path.c_str()));
    CHECK_EQ(i.general.debug_flags, (int)KINESTOP_DEBUG_ALL);

    write_file(path, "[general]\ndebug_flags = 80\n");  // 0x40 (unknown) | SCALED_SMALL
    Config j;
    CHECK(j.load(path.c_str()));
    CHECK_EQ(j.general.debug_flags, (int)KINESTOP_DEBUG_SCALED_SMALL);
}

void test_wire() {
    const Config a = non_default();
    KinestopWireConfig w;
    std::memset(&w, 0xAB, sizeof(w));
    a.to_wire(w);

    CHECK_EQ(w.style, 4);
    CHECK_EQ(w.color, 7);
    CHECK_EQ(w.density, 3);
    CHECK_EQ(w.dot_size, 8);
    CHECK_EQ(w.free_floating, 0);
    CHECK_EQ(w.invert_pitch, 1);
    CHECK_EQ(w.invert_roll, 1);
    CHECK_EQ(w.show_in_captures, 1);
    CHECK_EQ(w.poll_rate_hz, 90);
    CHECK_EQ(w.debug_flags, 5);
    CHECK_EQ(w.reserved, 0);
    CHECK_EQ(w.opacity, 0.35f);
    CHECK_EQ(w.filter_alpha, 0.92f);
    CHECK_EQ(w.deadzone_deg, 1.5f);
    CHECK_EQ(w.pitch_offset, -12.375f);
    CHECK_EQ(w.roll_offset, 3.1415927f);
    CHECK_EQ(w.sensitivity_pitch, 2.0f);
    CHECK_EQ(w.sensitivity_roll, 4.0f);

    // from_wire restores everything carried, exactly, and leaves the
    // general fields that are not on the wire alone.
    Config b;
    b.general.enabled = true;
    b.general.language = 0;
    b.from_wire(w);
    Config expect = a;
    expect.general.enabled = true;
    expect.general.language = 0;
    CHECK(same(b, expect, 0.0));

    // to_wire works on a sanitized copy and does not modify the source.
    Config bad;
    bad.visual.style = 99;
    bad.visual.line_thickness = -4;
    bad.general.poll_rate_hz = 100000;
    bad.visual.opacity = 7.0f;
    KinestopWireConfig w2;
    bad.to_wire(w2);
    CHECK_EQ(w2.style, 0);  // unknown style -> Perimeter, like the original
    CHECK_EQ(w2.dot_size, 0);
    CHECK_EQ(w2.poll_rate_hz, 120);
    CHECK_EQ(w2.opacity, 1.0f);
    CHECK_EQ(w2.debug_flags, 0);
    CHECK_EQ(w2.reserved, 0);
    CHECK_EQ(bad.visual.style, 99);

    // debug_flags on the wire: unknown bits are dropped, both ways.
    Config dbg;
    dbg.general.debug_flags = 0xFF;
    KinestopWireConfig w3;
    dbg.to_wire(w3);
    CHECK_EQ(w3.debug_flags, (int)KINESTOP_DEBUG_ALL);
    CHECK_EQ(dbg.general.debug_flags, 0xFF);  // source untouched
    w3.debug_flags = 0xC2;  // 0b11000010: only UNSCALED_LAYER is known
    Config dbg2;
    dbg2.from_wire(w3);
    CHECK_EQ(dbg2.general.debug_flags, (int)KINESTOP_DEBUG_UNSCALED_LAYER);
    for (int bits = 0; bits <= (int)KINESTOP_DEBUG_ALL; ++bits) {
        Config x;
        x.general.debug_flags = bits;
        KinestopWireConfig wx;
        x.to_wire(wx);
        Config y;
        y.from_wire(wx);
        CHECK_ALL(wx.debug_flags == bits && y.general.debug_flags == bits, "debug_flags %d", bits);
    }

    // from_wire sanitizes hostile input.
    KinestopWireConfig h;
    std::memset(&h, 0, sizeof(h));
    h.style = 200;
    h.color = 255;
    h.density = 9;
    h.dot_size = 50;
    h.free_floating = 7;  // any non-zero is true
    h.poll_rate_hz = 1;
    h.opacity = std::numeric_limits<float>::quiet_NaN();
    h.filter_alpha = 2.0f;
    h.deadzone_deg = -1.0f;
    h.pitch_offset = std::numeric_limits<float>::infinity();
    h.roll_offset = -std::numeric_limits<float>::infinity();
    h.sensitivity_pitch = 0.0f;
    h.sensitivity_roll = 1000.0f;
    Config c;
    c.from_wire(h);
    CHECK_EQ(c.visual.style, 0);
    CHECK_EQ(c.visual.color, 11);
    CHECK_EQ(c.visual.density, 3);
    CHECK_EQ(c.visual.line_thickness, 9);
    CHECK_EQ(c.visual.free_floating, true);
    CHECK_EQ(c.general.poll_rate_hz, 10);
    CHECK_EQ(c.visual.opacity, 0.80f);
    CHECK(c.filter.alpha < 1.0f);
    CHECK_EQ(c.filter.deadzone_deg, 0.0f);
    CHECK_EQ(c.calibration.pitch_offset, 0.0f);
    CHECK_EQ(c.calibration.roll_offset, 0.0f);
    CHECK_EQ(c.calibration.sensitivity_pitch, 0.2f);
    CHECK_EQ(c.calibration.sensitivity_roll, 6.0f);
    CHECK_EQ(c.general.debug_flags, 0);
}

void test_sanitize() {
    const float nan = std::numeric_limits<float>::quiet_NaN();

    Config lo;
    lo.general.poll_rate_hz = -5;
    lo.general.language = -1;
    lo.general.debug_flags = -1;  // not "every bit": negative means none
    lo.visual.style = -1;
    lo.visual.color = -1;
    lo.visual.density = -1;
    lo.visual.opacity = 0.0f;
    lo.visual.line_thickness = -1;
    lo.filter.alpha = -0.5f;
    lo.filter.deadzone_deg = -3.0f;
    lo.calibration.sensitivity_pitch = 0.1f;
    lo.calibration.sensitivity_roll = -2.0f;
    lo.sanitize();
    CHECK_EQ(lo.general.poll_rate_hz, 10);
    CHECK_EQ(lo.general.language, 0);
    CHECK_EQ(lo.general.debug_flags, 0);
    CHECK_EQ(lo.visual.style, 0);
    CHECK_EQ(lo.visual.color, 0);
    CHECK_EQ(lo.visual.density, 0);
    CHECK_EQ(lo.visual.opacity, 0.10f);
    CHECK_EQ(lo.visual.line_thickness, 0);
    CHECK_EQ(lo.filter.alpha, 0.0f);
    CHECK_EQ(lo.filter.deadzone_deg, 0.0f);
    CHECK_EQ(lo.calibration.sensitivity_pitch, 0.2f);
    CHECK_EQ(lo.calibration.sensitivity_roll, 0.2f);

    Config hi;
    hi.general.poll_rate_hz = 1000;
    hi.general.language = 7;
    hi.general.debug_flags = 0x7FFFFFFF;  // unknown bits dropped, known kept
    hi.visual.style = 6;
    hi.visual.color = 12;
    hi.visual.density = 4;
    hi.visual.opacity = 1.5f;
    hi.visual.line_thickness = 10;
    hi.filter.alpha = 1.0f;
    hi.filter.deadzone_deg = 9.0f;
    hi.calibration.sensitivity_pitch = 6.5f;
    hi.calibration.sensitivity_roll = 60.0f;
    hi.sanitize();
    CHECK_EQ(hi.general.poll_rate_hz, 120);
    CHECK_EQ(hi.general.language, 1);
    CHECK_EQ(hi.general.debug_flags, (int)KINESTOP_DEBUG_ALL);
    CHECK_EQ(hi.visual.style, 0);  // original get_anchors(): default -> Perimeter
    CHECK_EQ(hi.visual.color, 11);
    CHECK_EQ(hi.visual.density, 3);
    CHECK_EQ(hi.visual.opacity, 1.0f);
    CHECK_EQ(hi.visual.line_thickness, 9);
    CHECK(hi.filter.alpha < 1.0f);  // the filter needs alpha < 1 to converge
    CHECK_EQ(hi.filter.deadzone_deg, 5.0f);
    CHECK_EQ(hi.calibration.sensitivity_pitch, 6.0f);
    CHECK_EQ(hi.calibration.sensitivity_roll, 6.0f);

    // NaN -> the field's default; non-finite calibration offsets -> 0.
    Config n;
    n.visual.opacity = nan;
    n.filter.alpha = nan;
    n.filter.deadzone_deg = nan;
    n.calibration.sensitivity_pitch = nan;
    n.calibration.sensitivity_roll = nan;
    n.calibration.pitch_offset = nan;
    n.calibration.roll_offset = std::numeric_limits<float>::infinity();
    n.sanitize();
    CHECK_EQ(n.visual.opacity, 0.80f);
    CHECK_EQ(n.filter.alpha, 0.85f);
    CHECK_EQ(n.filter.deadzone_deg, 0.5f);
    CHECK_EQ(n.calibration.sensitivity_pitch, 1.0f);
    CHECK_EQ(n.calibration.sensitivity_roll, 1.0f);
    CHECK_EQ(n.calibration.pitch_offset, 0.0f);
    CHECK_EQ(n.calibration.roll_offset, 0.0f);

    // In-range values are untouched, including every UI preset.
    Config ok = non_default();
    Config ok2 = ok;
    ok2.sanitize();
    CHECK(same(ok, ok2, 0.0));

    // load() sanitizes what it reads.
    const std::string path = g_tmp + "/out_of_range.ini";
    write_file(path,
               "[general]\npoll_rate_hz = 5000\nlanguage = 3\ndebug_flags = 108\n"
               "[visual]\nstyle = 42\ncolor = -3\nopacity = 9\nline_thickness = 99999999999\n"
               "[filter]\nalpha = nan\ndeadzone_deg = 50\n"
               "[calibration]\nsensitivity_roll = 0\n");
    Config l;
    CHECK(l.load(path.c_str()));
    CHECK_EQ(l.general.poll_rate_hz, 120);
    CHECK_EQ(l.general.language, 1);
    CHECK_EQ(l.general.debug_flags, 44);  // 108 = 0b1101100: bit 6 dropped
    CHECK_EQ(l.visual.style, 0);
    CHECK_EQ(l.visual.color, 0);
    CHECK_EQ(l.visual.opacity, 1.0f);
    CHECK_EQ(l.visual.line_thickness, 9);
    CHECK_EQ(l.filter.alpha, 0.85f);
    CHECK_EQ(l.filter.deadzone_deg, 5.0f);
    CHECK_EQ(l.calibration.sensitivity_roll, 0.2f);
}

} // namespace

int main() {
    char tmpl[] = "/tmp/kinestop_test_XXXXXX";
    const char* tmpdir_env = std::getenv("TMPDIR");
    std::string pattern = std::string(tmpdir_env && *tmpdir_env ? tmpdir_env : "/tmp") + "/kinestop_test_XXXXXX";
    std::string buf = pattern;
    char* dir = mkdtemp(buf.data());
    if (!dir) dir = mkdtemp(tmpl);
    if (!dir) { std::perror("mkdtemp"); return 2; }
    g_tmp = dir;

    test_defaults();
    test_roundtrip();
    test_legacy_file();
    test_parser_robustness();
    test_debug_flags_parse();
    test_wire();
    test_sanitize();

    const std::string cmd = "rm -rf '" + g_tmp + "'";
    if (std::system(cmd.c_str()) != 0)
        std::fprintf(stderr, "  note: could not remove %s\n", g_tmp.c_str());
    return check::summary("config");
}
