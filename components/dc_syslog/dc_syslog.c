#include "dc_syslog.h"
#include "dc_syslog_format.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

// The sender task must not log: its lines would come back into the queue. Only
// dc_syslog_start() logs, before the hook is installed.
static const char *TAG = "dc_syslog";

#define SERVER_MAX     63
#define RETRY_MIN_MS   1000
#define RETRY_MAX_MS   30000
#define RESOLVE_US     (10LL * 60 * 1000000)   // look up a host name again after 10 min

typedef struct {
    uint32_t seq;
    uint16_t len;            // bytes in text, without a NUL
    bool truncated;          // vsnprintf() cut the line
    uint64_t up_ms;
    char ts[DC_SYSLOG_TIMESTAMP_MAX + 1];
    char text[];             // slot_size bytes
} slot_t;

// Configuration, written once by dc_syslog_start() before the hook is installed.
static char s_server[SERVER_MAX + 1];
static uint16_t s_port;
static char s_app[DC_SYSLOG_APPNAME_MAX + 1];
static char s_host[DC_SYSLOG_HOSTNAME_MAX + 1];
static size_t s_slot_size;
static size_t s_slot_stride;
static dc_syslog_timestamp_fn s_timestamp;

static uint8_t *s_slots;          // depth * s_slot_stride bytes
static QueueHandle_t s_free;      // indexes of free slots
static QueueHandle_t s_ready;     // indexes of slots to send, oldest first
static char *s_dgram;             // one formatted datagram (sender task only)
static size_t s_dgram_size;
static TaskHandle_t s_task;
static vprintf_like_t s_prev_vprintf;
static volatile bool s_running;

// Counters. A spinlock guards them: the ESP32-C3 has no atomic instructions.
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_seq;
static uint32_t s_sent;
static uint32_t s_dropped;
static uint32_t s_send_errors;

static inline slot_t *slot_at(uint8_t idx)
{
    return (slot_t *)(s_slots + (size_t)idx * s_slot_stride);
}

// ---- log hook (any task: no block, no allocation) ----

static void enqueue_line(const char *fmt, va_list ap)
{
    uint8_t idx;
    bool have_slot = xQueueReceive(s_free, &idx, 0) == pdTRUE;

    portENTER_CRITICAL(&s_mux);
    uint32_t seq = ++s_seq;
    if (!have_slot) s_dropped++;
    portEXIT_CRITICAL(&s_mux);
    if (!have_slot) return;

    slot_t *sl = slot_at(idx);
    sl->seq = seq;
    sl->up_ms = (uint64_t)(esp_timer_get_time() / 1000);
    if (!s_timestamp || !s_timestamp(sl->ts, sizeof sl->ts)) sl->ts[0] = '\0';
    int n = vsnprintf(sl->text, s_slot_size, fmt, ap);
    if (n < 0) n = 0;
    sl->truncated = (size_t)n >= s_slot_size;
    sl->len = (uint16_t)(sl->truncated ? s_slot_size - 1 : (size_t)n);
    // s_ready has room for every slot, so this cannot fail.
    (void)xQueueSend(s_ready, &idx, 0);
}

static int syslog_vprintf(const char *fmt, va_list ap)
{
    if (s_running && !xPortInIsrContext() &&
        xTaskGetSchedulerState() == taskSCHEDULER_RUNNING &&
        xTaskGetCurrentTaskHandle() != s_task) {
        va_list ap2;
        va_copy(ap2, ap);
        enqueue_line(fmt, ap2);
        va_end(ap2);
    }
    return s_prev_vprintf ? s_prev_vprintf(fmt, ap) : vprintf(fmt, ap);
}

// ---- sender task ----

// Send only through the station interface. In AP-only mode (provisioning) the
// AP interface has an address, but the syslog server is not on that network.
static bool network_up(void)
{
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip;
    return netif && esp_netif_is_netif_up(netif) &&
           esp_netif_get_ip_info(netif, &ip) == ESP_OK && ip.ip.addr != 0;
}

static bool resolve(struct sockaddr_in *addr)
{
    memset(addr, 0, sizeof *addr);
    addr->sin_family = AF_INET;
    addr->sin_port = htons(s_port);
    if (inet_aton(s_server, &addr->sin_addr)) return true;

    struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_DGRAM };
    struct addrinfo *res = NULL;
    if (getaddrinfo(s_server, NULL, &hints, &res) != 0 || res == NULL) return false;
    addr->sin_addr = ((struct sockaddr_in *)res->ai_addr)->sin_addr;
    freeaddrinfo(res);
    return true;
}

static size_t build_datagram(slot_t *sl)
{
    // The sender owns the slot now. strip() works in place and can run again.
    sl->len = (uint16_t)dc_syslog_strip(sl->text, sl->len);
    dc_syslog_line_t line;
    dc_syslog_parse_line(sl->text, sl->len, &line);
    dc_syslog_meta_t meta = {
        .timestamp = sl->ts,
        .hostname = s_host,
        .app_name = s_app,
        .seq = sl->seq,
        .up_ms = sl->up_ms,
        .truncated = sl->truncated,
    };
    return dc_syslog_format(s_dgram, s_dgram_size, &meta, &line);
}

static void count_error(void)
{
    portENTER_CRITICAL(&s_mux);
    s_send_errors++;
    portEXIT_CRITICAL(&s_mux);
}

// State of the sender task. Only the sender task uses it.
static int s_sock = -1;
static bool s_have_addr;
static int64_t s_resolved_us;
static struct sockaddr_in s_addr;

