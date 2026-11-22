#include "dc_evlog.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static SemaphoreHandle_t s_lock = NULL;
static dc_evlog_entry_t  s_buf[DC_EVLOG_MAX_ENTRIES];
static size_t            s_head = 0;   // next write slot
static size_t            s_count = 0;  // number of valid entries (<= MAX)

// ---- firmware console capture (raw ESP_LOGx byte ring) ----
// Independent of the curated event ring above. A spinlock (not a mutex) guards it
// because the esp_log vprintf hook can be reached from many contexts and must never
// block or assert. Appends and bounded snapshot reads use short critical sections;
// no console lock is ever held across caller or network I/O. We NEVER log from
// inside the hook.
#define CON_LINE  200
static portMUX_TYPE     s_con_mux = portMUX_INITIALIZER_UNLOCKED;
static char             s_con[DC_EVLOG_CONSOLE_BYTES];
static size_t           s_con_head = 0;    // next write index
static bool             s_con_full = false;
static uint64_t         s_con_write_seq = 0; // total bytes appended; guarded by s_con_mux
static vprintf_like_t   s_prev_vprintf = NULL;
static bool             s_con_on = false;

// Marker appended in place of the tail of a line vsnprintf had to truncate,
// so the captured ring never loses its line terminator (which would glue the
// next captured line onto this one) and a reader can see truncation happened.
// Always ends in '\n'; kept well under CON_LINE so it always fits alongside
// at least some of the real content.
static const char TRUNCATION_MARKER[] = "...[truncated]\n";
#define TRUNCATION_MARKER_LEN (sizeof(TRUNCATION_MARKER) - 1)
_Static_assert(TRUNCATION_MARKER_LEN < CON_LINE - 1,
               "truncation marker must fit within CON_LINE alongside content");

// Caller holds s_con_mux.
static void con_append_locked(const char *p, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        s_con[s_con_head++] = p[i];
        if (s_con_head >= DC_EVLOG_CONSOLE_BYTES) { s_con_head = 0; s_con_full = true; }
    }
    s_con_write_seq += (uint64_t)n;
}

// ESP-IDF Log V1 (the default in ESP-IDF 5 and 6) calls the hook once for each
// line. Log V2 (an option in ESP-IDF 5.5 and later) calls it three times: for
// the "I (123) tag: " prefix, for the message and for the newline. With
// Picolibc (the ESP-IDF 6 default), Log V2 does not lock between the three
// calls, so the pieces of two tasks can mix. For Log V2, keep the pieces of
// each task until its newline, then append the full line.
#ifndef DC_EVLOG_LINE_ASSEMBLY
#if defined(CONFIG_LOG_VERSION_2) && CONFIG_LOG_VERSION_2
#define DC_EVLOG_LINE_ASSEMBLY 1
#else
#define DC_EVLOG_LINE_ASSEMBLY 0
#endif
#endif

#if DC_EVLOG_LINE_ASSEMBLY
#define CON_PENDING_SLOTS 4
typedef struct {
    TaskHandle_t owner;     // NULL: the slot is free
    size_t       len;
    char         buf[CON_LINE];
} con_pending_t;
static con_pending_t s_con_pending[CON_PENDING_SLOTS];
static size_t        s_con_evict = 0;   // next slot to flush if all are busy

// Caller holds s_con_mux.
static void con_flush_locked(con_pending_t *slot)
{
    con_append_locked(slot->buf, slot->len);
    slot->len = 0;
    slot->owner = NULL;
}

