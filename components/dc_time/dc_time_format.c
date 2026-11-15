#include "dc_time_format.h"

// Last millisecond of 9999-12-31T23:59:59.999Z.
#define MAX_UNIX_MS 253402300799999LL

static void put_digits(char *p, unsigned value, int width)
{
    for (int i = width - 1; i >= 0; i--) {
        p[i] = (char)('0' + value % 10u);
        value /= 10u;
    }
}

size_t dc_time_format_rfc3339(int64_t unix_ms, char *buf, size_t len)
{
    if (buf == NULL || len == 0) return 0;
    buf[0] = '\0';
    if (len < DC_TIME_RFC3339_SIZE || unix_ms < 0 || unix_ms > MAX_UNIX_MS) return 0;

    int64_t secs = unix_ms / 1000;
    unsigned ms = (unsigned)(unix_ms % 1000);
    int32_t days = (int32_t)(secs / 86400);
    unsigned sod = (unsigned)(secs % 86400);

    // Civil date from days since 1970-01-01 (H. Hinnant, "civil_from_days").
    // Only integer math: the ESP32-C3 has no FPU.
    int32_t z = days + 719468;
    int32_t era = z / 146097;
    uint32_t doe = (uint32_t)(z - era * 146097);
    uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int32_t year = (int32_t)yoe + era * 400;
    uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    uint32_t mp = (5 * doy + 2) / 153;
    unsigned day = doy - (153 * mp + 2) / 5 + 1;
    unsigned month = mp < 10 ? mp + 3 : mp - 9;
    if (month <= 2) year++;

    char *p = buf;
    put_digits(p, (unsigned)year, 4);      p += 4; *p++ = '-';
    put_digits(p, month, 2);               p += 2; *p++ = '-';
    put_digits(p, day, 2);                 p += 2; *p++ = 'T';
    put_digits(p, sod / 3600u, 2);         p += 2; *p++ = ':';
    put_digits(p, (sod / 60u) % 60u, 2);   p += 2; *p++ = ':';
    put_digits(p, sod % 60u, 2);           p += 2; *p++ = '.';
    put_digits(p, ms, 3);                  p += 3; *p++ = 'Z';
    *p = '\0';
    return DC_TIME_RFC3339_LEN;
}

int64_t dc_time_offset_us(int64_t wall_us, int64_t mono_us)
{
    return wall_us - mono_us;
}

bool dc_time_wall_ms(bool synced, int64_t offset_us, int64_t mono_us, int64_t *unix_ms)
{
    if (!synced || unix_ms == NULL) return false;
    int64_t wall_us = mono_us + offset_us;
    if (wall_us < 0) return false;
    *unix_ms = wall_us / 1000;
    return true;
}
