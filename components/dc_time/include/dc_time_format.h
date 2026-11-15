#pragma once

// Pure parts of dc_time: the timestamp formatter and the clock offset math.
// They have no ESP-IDF dependency, so the host test (tests/dc_time_host_test.c)
// compiles them.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Length of "YYYY-MM-DDTHH:MM:SS.mmmZ" without the NUL.
#define DC_TIME_RFC3339_LEN  24
// Buffer size for one timestamp, with the NUL.
#define DC_TIME_RFC3339_SIZE (DC_TIME_RFC3339_LEN + 1)

// Write unix_ms (milliseconds since 1970-01-01T00:00:00Z) as an RFC 3339 UTC
// timestamp with milliseconds, for example "2026-11-14T23:40:11.123Z". The
// format is also a valid RFC 5424 TIMESTAMP. The function uses no libc time
// functions and no time zone. It does not allocate and does not block.
// Returns DC_TIME_RFC3339_LEN. Returns 0 and writes an empty string (if len > 0)
// if len < DC_TIME_RFC3339_SIZE or unix_ms is not in years 1970 to 9999.
size_t dc_time_format_rfc3339(int64_t unix_ms, char *buf, size_t len);

// Offset from the monotonic clock (esp_timer) to the wall clock, from one pair
// of readings at a sync. Both values are in microseconds.
int64_t dc_time_offset_us(int64_t wall_us, int64_t mono_us);

// Wall clock in ms since 1970 at the monotonic time mono_us, with the offset
// of the last sync. If synced is false, unix_ms is NULL, or the result is
// before 1970, returns false and does not write *unix_ms.
bool dc_time_wall_ms(bool synced, int64_t offset_us, int64_t mono_us, int64_t *unix_ms);

#ifdef __cplusplus
}
#endif
