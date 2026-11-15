// Host test for the device part of dc_syslog (dc_syslog.c): the log hook, the
// slot queue, drops, sequence gaps, the hook chain, and the sender task with
// network down, DNS errors and send errors.
//
// The stubs in tests/stubs_dc_syslog declare the FreeRTOS, esp_netif, esp_log
// and lwIP functions. This file implements them. The sender task runs in the
// test thread. When it waits for an empty queue, or when it waits too often,
// the stub jumps back to the test with longjmp().
#include "dc_syslog.h"

#include <setjmp.h>
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

static int fails = 0;

static void check(const char *name, int good)
{
    if (!good) fails++;
    printf("[%s] %s\n", good ? "PASS" : "FAIL", name);
}

// ---- stub state ----
static jmp_buf g_stop;
static int g_delays, g_delay_limit = 3;
static int64_t g_now_us = 5000000;
static bool g_net_up = true;
static bool g_in_isr = false;
static bool g_send_fail = false;
static bool g_dns_fail = false;
static int g_dns_calls = 0;
static bool g_ts_ok = false;

struct stub_task { int unused; };
static struct stub_task g_sender_task, g_app_task;
static TaskHandle_t g_current = &g_app_task;
static TaskFunction_t g_task_fn;

static vprintf_like_t g_installed;
static int g_prev_calls;
static char g_prev_last[256];

#define MAX_SENT 32
static char g_sent[MAX_SENT][600];
static int g_sent_count;
static struct sockaddr_in g_sent_to;

// ---- FreeRTOS stubs ----
struct stub_queue { unsigned len, size, head, count; unsigned char *buf; };

QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t item_size)
{
    QueueHandle_t q = calloc(1, sizeof *q);
    q->len = length;
    q->size = item_size;
    q->buf = calloc(length, item_size);
    return q;
}

void vQueueDelete(QueueHandle_t q)
{
    free(q->buf);
    free(q);
}

BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t wait)
{
    (void)wait;
    if (q->count == q->len) return pdFALSE;
    memcpy(q->buf + ((q->head + q->count) % q->len) * q->size, item, q->size);
    q->count++;
    return pdTRUE;
}

static BaseType_t take(QueueHandle_t q, void *item, TickType_t wait, bool remove)
{
    if (q->count == 0) {
        if (wait == portMAX_DELAY) longjmp(g_stop, 1);   // the task waits: stop it
        return pdFALSE;
    }
    memcpy(item, q->buf + q->head * q->size, q->size);
    if (remove) {
        q->head = (q->head + 1) % q->len;
        q->count--;
    }
    return pdTRUE;
}

BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t wait) { return take(q, item, wait, true); }
BaseType_t xQueuePeek(QueueHandle_t q, void *item, TickType_t wait) { return take(q, item, wait, false); }
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t q) { return q->count; }

BaseType_t xTaskCreate(TaskFunction_t fn, const char *name, uint32_t stack,
                       void *arg, UBaseType_t prio, TaskHandle_t *out)
{
    (void)name; (void)stack; (void)arg; (void)prio;
    g_task_fn = fn;
    *out = &g_sender_task;
    return pdPASS;
}

void vTaskDelay(TickType_t ticks)
{
    (void)ticks;
    if (++g_delays >= g_delay_limit) longjmp(g_stop, 1);
}

BaseType_t xTaskGetSchedulerState(void) { return taskSCHEDULER_RUNNING; }
TaskHandle_t xTaskGetCurrentTaskHandle(void) { return g_current; }
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task) { (void)task; return 1234; }
BaseType_t xPortInIsrContext(void) { return g_in_isr; }

// ---- ESP-IDF stubs ----
int64_t esp_timer_get_time(void) { return g_now_us; }

static int prev_hook(const char *fmt, va_list ap)
{
    g_prev_calls++;
    return vsnprintf(g_prev_last, sizeof g_prev_last, fmt, ap);
}

vprintf_like_t esp_log_set_vprintf(vprintf_like_t func)
{
    vprintf_like_t old = g_installed;
    g_installed = func;
    return old;
}