static void sender_task(void *arg)
{
    (void)arg;
    uint32_t retry_ms = RETRY_MIN_MS;

    for (;;) {
        uint8_t idx;
        // Keep the line in the queue until it is sent.
        xQueuePeek(s_ready, &idx, portMAX_DELAY);

        if (!network_up()) {
            vTaskDelay(pdMS_TO_TICKS(RETRY_MIN_MS));
            continue;
        }
        if (s_sock < 0) s_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        // The address of a host name can change (DHCP). An IP address costs nothing.
        int64_t now_us = esp_timer_get_time();
        if (s_have_addr && now_us - s_resolved_us >= RESOLVE_US) s_have_addr = false;
        if (s_sock >= 0 && !s_have_addr) {
            s_have_addr = resolve(&s_addr);
            s_resolved_us = now_us;
        }

        bool ok = false;
        size_t n = 0;
        if (s_sock >= 0 && s_have_addr) {
            // n is 0 only if the header does not fit. s_dgram_size prevents that.
            n = build_datagram(slot_at(idx));
            ok = n == 0 ||
                 sendto(s_sock, s_dgram, n, 0, (struct sockaddr *)&s_addr, sizeof s_addr) == (int)n;
        }
        if (!ok) {
            count_error();
            s_have_addr = false;            // look up the server again
            vTaskDelay(pdMS_TO_TICKS(retry_ms));
            retry_ms = retry_ms * 2 > RETRY_MAX_MS ? RETRY_MAX_MS : retry_ms * 2;
            continue;
        }

        retry_ms = RETRY_MIN_MS;
        (void)xQueueReceive(s_ready, &idx, 0);
        (void)xQueueSend(s_free, &idx, 0);
        portENTER_CRITICAL(&s_mux);
        if (n > 0) s_sent++;
        else s_dropped++;
        portEXIT_CRITICAL(&s_mux);
    }
}

// ---- API ----

static bool copy_str(char *dst, size_t dst_sz, const char *src)
{
    if (src == NULL) src = "";
    size_t n = strlen(src);
    if (n >= dst_sz) return false;
    memcpy(dst, src, n + 1);
    return true;
}

esp_err_t dc_syslog_start(const dc_syslog_config_t *cfg)
{
    if (s_task != NULL) return ESP_ERR_INVALID_STATE;
    if (cfg == NULL || cfg->server == NULL || cfg->server[0] == '\0') return ESP_ERR_INVALID_ARG;
    if (!copy_str(s_server, sizeof s_server, cfg->server) ||
        !copy_str(s_app, sizeof s_app, cfg->app_name) ||
        !copy_str(s_host, sizeof s_host, cfg->hostname)) return ESP_ERR_INVALID_ARG;

    size_t depth = cfg->queue_depth ? cfg->queue_depth : DC_SYSLOG_DEFAULT_QUEUE_DEPTH;
    size_t slot_size = cfg->slot_size ? cfg->slot_size : DC_SYSLOG_DEFAULT_SLOT_SIZE;
    if (slot_size < DC_SYSLOG_MIN_SLOT_SIZE || slot_size > DC_SYSLOG_MAX_SLOT_SIZE)
        return ESP_ERR_INVALID_ARG;

    s_port = cfg->port ? cfg->port : DC_SYSLOG_DEFAULT_PORT;
    s_timestamp = cfg->timestamp;
    s_slot_size = slot_size;
    // slot_t has a uint64_t, so each slot must start on _Alignof(slot_t).
    const size_t align = _Alignof(slot_t);
    s_slot_stride = (sizeof(slot_t) + slot_size + align - 1) & ~(align - 1);
    s_dgram_size = DC_SYSLOG_HEADER_MAX + slot_size;

    s_slots = aligned_alloc(align, depth * s_slot_stride);
    s_dgram = malloc(s_dgram_size);
    s_free = xQueueCreate(depth, sizeof(uint8_t));
    s_ready = xQueueCreate(depth, sizeof(uint8_t));
    if (!s_slots || !s_dgram || !s_free || !s_ready) goto no_mem;
    for (size_t i = 0; i < depth; i++) {
        uint8_t idx = (uint8_t)i;
        xQueueSend(s_free, &idx, 0);
    }
    if (xTaskCreate(sender_task, "dc_syslog", DC_SYSLOG_TASK_STACK, NULL,
                    DC_SYSLOG_TASK_PRIORITY, &s_task) != pdPASS) {
        s_task = NULL;
        goto no_mem;
    }

    ESP_LOGI(TAG, "sending log to %s:%u, %u slots of %u bytes",
             s_server, (unsigned)s_port, (unsigned)depth, (unsigned)slot_size);
    s_running = true;
    s_prev_vprintf = esp_log_set_vprintf(syslog_vprintf);
    return ESP_OK;

no_mem:
    if (s_free) vQueueDelete(s_free);
    if (s_ready) vQueueDelete(s_ready);
    free(s_slots);
    free(s_dgram);
    s_free = s_ready = NULL;
    s_slots = NULL;
    s_dgram = NULL;
    return ESP_ERR_NO_MEM;
}

void dc_syslog_stats(dc_syslog_stats_t *out)
{
    if (out == NULL) return;
    portENTER_CRITICAL(&s_mux);
    out->sent = s_sent;
    out->dropped = s_dropped;
    out->send_errors = s_send_errors;
    portEXIT_CRITICAL(&s_mux);
    out->queued = s_ready ? (uint32_t)uxQueueMessagesWaiting(s_ready) : 0;
    // On ESP-IDF, the high water mark is in bytes (StackType_t is uint8_t).
    out->stack_free_min = s_task ? (uint32_t)uxTaskGetStackHighWaterMark(s_task) : 0;
}
