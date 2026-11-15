// Host test for the dc_evlog line assembly of ESP-IDF Log V2 pieces.
// Build with -DDC_EVLOG_LINE_ASSEMBLY=1 (see run_evlog_line_host_test.sh).
#include "dc_evlog.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static int s_failures;
static vprintf_like_t s_log_hook;
static TaskHandle_t s_task;
static int s_in_isr;
static char s_snap[DC_EVLOG_CONSOLE_BYTES + 1];

static int discard_vprintf(const char *fmt, va_list ap)
{
    (void)fmt;
    (void)ap;
    return 0;
}

vprintf_like_t esp_log_set_vprintf(vprintf_like_t func)
{
    vprintf_like_t previous = s_log_hook ? s_log_hook : discard_vprintf;
    s_log_hook = func;
    return previous;
}

TaskHandle_t xTaskGetCurrentTaskHandle(void) { return s_task; }
int xPortInIsrContext(void) { return s_in_isr; }

static int task_a, task_b, task_c, task_d, task_e;

// One call of the hook, as the log component does it, from the task `t`.
static void piece(void *t, const char *fmt, ...)
{
    s_task = t;
    va_list ap;
    va_start(ap, fmt);
    (void)s_log_hook(fmt, ap);
    va_end(ap);
}

static const char *ring(void)
{
    (void)dc_evlog_console_snapshot(s_snap, sizeof s_snap);
    return s_snap;
}

static void expect_ring(const char *name, const char *want)
{
    bool ok = strcmp(ring(), want) == 0;
    printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) {
        printf("  want: \"%s\"\n  got:  \"%s\"\n", want, s_snap);
        s_failures++;
    }
}

int main(void)
{
    dc_evlog_console_init();

    // Log V2 pieces of two tasks mix: each line must stay whole.
    piece(&task_a, "%s%c %s%s%s%s%s", "", 'I', "(", "1", ") ", "a", ": ");
    piece(&task_b, "%s%c %s%s%s%s%s", "", 'W', "(", "2", ") ", "b", ": ");
    piece(&task_b, "b says %d", 2);
    piece(&task_a, "a says %d", 1);
    piece(&task_b, "%s", "\n");
    expect_ring("finished line of task B goes first", "W (2) b: b says 2\n");
    piece(&task_a, "%s", "\n");
    expect_ring("line of task A stays whole",
                "W (2) b: b says 2\nI (1) a: a says 1\n");

    // A Log V1 line (one call with the newline) goes in at once.
    piece(&task_c, "I (3) c: one call\n");
    expect_ring("full line goes in at once",
                "W (2) b: b says 2\nI (1) a: a says 1\nI (3) c: one call\n");

    // An ISR piece does not wait for a newline.
    s_in_isr = 1;
    piece(&task_a, "isr");
    s_in_isr = 0;
    expect_ring("ISR piece goes in at once",
                "W (2) b: b says 2\nI (1) a: a says 1\nI (3) c: one call\nisr");

    // If more tasks than slots have open lines, the oldest slot is flushed.
    piece(&task_a, "A");
    piece(&task_b, "B");
    piece(&task_c, "C");
    piece(&task_d, "D");
    piece(&task_e, "E");   // no free slot: task A's "A" goes into the ring
    expect_ring("busy slots flush instead of losing bytes",
                "W (2) b: b says 2\nI (1) a: a says 1\nI (3) c: one call\nisrA");
    piece(&task_b, "\n");
    piece(&task_c, "\n");
    piece(&task_d, "\n");
    piece(&task_e, "\n");
    expect_ring("every open line ends",
                "W (2) b: b says 2\nI (1) a: a says 1\nI (3) c: one call\nisrA"
                "B\nC\nD\nE\n");

    if (s_failures != 0)
        fprintf(stderr, "%d evlog line test(s) failed\n", s_failures);
    return s_failures == 0 ? 0 : 1;
}