// `owner` is NULL in an ISR: then the piece goes directly into the ring.
// On ESP32-C3 with ESP-IDF 6.1, this case does not occur by default: Log V2
// prints from an ISR with esp_rom_vprintf and does not call the hook
// (CONFIG_LOG_API_CONSTRAINED_ENV_SAFE). The branch is a safe fallback.
static void con_append_piece(TaskHandle_t owner, const char *p, size_t n)
{
    if (n == 0) return;
    bool ends_line = p[n - 1] == '\n';
    portENTER_CRITICAL(&s_con_mux);
    con_pending_t *slot = NULL;
    con_pending_t *free_slot = NULL;
    for (size_t i = 0; owner != NULL && i < CON_PENDING_SLOTS; i++) {
        if (s_con_pending[i].owner == owner) { slot = &s_con_pending[i]; break; }
        if (free_slot == NULL && s_con_pending[i].owner == NULL) free_slot = &s_con_pending[i];
    }
    if (slot == NULL && (owner == NULL || ends_line)) {
        con_append_locked(p, n);           // a full line: nothing to keep
        portEXIT_CRITICAL(&s_con_mux);
        return;
    }
    if (slot == NULL) {
        slot = free_slot;
        if (slot == NULL) {                // all slots busy: flush one, then use it
            slot = &s_con_pending[s_con_evict];
            s_con_evict = (s_con_evict + 1) % CON_PENDING_SLOTS;
            con_flush_locked(slot);
        }
        slot->owner = owner;
    }
    while (n > 0) {
        size_t room = sizeof slot->buf - slot->len;
        size_t take = n < room ? n : room;
        memcpy(slot->buf + slot->len, p, take);
        slot->len += take;
        p += take;
        n -= take;
        if (slot->len == sizeof slot->buf) {   // line longer than the slot
            con_append_locked(slot->buf, slot->len);
            slot->len = 0;
        }
    }
    if (ends_line) con_flush_locked(slot);
    portEXIT_CRITICAL(&s_con_mux);
}
#endif

static void con_append(const char *p, int n)
{
    if (n <= 0) return;
#if DC_EVLOG_LINE_ASSEMBLY
    con_append_piece(xPortInIsrContext() ? NULL : xTaskGetCurrentTaskHandle(), p, (size_t)n);
#else
    portENTER_CRITICAL(&s_con_mux);
    con_append_locked(p, (size_t)n);
    portEXIT_CRITICAL(&s_con_mux);
#endif
}

// esp_log hook: tee the formatted line into the ring, then forward to the original
// writer (UART) via a va_copy so the serial console is unchanged.
//
// vsnprintf's return value is the length the formatted line WOULD occupy,
// regardless of buffer size, so a return >= sizeof(line) means the line was
// too long for the capture buffer (the untouched fmt/ap2 pair still reaches
// the real writer below with the full line, so UART is never affected).
// A previous version captured the raw truncated bytes as-is: since vsnprintf
// truncates by dropping the tail, whatever newline the original line ended
// with was also dropped, so the next captured line ran on directly after the
// truncated one with no separator. Reserve room for a marker that always
// re-terminates the captured entry so ring framing can never be lost this
// way; the discarded tail (beyond CON_LINE) was never captured either way.
static int con_vprintf(const char *fmt, va_list ap)
{
    char line[CON_LINE];
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(line, sizeof line, fmt, ap);
    if (n < (int)sizeof line) {
        con_append(line, n);
    } else {
        // Compose the bounded replacement before taking the ring lock.
        // A single append keeps the marker adjacent to this log entry even
        // when other tasks log concurrently, and advances write_seq once.
        const size_t prefix_len = sizeof line - 1 - TRUNCATION_MARKER_LEN;
        memcpy(line + prefix_len, TRUNCATION_MARKER, TRUNCATION_MARKER_LEN);
        con_append(line, (int)sizeof line - 1);
    }
    int r = s_prev_vprintf ? s_prev_vprintf(fmt, ap2) : 0;
    va_end(ap2);
    return r;
}

void dc_evlog_init(void)
{
    if (s_lock != NULL) return;
    s_lock = xSemaphoreCreateMutex();
    // If mutex creation fails we just stay in the "not initialised" state and
    // dc_evlog_add becomes a no-op — good enough for a diagnostic aid.
}

void dc_evlog_add(const char *fmt, ...)
{
    if (s_lock == NULL) return;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) != pdTRUE) return;

    dc_evlog_entry_t *e = &s_buf[s_head];
    e->ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(e->text, sizeof(e->text), fmt, ap);
    va_end(ap);

    s_head = (s_head + 1) % DC_EVLOG_MAX_ENTRIES;
    if (s_count < DC_EVLOG_MAX_ENTRIES) s_count++;

    xSemaphoreGive(s_lock);
}

