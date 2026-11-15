#pragma once

// Send the ESP-IDF log as RFC 5424 syslog over UDP.
//
// dc_syslog installs an esp_log vprintf hook. The hook chains to the previous
// hook, for example the hook of dc_evlog. Thus the UART and /console continue
// to get each line. The hook does not block and does not allocate. It
// formats the line into a free slot and puts the slot into a queue. If no slot
// is free, the hook drops the line and counts the drop. The newlib vsnprintf()
// can allocate for %f. That is also true for all other log paths.
//
// A sender task (priority 1) takes the lines from the queue and sends each line
// as one UDP datagram. The station interface (WIFI_STA_DEF) must have an
// address for a send. Until then, the lines stay in the queue until the queue
// is full. The sender looks up a host name again every 10 minutes and after an
// error.
//
// Message format (see dc_syslog_format.h):
//   <PRI>1 TIMESTAMP HOSTNAME APP-NAME TAG - [dc@32473 seq="N" up="MS"] MSG
// seq increments for each log line, also for dropped lines. A gap in seq on
// the server shows dropped lines. up is the uptime in ms (esp_timer).
// Lines can arrive out of seq order. Two tasks can log at the same time, and
// UDP does not keep the order. If the order is important, sort by seq on the
// server.
//
// TIMESTAMP: dc_syslog does not depend on dc_time. The product can set
// dc_syslog_config_t.timestamp to dc_time_format_now (same signature). Without
// a callback, or before the clock is synced, TIMESTAMP is "-".
//
// LIMITS
// - ESP-IDF log v1 only. Log v1 calls the hook one time for each line. Log v2
//   (CONFIG_LOG_VERSION_2, ESP-IDF 5.5 and later) calls it three times: the
//   prefix, the message, and the color reset with the newline. If
//   CONFIG_LOG_VERSION_2 is set, dc_syslog stops the build with #error. The
//   default of ESP-IDF v6.1 is still log v1 (components/log/Kconfig:
//   default LOG_VERSION_1).
// - Install window: esp_log_set_vprintf() installs the new hook and gives the
//   previous hook in one call. Another task can log in the few instructions
//   before dc_syslog keeps the previous hook. Then that line goes to vprintf()
//   (UART), not to the previous hook (dc_evlog). The API gives no way to read
//   the previous hook first.
// - Log loop: the hook ignores lines from ISRs and from the sender task. But a
//   send can make other tasks log, for example the Wi-Fi or lwIP tasks with a
//   verbose or debug log level. Each such line makes a new datagram, which can
//   make a new line. With the default log levels this does not occur. If you
//   set Wi-Fi or lwIP to debug, do not use dc_syslog at the same time.
//
// WHY PUSH, AND NOT A CURSOR ON THE dc_evlog RING (draft PR #69)
// - Each syslog server can receive it (Alloy, rsyslog, syslog-ng). No HTTP
//   poller is necessary on the server.
// - Each line keeps its severity, sequence number and timestamp as fields.
// - If the ring wraps between two polls, a poller loses lines. dc_syslog
//   counts each lost line, and the seq gap shows where.
// - The two can be used together. The /console ring stays for a browser view.
//
// RAM: the slots use depth * (slot_size + 56, rounded up to 8) bytes, the
// datagram buffer slot_size + 256 bytes, and the task stack DC_SYSLOG_TASK_STACK
// bytes. All of it comes from the heap at start. With the default sizes (16
// slots of 192 bytes), the total is approximately 8 KB, plus 236 bytes of
// static RAM.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DC_SYSLOG_DEFAULT_PORT        514
#define DC_SYSLOG_DEFAULT_QUEUE_DEPTH 16
#define DC_SYSLOG_DEFAULT_SLOT_SIZE   192   // bytes of one formatted log line
#define DC_SYSLOG_MIN_SLOT_SIZE       64
#define DC_SYSLOG_MAX_SLOT_SIZE       1024
#define DC_SYSLOG_TASK_STACK          3072
#define DC_SYSLOG_TASK_PRIORITY       1

// Write the RFC 5424 TIMESTAMP of "now" into buf (NUL-terminated, at most len
// bytes). If the wall clock is not known, return false. The hook calls it for
// each line, so it must not block. dc_time_format_now() has this signature.
typedef bool (*dc_syslog_timestamp_fn)(char *buf, size_t len);

typedef struct {
    const char *server;          // syslog server: host name or IPv4 address (required)
    uint16_t port;               // UDP port. 0 gives DC_SYSLOG_DEFAULT_PORT.
    const char *app_name;        // APP-NAME, for example "dragonbreath"
    const char *hostname;        // HOSTNAME, for example the DHCP host name
    uint8_t queue_depth;         // number of slots. 0 gives the default.
    uint16_t slot_size;          // bytes for one line. 0 gives the default.
    dc_syslog_timestamp_fn timestamp;  // NULL: TIMESTAMP is always "-"
} dc_syslog_config_t;

typedef struct {
    uint32_t sent;            // datagrams that sendto() accepted
    uint32_t dropped;         // lines dropped because no slot was free
    uint32_t send_errors;     // failed sendto() or address lookups
    uint32_t queued;          // lines in the queue now
    uint32_t stack_free_min;  // smallest free stack of the sender task, bytes
} dc_syslog_stats_t;

// Start dc_syslog. Call it once, after esp_netif_init() (dc_wifi_start() calls
// it). dc_syslog copies the strings. Errors:
// - ESP_ERR_INVALID_ARG: no server, a string is too long, or a size is out of range.
// - ESP_ERR_INVALID_STATE: dc_syslog is already started.
// - ESP_ERR_NO_MEM: the slots, queues or task could not be allocated.
esp_err_t dc_syslog_start(const dc_syslog_config_t *cfg);

// Copy the counters into out. Before dc_syslog_start(), all counters are 0.
void dc_syslog_stats(dc_syslog_stats_t *out);

#ifdef __cplusplus
}
#endif
