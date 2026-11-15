// Host test for the request deadline in dc_portal_deadline.h.
#include "dc_portal_deadline.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            exit(1);                                                      \
        }                                                                 \
    } while (0)

#define S 1000000LL
static const dc_portal_deadline_limits_t LIM = DC_PORTAL_REQUEST_LIMITS;

// A client that sends one byte for each 4.9 s. The receive function waits
// for the byte only for the time that is left (see deadline_recv()). Return
// the time from the first receive to the close.
static int64_t drip(dc_portal_deadline_t *d, int64_t start, int64_t step_us, int64_t max_us)
{
    int64_t now = start;
    for (;;) {
        int64_t left = dc_portal_deadline_left(d, now, LIM);
        if (left <= 0) return now - start;
        if (left < step_us) return now + left - start;   // select() times out
        now += step_us;
        dc_portal_deadline_on_recv(d, 1);
        CHECK(now - start < max_us);
    }
}

int main(void)
{
    // A fast request: the first receive starts the clock with the full time.
    {
        dc_portal_deadline_t d = {0};
        CHECK(dc_portal_deadline_left(&d, 100 * S, LIM) == LIM.request_us);
        dc_portal_deadline_on_recv(&d, 300);
        CHECK(dc_portal_deadline_left(&d, 101 * S, LIM) == LIM.request_us - 1 * S);
    }

    // A header drip (1 byte for each 4.9 s) stops at 20 s, not after 2.8 h.
    {
        dc_portal_deadline_t d = {0};
        int64_t held = drip(&d, 5 * S, 4900000, 3600 * S);
        CHECK(held == LIM.request_us);
    }

    // A keep-alive socket: a send ends the request. The next receive starts a
    // new request with the full time, after any idle gap.
    {
        dc_portal_deadline_t d = {0};
        dc_portal_deadline_left(&d, 0, LIM);
        dc_portal_deadline_on_recv(&d, 200);
        CHECK(dc_portal_deadline_left(&d, 19 * S, LIM) == 1 * S);
        dc_portal_deadline_on_send(&d);
        CHECK(dc_portal_deadline_left(&d, 600 * S, LIM) == LIM.request_us);
        CHECK(d.bytes == 0 && !d.sent);
        dc_portal_deadline_on_send(&d);
        // Many requests over a long time on one socket: none is cut.
        for (int i = 0; i < 100; i++) {
            int64_t t = 1000 * S + i * 15 * S;
            CHECK(dc_portal_deadline_left(&d, t, LIM) == LIM.request_us);
            dc_portal_deadline_on_recv(&d, 500);
            CHECK(dc_portal_deadline_left(&d, t + 10 * S, LIM) == 10 * S);
            dc_portal_deadline_on_send(&d);
        }
    }

    // Without a send, the time keeps counting: a slow client cannot restart
    // the clock with more data.
    {
        dc_portal_deadline_t d = {0};
        dc_portal_deadline_left(&d, 0, LIM);
        for (int i = 1; i <= 19; i++) dc_portal_deadline_on_recv(&d, 4096);
        CHECK(dc_portal_deadline_left(&d, 21 * S, LIM) < 0);
    }

    // The purge of an unread body after a response gets its own 20 s, at most.
    {
        dc_portal_deadline_t d = {0};
        dc_portal_deadline_left(&d, 0, LIM);
        dc_portal_deadline_on_recv(&d, 100);
        dc_portal_deadline_on_send(&d);
        int64_t held = drip(&d, 3 * S, 4900000, 3600 * S);
        CHECK(held == LIM.request_us);
    }

    // OTA in progress mode: a steady upload of 1.5 MB at 50 KB/s passes.
    {
        dc_portal_deadline_t d = {0};
        int64_t now = 0;
        dc_portal_deadline_left(&d, now, LIM);
        dc_portal_deadline_allow_progress(&d);
        for (int i = 0; i < 1536; i++) {
            CHECK(dc_portal_deadline_left(&d, now, LIM) > 0);
            now += 20000;                       // 1 KB each 20 ms
            dc_portal_deadline_on_recv(&d, 1024);
        }
        CHECK(dc_portal_deadline_left(&d, now, LIM) > 1000 * S);
    }

    // OTA in progress mode with a drip: about 20 s, not 1.9 MB * 4.9 s.
    {
        dc_portal_deadline_t d = {0};
        dc_portal_deadline_left(&d, 0, LIM);
        dc_portal_deadline_allow_progress(&d);
        int64_t held = drip(&d, 0, 4900000, 3600 * S);
        CHECK(held >= LIM.request_us && held < LIM.request_us + 1 * S);
    }

    // Progress mode ends with the request.
    {
        dc_portal_deadline_t d = {0};
        dc_portal_deadline_left(&d, 0, LIM);
        dc_portal_deadline_allow_progress(&d);
        dc_portal_deadline_on_recv(&d, 1024 * 1024);
        dc_portal_deadline_on_send(&d);
        CHECK(dc_portal_deadline_left(&d, 5 * S, LIM) == LIM.request_us);
        CHECK(!d.progress);
    }

    // The byte count saturates. Errors and zero do not count.
    {
        dc_portal_deadline_t d = {0};
        dc_portal_deadline_left(&d, 0, LIM);
        dc_portal_deadline_on_recv(&d, -3);
        dc_portal_deadline_on_recv(&d, 0);
        CHECK(d.bytes == 0);
        d.bytes = UINT32_MAX - 10;
        dc_portal_deadline_on_recv(&d, 100);
        CHECK(d.bytes == UINT32_MAX);
        dc_portal_deadline_allow_progress(&d);
        CHECK(dc_portal_deadline_left(&d, 0, LIM) > 0);
    }

    printf("dc_portal deadline host tests: PASS\n");
    return 0;
}
