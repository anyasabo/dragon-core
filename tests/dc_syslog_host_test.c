// Host test for the pure dc_syslog parts (dc_syslog_format.h): level mapping,
// ANSI color strip, ESP log prefix parse, RFC 5424 format, sequence numbers and
// truncation of long lines.
#include "dc_syslog_format.h"

#include <stdio.h>
#include <string.h>

static int fails = 0;

static void check(const char *name, int good)
{
    if (!good) fails++;
    printf("[%s] %s\n", good ? "PASS" : "FAIL", name);
}

static void check_str(const char *name, const char *got, const char *want)
{
    int good = strcmp(got, want) == 0;
    if (!good) fails++;
    printf("[%s] %s\n", good ? "PASS" : "FAIL", name);
    if (!good) printf("    want: %s\n    got:  %s\n", want, got);
}

// Strip, parse and format one raw line, as the sender task does.
static size_t render(const char *raw, const dc_syslog_meta_t *meta, char *out, size_t out_sz)
{
    char line[512];
    size_t len = strlen(raw);
    memcpy(line, raw, len);
    len = dc_syslog_strip(line, len);
    dc_syslog_line_t parsed;
    dc_syslog_parse_line(line, len, &parsed);
    return dc_syslog_format(out, out_sz, meta, &parsed);
}

static void test_severity(void)
{
    check("severity E=3", dc_syslog_severity('E') == 3);
    check("severity W=4", dc_syslog_severity('W') == 4);
    check("severity I=6", dc_syslog_severity('I') == 6);
    check("severity D=7", dc_syslog_severity('D') == 7);
    check("severity V=7", dc_syslog_severity('V') == 7);
    check("severity x=-1", dc_syslog_severity('x') == -1);
    check("severity NUL=-1", dc_syslog_severity('\0') == -1);
}

static void strip(const char *name, const char *in, const char *want)
{
    char buf[128];
    size_t len = strlen(in);
    memcpy(buf, in, len);
    len = dc_syslog_strip(buf, len);
    buf[len] = '\0';
    check_str(name, buf, want);
    // A second strip must not change the text.
    size_t again = dc_syslog_strip(buf, len);
    check("  strip again keeps the text", again == len);
}

static void test_strip(void)
{
    strip("strip color line", "\x1b[0;32mI (123) tag: hello\x1b[0m\n", "I (123) tag: hello");
    strip("strip bold color", "\x1b[1;31mE (5) t: x\x1b[0m\r\n", "E (5) t: x");
    strip("strip cut sequence at end", "I (1) t: abc\x1b[0", "I (1) t: abc");
    strip("strip ESC [ at end", "I (1) t: abc\x1b[", "I (1) t: abc");
    strip("strip lone ESC at end", "I (1) t: abc\x1b", "I (1) t: abc");
    strip("strip ESC without [", "a\x1b" "bc", "abc");
    strip("strip sequence in the middle", "a\x1b[2Kb", "ab");
    strip("strip keeps plain text", "plain text", "plain text");
    strip("strip only newlines", "\n\n", "");
    strip("strip invalid byte ends sequence", "a\x1b[1\x01z", "a\x01z");
}

static void parse(const char *name, const char *in, int sev, const char *tag, const char *msg)
{
    dc_syslog_line_t l;
    dc_syslog_parse_line(in, strlen(in), &l);
    int good = l.severity == sev &&
               (tag ? (l.tag && l.tag_len == strlen(tag) && memcmp(l.tag, tag, l.tag_len) == 0)
                    : l.tag == NULL) &&
               l.msg_len == strlen(msg) && memcmp(l.msg, msg, l.msg_len) == 0;
    check(name, good);
    if (!good) printf("    sev=%d tag=%.*s msg=%.*s\n", l.severity, (int)l.tag_len,
                      l.tag ? l.tag : "", (int)l.msg_len, l.msg);
}