static struct esp_netif_obj { int unused; } *g_netif = (void *)&g_app_task;
esp_netif_t *esp_netif_get_default_netif(void) { return g_netif; }
bool esp_netif_is_netif_up(esp_netif_t *netif) { (void)netif; return g_net_up; }
esp_err_t esp_netif_get_ip_info(esp_netif_t *netif, esp_netif_ip_info_t *ip)
{
    (void)netif;
    ip->ip.addr = g_net_up ? 0x0a01a8c0u : 0;
    return ESP_OK;
}

// ---- lwIP stubs ----
int stub_socket(int domain, int type, int protocol)
{
    (void)domain; (void)type; (void)protocol;
    return 3;
}

ssize_t stub_sendto(int fd, const void *buf, size_t len, int flags,
                    const struct sockaddr *to, socklen_t tolen)
{
    (void)fd; (void)flags; (void)tolen;
    if (g_send_fail) return -1;
    if (g_sent_count < MAX_SENT && len < sizeof g_sent[0]) {
        memcpy(g_sent[g_sent_count], buf, len);
        g_sent[g_sent_count][len] = '\0';
    }
    g_sent_count++;
    memcpy(&g_sent_to, to, sizeof g_sent_to);
    return (ssize_t)len;
}

static struct sockaddr_in g_dns_addr;
static struct addrinfo g_dns_result;

int stub_getaddrinfo(const char *node, const char *service,
                     const struct addrinfo *hints, struct addrinfo **res)
{
    (void)service; (void)hints;
    g_dns_calls++;
    if (g_dns_fail || strcmp(node, "syslog.lan") != 0) return EAI_FAIL;
    g_dns_addr.sin_family = AF_INET;
    inet_aton("192.0.2.10", &g_dns_addr.sin_addr);
    g_dns_result.ai_addr = (struct sockaddr *)&g_dns_addr;
    *res = &g_dns_result;
    return 0;
}

void stub_freeaddrinfo(struct addrinfo *res) { (void)res; }

// ---- helpers ----
static void app_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    g_installed(fmt, ap);
    va_end(ap);
}

static void run_sender(void)
{
    g_delays = 0;
    if (setjmp(g_stop) == 0) g_task_fn(NULL);
}

static bool timestamp_cb(char *buf, size_t len)
{
    if (!g_ts_ok) return false;
    snprintf(buf, len, "2026-11-14T23:40:11.123Z");
    return true;
}

static dc_syslog_stats_t stats(void)
{
    dc_syslog_stats_t s;
    dc_syslog_stats(&s);
    return s;
}

