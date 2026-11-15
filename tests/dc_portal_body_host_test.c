// Host test for the bounded body read in dc_portal_body.h.
#include "dc_portal_body.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            exit(1);                                                      \
        }                                                                 \
    } while (0)

// A scripted client. Each step is a byte count (> 0), a timeout, or a close.
// Each call advances the fake clock by the step's duration.
enum { STEP_TIMEOUT = -1000, STEP_CLOSE = -1001 };
typedef struct {
    const int *steps;
    int n;
    int i;
    const char *data;
    size_t sent;
    int calls;
} client_t;

static int64_t s_now;
static int64_t fake_now(void) { return s_now; }

static int fake_recv(void *ctx, char *buf, size_t len)
{
    client_t *c = ctx;
    c->calls++;
    if (c->i >= c->n) { s_now += 5000000; return DC_PORTAL_BODY_RECV_TIMEOUT; }
    int step = c->steps[c->i++];
    if (step == STEP_TIMEOUT) { s_now += 5000000; return DC_PORTAL_BODY_RECV_TIMEOUT; }
    if (step == STEP_CLOSE) return 0;
    size_t k = (size_t)step < len ? (size_t)step : len;
    memcpy(buf, c->data + c->sent, k);
    c->sent += k;
    s_now += 1000;
    return (int)k;
}

static bool run(const int *steps, int n, const char *data, size_t len, char *out, client_t *c)
{
    *c = (client_t){ .steps = steps, .n = n, .data = data };
    s_now = 0;
    return dc_portal_read_body(fake_recv, fake_now, c, out, len, DC_PORTAL_JSON_BODY_LIMITS);
}

int main(void)
{
    const char *body = "{\"ssid\":\"lab\",\"password\":\"secret\"}";
    size_t len = strlen(body);
    char out[128];
    client_t c;

    // The whole body in one part.
    { int s[] = { (int)len }; memset(out, 0, sizeof out);
      CHECK(run(s, 1, body, len, out, &c)); CHECK(memcmp(out, body, len) == 0); }

    // The body in parts, with a timeout between them, inside the deadline.
    { int s[] = { 5, STEP_TIMEOUT, 7, (int)len - 12 };
      memset(out, 0, sizeof out);
      CHECK(run(s, 4, body, len, out, &c)); CHECK(memcmp(out, body, len) == 0); }

    // Data resets the idle count: two timeouts, data, two timeouts, data.
    // With no deadline this passes, because the count never reaches 3.
    { int s[] = { 5, STEP_TIMEOUT, STEP_TIMEOUT, 7, STEP_TIMEOUT, STEP_TIMEOUT, (int)len - 12 };
      client_t d = { .steps = s, .n = 7, .data = body };
      dc_portal_body_limits_t no_deadline = { .idle_timeouts_max = 3, .deadline_us = 0 };
      memset(out, 0, sizeof out); s_now = 0;
      CHECK(dc_portal_read_body(fake_recv, fake_now, &d, out, len, no_deadline));
      CHECK(memcmp(out, body, len) == 0); }

    // The same client with the JSON limits: the deadline (10 s) stops it.
    { int s[] = { 5, STEP_TIMEOUT, STEP_TIMEOUT, 7, STEP_TIMEOUT, STEP_TIMEOUT, (int)len - 12 };
      CHECK(!run(s, 7, body, len, out, &c)); }

    // A client that stalls after a part: three timeouts in a row stop the read.
    { int s[] = { 5 }; CHECK(!run(s, 1, body, len, out, &c)); CHECK(c.calls >= 3 && c.calls <= 4); }

    // A client that closes the socket early.
    { int s[] = { 5, STEP_CLOSE }; CHECK(!run(s, 2, body, len, out, &c)); }

    // A slow client: one byte, then two timeouts, again and again. The idle
    // count never reaches 3, so only the deadline stops it.
    {
        int s[3 * 64];
        for (int i = 0; i < 64; i++) { s[3 * i] = 1; s[3 * i + 1] = STEP_TIMEOUT; s[3 * i + 2] = STEP_TIMEOUT; }
        CHECK(!run(s, 3 * 64, body, len, out, &c));
        // 10 s deadline: the read stops near it, not after len * 10 s.
        CHECK(s_now <= DC_PORTAL_JSON_BODY_LIMITS.deadline_us + 5000000);
        CHECK(c.sent < len);
    }

    // No clock: the deadline is off, the idle limit still works.
    { int s[] = { 5 }; client_t d = { .steps = s, .n = 1, .data = body };
      CHECK(!dc_portal_read_body(fake_recv, NULL, &d, out, len, DC_PORTAL_JSON_BODY_LIMITS)); }

    // A zero-length read returns true at once and calls nothing.
    { int s[] = { 1 }; CHECK(run(s, 1, body, 0, out, &c)); CHECK(c.calls == 0); }

    printf("dc_portal body host tests: PASS\n");
    return 0;
}