static void test_parse(void)
{
    parse("parse info", "I (12345) dc_prusa: polling", 6, "dc_prusa", "polling");
    parse("parse error", "E (1) pb_heater: fault", 3, "pb_heater", "fault");
    parse("parse warning", "W (99) dc_wifi: retry 3", 4, "dc_wifi", "retry 3");
    parse("parse debug", "D (99) x: y", 7, "x", "y");
    parse("parse verbose", "V (99) x: y", 7, "x", "y");
    parse("parse wifi lib (no space)", "I (588) wifi:mode : sta", 6, "wifi", "mode : sta");
    parse("parse system time", "W (12:34:56.789) dc_time: late", 4, "dc_time", "late");
    parse("parse colon in message", "I (5) t: a: b", 6, "t", "a: b");
    parse("parse empty message", "I (5) t: ", 6, "t", "");
    parse("parse no prefix", "rst:0x1 (POWERON),boot:0xc", 6, NULL, "rst:0x1 (POWERON),boot:0xc");
    parse("parse bad level", "X (5) t: m", 6, NULL, "X (5) t: m");
    parse("parse letters in time", "I (abc) t: m", 6, NULL, "I (abc) t: m");
    parse("parse no closing paren", "I (123 t: m", 6, NULL, "I (123 t: m");
    parse("parse no tag colon", "I (123) no colon here", 6, NULL, "I (123) no colon here");
    parse("parse empty tag", "I (123) : m", 6, NULL, "I (123) : m");
    parse("parse short", "I (", 6, NULL, "I (");
    parse("parse empty", "", 6, NULL, "");
}

static void test_format(void)
{
    char out[512];
    dc_syslog_meta_t meta = {
        .timestamp = NULL, .hostname = "dragonbreath", .app_name = "dragonbreath",
        .seq = 123, .up_ms = 456789,
    };

    render("\x1b[0;32mI (456789) dc_prusa: printer PRINTING\x1b[0m\n", &meta, out, sizeof out);
    check_str("format info, no time", out,
              "<14>1 - dragonbreath dragonbreath dc_prusa - "
              "[dc@32473 seq=\"123\" up=\"456789\"] printer PRINTING");

    meta.timestamp = "2026-11-14T23:40:11.123Z";
    render("\x1b[0;31mE (1) pb_heater: over temperature\x1b[0m\n", &meta, out, sizeof out);
    check_str("format error, with time", out,
              "<11>1 2026-11-14T23:40:11.123Z dragonbreath dragonbreath pb_heater - "
              "[dc@32473 seq=\"123\" up=\"456789\"] over temperature");

    render("\x1b[0;33mW (1) dc_wifi: weak\x1b[0m\n", &meta, out, sizeof out);
    check("format warning PRI 12", strncmp(out, "<12>1 ", 6) == 0);
    render("D (1) t: d\n", &meta, out, sizeof out);
    check("format debug PRI 15", strncmp(out, "<15>1 ", 6) == 0);
    render("V (1) t: v\n", &meta, out, sizeof out);
    check("format verbose PRI 15", strncmp(out, "<15>1 ", 6) == 0);

    meta.timestamp = "";
    meta.hostname = NULL;
    meta.app_name = "";
    render("boot line without prefix\n", &meta, out, sizeof out);
    check_str("format nil fields", out,
              "<14>1 - - - - - [dc@32473 seq=\"123\" up=\"456789\"] boot line without prefix");

    meta.hostname = "my host";
    meta.app_name = "app\x7f";
    render("I (1) tag with space: m\n", &meta, out, sizeof out);
    check_str("format bad header characters", out,
              "<14>1 - my_host app_ tag_with_space - [dc@32473 seq=\"123\" up=\"456789\"] m");

    meta.hostname = "h";
    meta.app_name = "a";
    render("I (1) t: \n", &meta, out, sizeof out);
    check_str("format empty message", out, "<14>1 - h a t - [dc@32473 seq=\"123\" up=\"456789\"]");

    // Long header fields are cut to the limits of dc_syslog_format.h.
    char longname[200];
    memset(longname, 'n', sizeof longname - 1);
    longname[sizeof longname - 1] = '\0';
    meta.hostname = longname;
    meta.app_name = longname;
    size_t n = render("I (1) t: m\n", &meta, out, sizeof out);
    check("format cuts long hostname and app name",
          n == strlen("<14>1 - ") + DC_SYSLOG_HOSTNAME_MAX + 1 + DC_SYSLOG_APPNAME_MAX +
               strlen(" t - [dc@32473 seq=\"123\" up=\"456789\"] m"));
}