int main(void)
{
    g_installed = prev_hook;   // the hook of dc_evlog in the product

    dc_syslog_stats_t s = stats();
    check("stats are 0 before start", s.sent == 0 && s.dropped == 0 && s.queued == 0);

    check("start NULL", dc_syslog_start(NULL) == ESP_ERR_INVALID_ARG);
    check("start no server", dc_syslog_start(&(dc_syslog_config_t){ .server = "" }) ==
                             ESP_ERR_INVALID_ARG);
    check("start slot too small",
          dc_syslog_start(&(dc_syslog_config_t){ .server = "x", .slot_size = 10 }) ==
          ESP_ERR_INVALID_ARG);
    check("start slot too big",
          dc_syslog_start(&(dc_syslog_config_t){ .server = "x", .slot_size = 2000 }) ==
          ESP_ERR_INVALID_ARG);
    char longhost[100];
    memset(longhost, 'h', sizeof longhost - 1);
    longhost[sizeof longhost - 1] = '\0';
    check("start hostname too long",
          dc_syslog_start(&(dc_syslog_config_t){ .server = "x", .hostname = longhost }) ==
          ESP_ERR_INVALID_ARG);

    dc_syslog_config_t cfg = {
        .server = "syslog.lan",
        .app_name = "dragonbreath",
        .hostname = "dragonbreath",
        .queue_depth = 4,
        .slot_size = 100,   // not a multiple of 8: slot alignment test
        .timestamp = timestamp_cb,
    };
    check("start", dc_syslog_start(&cfg) == ESP_OK);
    check("start again", dc_syslog_start(&cfg) == ESP_ERR_INVALID_STATE);
    check("hook installed", g_installed != prev_hook && g_task_fn != NULL);

    // Six lines into four slots: two drops. Each line goes to the previous hook.
    for (int i = 1; i <= 6; i++)
        app_log("\x1b[0;32mI (%d) app: line %d\x1b[0m\n", 5000, i);
    s = stats();
    check("hook chains each line", g_prev_calls == 6 &&
          strcmp(g_prev_last, "\x1b[0;32mI (5000) app: line 6\x1b[0m\n") == 0);
    check("queue full drops 2", s.dropped == 2 && s.queued == 4 && s.sent == 0);

    run_sender();
    s = stats();
    check("sender sends 4", g_sent_count == 4 && s.sent == 4 && s.queued == 0);
    check("first datagram",
          strcmp(g_sent[0], "<14>1 - dragonbreath dragonbreath app - "
                            "[dc@32473 seq=\"1\" up=\"5000\"] line 1") == 0);
    check("fourth datagram seq 4", strstr(g_sent[3], "seq=\"4\"") != NULL);
    check("destination 192.0.2.10:514",
          g_sent_to.sin_port == htons(514) &&
          g_sent_to.sin_addr.s_addr == inet_addr("192.0.2.10"));
    check("one DNS lookup", g_dns_calls == 1);

    // The next lines show the gap: seq 5 and 6 were dropped.
    g_ts_ok = true;
    g_now_us = 7000000;
    app_log("W (%d) dc_wifi: weak signal %d dBm\n", 7000, -80);
    run_sender();
    check("gap after drops: seq 7",
          g_sent_count == 5 &&
          strcmp(g_sent[4], "<12>1 2026-11-14T23:40:11.123Z dragonbreath dragonbreath dc_wifi - "
                            "[dc@32473 seq=\"7\" up=\"7000\"] weak signal -80 dBm") == 0);

    // Lines from an ISR or from the sender task are not queued, but chained.
    int prev_before = g_prev_calls;
    g_in_isr = true;
    app_log("I (1) isr: x\n");
    g_in_isr = false;
    g_current = &g_sender_task;
    app_log("I (1) self: x\n");
    g_current = &g_app_task;
    s = stats();
    check("ISR and sender lines skipped", s.queued == 0 && g_prev_calls == prev_before + 2);

    // Network down: the line stays in the queue. No send error.
    g_net_up = false;
    app_log("E (1) app: while down\n");
    run_sender();
    s = stats();
    check("network down keeps the line", s.queued == 1 && s.send_errors == 0 &&
          g_sent_count == 5);

    // DNS error: counted, line kept.
    g_net_up = true;
    g_dns_fail = true;
    run_sender();
    s = stats();
    check("DNS error counted, line kept", s.queued == 1 && s.send_errors >= 1 &&
          g_sent_count == 5);
    g_dns_fail = false;

    // Send error: counted, line kept, then sent.
    uint32_t errors = s.send_errors;
    g_send_fail = true;
    run_sender();
    s = stats();
    check("send error counted, line kept", s.queued == 1 && s.send_errors > errors);
    g_send_fail = false;
    run_sender();
    s = stats();
    check("line sent after recovery", s.queued == 0 && g_sent_count == 6 &&
          strstr(g_sent[5], "<11>1 ") == g_sent[5] && strstr(g_sent[5], "] while down"));

    // A long line is cut in the slot and marked.
    char big[300];
    memset(big, 'x', sizeof big - 1);
    big[sizeof big - 1] = '\0';
    app_log("I (1) app: %s\n", big);
    run_sender();
    check("long line truncated and marked",
          g_sent_count == 7 && strstr(g_sent[6], "trunc=\"1\"] xxx") != NULL &&
          strlen(strstr(g_sent[6], "] ") + 2) == 100 - 1 - strlen("I (1) app: "));

    s = stats();
    check("stack high water mark", s.stack_free_min == 1234);

    printf(fails ? "\n%d FAILED\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
