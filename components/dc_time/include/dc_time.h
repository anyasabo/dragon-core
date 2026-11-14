#pragma once

// Wall clock from SNTP, for log timestamps only.
//
// SAFETY: Nothing that is related to safety or control uses this clock. Safety
// and control timers use esp_timer (monotonic). The wall clock can be absent,
// wrong, or change at each sync. Use it only to put a time on log lines and on
// other diagnostic data.
//
// dc_time uses esp_netif_sntp. lwIP runs SNTP in the TCP/IP task, so dc_time
// adds no task. At each sync, dc_time keeps the offset between the wall clock
// and esp_timer. dc_time_now_ms() and dc_time_format_now() use that offset.
// They do not call gettimeofday(), thus they do not take a mutex. A log hook can
// call them.
//
// NTP server from DHCP: if dc_time_config_t.server is NULL or empty, dc_time
// uses the NTP server from DHCP option 42. This needs CONFIG_LWIP_DHCP_GET_NTP_SRV=y
// in the sdkconfig of the product. If that option is off, dc_time_start() returns
// ESP_ERR_NOT_SUPPORTED. Call dc_time_start() before the station gets its IP
// address. If the lease comes first, lwIP does not give its NTP server to SNTP
// until the next lease.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "dc_time_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    // NTP server: a host name or an IPv4 address. dc_time copies the string.
    // NULL or "" selects the NTP server from DHCP.
    const char *server;
} dc_time_config_t;

// Start SNTP. Call it once, after esp_netif_init() (dc_wifi_start() calls it).
// cfg can be NULL (same as server = NULL). Errors:
// - ESP_ERR_INVALID_STATE: SNTP is already started.
// - ESP_ERR_INVALID_ARG: the server name has 64 or more characters.
// - ESP_ERR_NOT_SUPPORTED: no server and CONFIG_LWIP_DHCP_GET_NTP_SRV is off.
esp_err_t dc_time_start(const dc_time_config_t *cfg);

// True after the first SNTP sync. It stays true after that.
bool dc_time_is_synced(void);

// Write the current UTC time in milliseconds since 1970 into *unix_ms.
// Returns false (and does not write) before the first sync.
bool dc_time_now_ms(int64_t *unix_ms);

// Write the current UTC time as an RFC 3339 / RFC 5424 timestamp with
// milliseconds (see dc_time_format_rfc3339). len must be at least
// DC_TIME_RFC3339_SIZE. Returns false and writes "" before the first sync.
// dc_syslog can use this function as its timestamp callback.
bool dc_time_format_now(char *buf, size_t len);

#ifdef __cplusplus
}
#endif
