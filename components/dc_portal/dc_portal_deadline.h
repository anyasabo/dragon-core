// SPDX-License-Identifier: MIT
#pragma once
// Time limit for one HTTP request on one socket.
//
// The httpd task serves one request at a time. recv_wait_timeout (5 s) limits
// only the wait for each part of a request. A client that sends one byte for
// each 4.9 s never causes a timeout. A 2 KB header then holds the task for
// about 2.8 h. dc_portal installs a receive function on each socket (see
// dc_portal.c). The receive function asks this clock how much time the
// request has left. If no time is left, it closes the socket.
//
// Request boundary: a request starts at the first receive on the socket, and
// at the first receive after the server sent data on the socket. httpd parses
// a request and runs its handler before it sends the response. Thus a receive
// after a send is the next request, or the purge of an unread body. (A handler
// that sends before it reads the body also starts a new deadline.) A
// keep-alive socket that waits for its next request has no receive in
// progress, so its idle time does not count. An event stream (SSE) only sends,
// so the clock never stops it.
//
// This header has no ESP-IDF dependency, so a host test can drive it.

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    // Most time for one request, from its first receive, in microseconds.
    int64_t request_us;
    // In progress mode, each this many received bytes add 1 s to the time.
    int32_t progress_bytes_per_s;
} dc_portal_deadline_limits_t;

// 20 s for the headers and the body of a normal request. An OTA upload uses
// progress mode: 20 s plus 1 s for each 1 KB that arrived. A client that sends
// one byte for each 4.9 s then holds the httpd task for 20 s at most.
#define DC_PORTAL_REQUEST_LIMITS \
    ((dc_portal_deadline_limits_t){ .request_us = 20 * 1000000LL, .progress_bytes_per_s = 1024 })

typedef struct {
    int64_t start_us;   // time of the first receive of this request
    uint32_t bytes;     // bytes that this request received
    bool active;        // a request started on this socket
    bool sent;          // the server sent data after the request started
    bool progress;      // the received bytes add time (an OTA upload)
} dc_portal_deadline_t;

// Call for each send on the socket. The next receive starts a new request.
static inline void dc_portal_deadline_on_send(dc_portal_deadline_t *d)
{
    d->sent = true;
}

// Call before each receive. Return the time that the request has left, in
// microseconds. If the value is 0 or less, the request is too slow: close the
// socket.
static inline int64_t dc_portal_deadline_left(dc_portal_deadline_t *d, int64_t now_us,
                                              dc_portal_deadline_limits_t lim)
{
    if (!d->active || d->sent) {
        *d = (dc_portal_deadline_t){ .start_us = now_us, .active = true };
    }
    int64_t end_us = d->start_us + lim.request_us;
    if (d->progress && lim.progress_bytes_per_s > 0)
        end_us += (int64_t)d->bytes * 1000000 / lim.progress_bytes_per_s;
    return end_us - now_us;
}

// Call after each receive with its result.
static inline void dc_portal_deadline_on_recv(dc_portal_deadline_t *d, int received)
{
    if (received <= 0) return;
    uint32_t n = (uint32_t)received;
    d->bytes = d->bytes > UINT32_MAX - n ? UINT32_MAX : d->bytes + n;
}

// Use progress mode for the rest of this request. Only a handler that reads a
// large body from an authorized client (OTA) does this.
static inline void dc_portal_deadline_allow_progress(dc_portal_deadline_t *d)
{
    d->progress = true;
}