size_t dc_evlog_snapshot(dc_evlog_entry_t *out, size_t max)
{
    if (out == NULL || max == 0) return 0;
    if (s_lock == NULL) return 0;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) != pdTRUE) return 0;

    size_t want = (max < s_count) ? max : s_count;
    // Copy newest-first: walk backward from s_head.
    size_t idx = (s_head == 0) ? (DC_EVLOG_MAX_ENTRIES - 1) : (s_head - 1);
    for (size_t i = 0; i < want; ++i) {
        out[i] = s_buf[idx];
        idx = (idx == 0) ? (DC_EVLOG_MAX_ENTRIES - 1) : (idx - 1);
    }

    xSemaphoreGive(s_lock);
    return want;
}

void dc_evlog_console_init(void)
{
    if (s_con_on) return;
    s_con_on = true;
    s_prev_vprintf = esp_log_set_vprintf(con_vprintf);   // returns the UART writer
}

size_t dc_evlog_console_snapshot(char *out, size_t max)
{
    if (out == NULL || max == 0) return 0;
    size_t cap = max - 1;   // leave room for the NUL
    size_t len = 0;

    portENTER_CRITICAL(&s_con_mux);
    if (s_con_full) {
        // Ring wrapped: oldest byte is at s_con_head. Emit [head..end) then [0..head).
        size_t tail = DC_EVLOG_CONSOLE_BYTES - s_con_head;
        size_t a = tail < cap ? tail : cap;
        memcpy(out, s_con + s_con_head, a);
        len = a;
        size_t b = s_con_head < (cap - len) ? s_con_head : (cap - len);
        memcpy(out + len, s_con, b);
        len += b;
    } else {
        size_t a = s_con_head < cap ? s_con_head : cap;
        memcpy(out, s_con, a);
        len = a;
    }
    portEXIT_CRITICAL(&s_con_mux);

    out[len] = '\0';
    return len;
}

static size_t con_copy_view_locked(const dc_evlog_console_view_t *view,
                                   size_t offset, char *out, size_t max)
{
    if (offset >= view->len || max == 0) return 0;
    size_t want = view->len - offset;
    if (want > max) want = max;

    size_t idx = (view->start + offset) % DC_EVLOG_CONSOLE_BYTES;
    size_t first = DC_EVLOG_CONSOLE_BYTES - idx;
    if (first > want) first = want;
    memcpy(out, s_con + idx, first);
    if (first < want) memcpy(out + first, s_con, want - first);
    return want;
}

size_t dc_evlog_console_snapshot_begin(dc_evlog_console_view_t *view,
                                       char *out, size_t max)
{
    if (view == NULL || out == NULL || max == 0) return 0;

    portENTER_CRITICAL(&s_con_mux);
    view->start = s_con_full ? s_con_head : 0;
    view->len = s_con_full ? DC_EVLOG_CONSOLE_BYTES : s_con_head;
    view->write_seq = s_con_write_seq;
    size_t written = con_copy_view_locked(view, 0, out, max);
    portEXIT_CRITICAL(&s_con_mux);
    return written;
}

bool dc_evlog_console_snapshot_read(const dc_evlog_console_view_t *view,
                                    size_t offset, char *out, size_t max,
                                    size_t *written)
{
    if (written == NULL) return false;
    *written = 0;
    if (view == NULL || out == NULL || max == 0 || offset > view->len) return false;
    if (offset == view->len) return true;

    portENTER_CRITICAL(&s_con_mux);
    uint64_t advanced = s_con_write_seq - view->write_seq;
    // Before the captured ring was full, new bytes first consume the free tail;
    // after that they overwrite the captured snapshot oldest-first. A chunk at
    // `offset` remains coherent iff producer progress has not reached it yet.
    uint64_t overwrite_budget = (uint64_t)(DC_EVLOG_CONSOLE_BYTES - view->len) + offset;
    if (advanced > overwrite_budget) {
        portEXIT_CRITICAL(&s_con_mux);
        return false;
    }
    *written = con_copy_view_locked(view, offset, out, max);
    portEXIT_CRITICAL(&s_con_mux);
    return true;
}
