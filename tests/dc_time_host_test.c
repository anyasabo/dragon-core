// Host test for the pure parts of dc_time (dc_time_format.h): fixed dates,
// leap years, range limits, buffer limits, the clock offset math, and a
// comparison with the gmtime_r() of the host libc over the full range.
#define _DEFAULT_SOURCE   // gmtime_r with -std=c11
#include "dc_time_format.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static int fails = 0;

static void expect(const char *name, int64_t ms, size_t len, const char *want)
{
    char buf[64];
    memset(buf, 'X', sizeof buf);
    size_t n = dc_time_format_rfc3339(ms, buf, len);
    size_t want_n = strlen(want);
    int good = n == want_n && strcmp(buf, want) == 0 &&
               (len >= sizeof buf || buf[len] == 'X');
    if (!good) fails++;
    printf("[%s] %-24s n=%zu out=\"%s\"\n", good ? "PASS" : "FAIL", name, n, buf);
}

static void compare_with_libc(int64_t ms)
{
    time_t secs = (time_t)(ms / 1000);
    struct tm tm;
    if (!gmtime_r(&secs, &tm)) return;
    char want[80];
    snprintf(want, sizeof want, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec, (int)(ms % 1000));
    char got[DC_TIME_RFC3339_SIZE];
    dc_time_format_rfc3339(ms, got, sizeof got);
    if (strcmp(got, want) != 0) {
        if (fails < 10) printf("[FAIL] libc %lld want=%s got=%s\n", (long long)ms, want, got);
        fails++;
    }
}

static void check_bool(const char *name, int good)
{
    if (!good) fails++;
    printf("[%s] %s\n", good ? "PASS" : "FAIL", name);
}

static void test_offset(void)
{
    int64_t ms = 42;
    // Before the first sync: no time, *unix_ms stays the same.
    check_bool("not synced gives false", !dc_time_wall_ms(false, 0, 5000000, &ms) && ms == 42);
    check_bool("NULL output gives false", !dc_time_wall_ms(true, 0, 0, NULL));

    // Sync at esp_timer 5 s, wall clock 2024-11-14T23:40:11.123456Z.
    int64_t wall_us = 1731627611123456LL, mono_us = 5000000;
    int64_t off = dc_time_offset_us(wall_us, mono_us);
    check_bool("offset is wall - mono", off == wall_us - mono_us);
    check_bool("at the sync", dc_time_wall_ms(true, off, mono_us, &ms) && ms == 1731627611123LL);
    // 1.5 ms later: 123456 + 1500 = 124956 us, rounded down to 124 ms.
    check_bool("1.5 ms later rounds down",
               dc_time_wall_ms(true, off, mono_us + 1500, &ms) && ms == 1731627611124LL);
    // 49.7 days of uptime (past the 32-bit ms wrap): no overflow.
    int64_t later = mono_us + 4294967296000LL;
    check_bool("after 2^32 ms of uptime",
               dc_time_wall_ms(true, off, later, &ms) && ms == 1731627611123LL + 4294967296LL);
    // A wall clock before 1970 is not valid.
    check_bool("before 1970 gives false",
               !dc_time_wall_ms(true, dc_time_offset_us(0, 10), 0, &ms));

    char buf[DC_TIME_RFC3339_SIZE];
    dc_time_wall_ms(true, off, mono_us + 1500, &ms);
    dc_time_format_rfc3339(ms, buf, sizeof buf);
    check_bool("formatted after sync", strcmp(buf, "2024-11-14T23:40:11.124Z") == 0);
}

int main(void)
{
    expect("epoch", 0, 25, "1970-01-01T00:00:00.000Z");
    expect("ms digits", 1731627611123LL, 25, "2024-11-14T23:40:11.123Z");
    expect("leap day 2000", 951782400000LL, 25, "2000-02-29T00:00:00.000Z");
    expect("leap day 2024 end", 1709251199999LL, 25, "2024-02-29T23:59:59.999Z");
    expect("no leap 2100", 4107542400000LL, 25, "2100-03-01T00:00:00.000Z");
    expect("year end 2026", 1798761599999LL, 25, "2026-12-31T23:59:59.999Z");
    expect("max 9999", 253402300799999LL, 25, "9999-12-31T23:59:59.999Z");
    expect("above max", 253402300800000LL, 25, "");
    expect("negative", -1, 25, "");
    expect("buffer 24 too small", 0, 24, "");
    expect("buffer 1", 0, 1, "");
    expect("buffer 64", 0, 64, "1970-01-01T00:00:00.000Z");

    // len 0 and NULL must not write.
    {
        char b = 'X';
        int good = dc_time_format_rfc3339(0, &b, 0) == 0 && b == 'X' &&
                   dc_time_format_rfc3339(0, NULL, 25) == 0;
        if (!good) fails++;
        printf("[%s] len 0 and NULL\n", good ? "PASS" : "FAIL");
    }

    test_offset();

    // Compare with libc: each day boundary of 1970-2200, then big steps to 9999.
    int before = fails;
    for (int64_t ms = 0; ms < 7258118400000LL; ms += 86400000LL) {
        compare_with_libc(ms);
        compare_with_libc(ms + 86399999LL);
    }
    for (int64_t ms = 0; ms <= 253402300799999LL; ms += 9876543211LL) compare_with_libc(ms);
    printf("[%s] libc comparison\n", fails == before ? "PASS" : "FAIL");

    printf(fails ? "\n%d FAILED\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
