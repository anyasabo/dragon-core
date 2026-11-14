// Host test for the dc_time timestamp formatter (dc_time_format.h): fixed
// dates, leap years, range limits, buffer limits, and a comparison with the
// gmtime_r() of the host libc over the full range.
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
