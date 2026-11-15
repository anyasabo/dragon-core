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
// as one UDP datagram. If the network is down, the lines stay in the queue
// until the queue is full. The task does not log, and the hook ignores lines
// from the task, so the log cannot loop.
//
// Message format (see dc_syslog_format.h):
//   <PRI>1 TIMESTAMP HOSTNAME APP-NAME TAG - [dc@32473 seq="N" up="MS"] MSG
// seq increments for each log line, also for dropped lines. A gap in seq on
// the server shows dropped lines. up is the uptime in ms (esp_timer).
//
// TIMESTAMP: dc_syslog does not depend on dc_time. The product can set
// dc_syslog_config_t.timestamp to dc_time_format_now (same signature). Without
// a callback, or before the clock is synced, TIMESTAMP is "-".
//
// RAM: the slots use depth * (slot_size + 56, rounded up to 8) bytes, the datagram buffer
// slot_size + 256 bytes, and the task stack DC_SYSLOG_TASK_STACK bytes. All of
// it comes from the heap at start. With the default sizes (16 slots of 192
// bytes), the total is approximately 8 KB, plus 236 bytes of static RAM.

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