static void test_sequence(void)
{
    char out[256];
    dc_syslog_meta_t meta = { .hostname = "h", .app_name = "a" };
    const struct { uint32_t seq; uint64_t up; const char *want; } cases[] = {
        { 0, 0, "<14>1 - h a t - [dc@32473 seq=\"0\" up=\"0\"] m" },
        { 1, 1, "<14>1 - h a t - [dc@32473 seq=\"1\" up=\"1\"] m" },
        { 4294967295u, 18446744073709551615ull,
          "<14>1 - h a t - [dc@32473 seq=\"4294967295\" up=\"18446744073709551615\"] m" },
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        meta.seq = cases[i].seq;
        meta.up_ms = cases[i].up;
        render("I (1) t: m", &meta, out, sizeof out);
        check_str("sequence and uptime values", out, cases[i].want);
    }
}

static void test_truncation(void)
{
    dc_syslog_meta_t meta = { .hostname = "h", .app_name = "a", .seq = 7, .up_ms = 8 };
    const char *head = "<14>1 - h a t - [dc@32473 seq=\"7\" up=\"8\"";
    char raw[400];
    snprintf(raw, sizeof raw, "I (1) t: ");
    memset(raw + strlen(raw), 'x', 300);
    raw[9 + 300] = '\0';

    char out[100];
    memset(out, 'Z', sizeof out);
    size_t n = render(raw, &meta, out, sizeof out);
    char want[100];
    snprintf(want, sizeof want, "%s trunc=\"1\"] ", head);
    size_t keep = sizeof out - 1 - strlen(want);
    memset(want + strlen(want), 'x', keep);
    want[sizeof out - 1] = '\0';
    check("truncation fills the buffer", n == sizeof out - 1 && out[n] == '\0');
    check_str("truncation marks trunc=\"1\"", out, want);

    // A message that fits exactly has no trunc parameter.
    char exact[200];
    snprintf(exact, sizeof exact, "%s] m", head);
    size_t sz = strlen(exact) + 1;
    n = render("I (1) t: m", &meta, out, sz);
    check_str("exact fit is not truncated", out, exact);
    check("exact fit length", n == sz - 1);
    // A longer message in a slightly larger buffer is cut and gets trunc="1".
    n = render("I (1) t: mxxxxxxxxxxxxxxxxxxxx", &meta, out, sz + 10);
    char cut[200];
    snprintf(cut, sizeof cut, "%s trunc=\"1\"] m", head);
    check_str("cut message keeps what fits", out, cut);
    check("cut message length", n == sz + 10 - 1);
    // If trunc="1" itself does not fit, the result is 0 and "".
    n = render("I (1) t: mm", &meta, out, sz);
    check("no room for trunc gives 0", n == 0 && out[0] == '\0');

    // The hook cut the line: trunc="1" also when the rest fits.
    meta.truncated = true;
    render("I (1) t: m", &meta, out, sizeof out);
    char want2[120];
    snprintf(want2, sizeof want2, "%s trunc=\"1\"] m", head);
    check_str("hook truncation marks trunc=\"1\"", out, want2);

    // A buffer that cannot hold the header gives 0 and an empty string.
    meta.truncated = false;
    n = render("I (1) t: m", &meta, out, 20);
    check("buffer too small for header", n == 0 && out[0] == '\0');
    check("NULL output", dc_syslog_format(NULL, 10, &meta, NULL) == 0);
}

int main(void)
{
    test_severity();
    test_strip();
    test_parse();
    test_format();
    test_sequence();
    test_truncation();
    printf(fails ? "\n%d FAILED\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
