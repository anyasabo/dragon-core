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
// They do not call gettimeofday(), thus they do not take a mutex. They use
// portENTER_CRITICAL_SAFE(), thus a log hook and an ISR can call them.
//
// NTP SERVER
// - Recommended: set dc_time_config_t.server to a known NTP server, for
//   example the home server that runs chrony.
// - DHCP (use_dhcp = true): many routers do not send DHCP option 42. Also,
//   lwIP gives the option to SNTP only from a lease that comes after
//   dc_time_start(). Thus call dc_time_start() before the station gets its IP
//   address. DHCP needs CONFIG_LWIP_DHCP_GET_NTP_SRV=y.
// - DHCP with a fallback (use_dhcp = true and server set): the DHCP server
//   goes in SNTP slot 0 and the configured server in slot 1. This needs
//   CONFIG_LWIP_SNTP_MAX_SERVERS=2 (or more). When DHCP gives a server, lwIP
//   clears the other slots. Thus dc_time sets slot 1 again at each
//   IP_EVENT_STA_GOT_IP.
//
// If there is no sync DC_TIME_SYNC_WARN_MIN minutes after dc_time_start(),
// dc_time logs one warning. An esp_timer one-shot does the check. The product
// can also poll dc_time_is_synced().

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "dc_time_format.h"

#ifdef __cplusplus
extern "C" {
#endif

// Minutes after dc_time_start() for the "not synced" warning.
#define DC_TIME_SYNC_WARN_MIN 5

typedef struct {
    // NTP server: a host name or an IPv4 address. dc_time copies the string.
    // NULL or "" means no configured server.
    const char *server;
    // Also use the NTP server from DHCP option 42. See NTP SERVER above.
    bool use_dhcp;
} dc_time_config_t;

// Start SNTP. Call it once, after esp_netif_init() (dc_wifi_start() calls it).
// Errors:
// - ESP_ERR_INVALID_STATE: SNTP is already started.
// - ESP_ERR_INVALID_ARG: cfg is NULL, there is no server and no DHCP, or the
//   server name has 64 or more characters.
// - ESP_ERR_NOT_SUPPORTED: use_dhcp and CONFIG_LWIP_DHCP_GET_NTP_SRV is off, or
//   use_dhcp with a server and CONFIG_LWIP_SNTP_MAX_SERVERS is less than 2.
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
