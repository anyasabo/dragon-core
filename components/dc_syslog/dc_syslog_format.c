#include "dc_syslog_format.h"

#include <string.h>

#define ESC '\x1b'

int dc_syslog_severity(char level)
{
    switch (level) {
    case 'E': return DC_SYSLOG_SEV_ERR;
    case 'W': return DC_SYSLOG_SEV_WARNING;
    case 'I': return DC_SYSLOG_SEV_INFO;
    case 'D':
    case 'V': return DC_SYSLOG_SEV_DEBUG;
    default:  return -1;
    }
}

size_t dc_syslog_strip(char *s, size_t len)
{
    size_t w = 0;
    for (size_t r = 0; r < len; r++) {
        if (s[r] != ESC) {
            s[w++] = s[r];
            continue;
        }
        // CSI: ESC '[' parameter bytes 0x20-0x3F, final byte 0x40-0x7E.
        if (r + 1 < len && s[r + 1] == '[') {
            r += 2;
            while (r < len && (unsigned char)s[r] >= 0x20 && (unsigned char)s[r] <= 0x3F) r++;
            // r is now at the final byte (skipped by the loop) or at the end.
            if (r < len && ((unsigned char)s[r] < 0x40 || (unsigned char)s[r] > 0x7E)) r--;
        }
        // Other escapes: drop the ESC only.
    }
    while (w > 0 && (s[w - 1] == '\n' || s[w - 1] == '\r')) w--;
    return w;
}

void dc_syslog_parse_line(const char *line, size_t len, dc_syslog_line_t *out)
{
    out->severity = DC_SYSLOG_SEV_INFO;
    out->tag = NULL;
    out->tag_len = 0;
    out->msg = line;
    out->msg_len = len;

    // "L (" + at least one character + ") " + tag + ":"
    if (len < 7 || line[1] != ' ' || line[2] != '(') return;
    int sev = dc_syslog_severity(line[0]);
    if (sev < 0) return;

    // The log timestamp is digits ("12345") or a time ("12:34:56.789").
    size_t p = 3;
    while (p < len && p < 3 + 24 && line[p] != ')') {
        char c = line[p];
        if (!((c >= '0' && c <= '9') || c == ':' || c == '.')) return;
        p++;
    }
    if (p == 3 || p + 1 >= len || line[p] != ')' || line[p + 1] != ' ') return;
    p += 2;

    // The tag ends at the first ':'. ESP-IDF writes "tag: msg". The Wi-Fi
    // libraries write "wifi:msg" without the space.
    size_t tag = p;
    while (p < len && line[p] != ':') p++;
    if (p >= len || p == tag) return;
    size_t tag_len = p - tag;
    p++;
    if (p < len && line[p] == ' ') p++;

    out->severity = sev;
    out->tag = line + tag;
    out->tag_len = tag_len;
    out->msg = line + p;
    out->msg_len = len - p;
}

// Appenders. Each one writes only if all of the text fits (pos + n < sz),
// so the caller can check the result once at the end.
typedef struct {
    char *buf;
    size_t sz;
    size_t pos;
    bool overflow;
} out_t;

static void put(out_t *o, const char *s, size_t n)
{
    if (o->overflow || o->pos + n >= o->sz) {
        o->overflow = true;
        return;
    }
    memcpy(o->buf + o->pos, s, n);
    o->pos += n;
}

static void put_str(out_t *o, const char *s)
{
    put(o, s, strlen(s));
}

static void put_u64(out_t *o, uint64_t v)
{
    char tmp[20];
    size_t n = 0;
    do {
        tmp[sizeof tmp - 1 - n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v);
    put(o, tmp + sizeof tmp - n, n);
}

// RFC 5424 header fields allow only PRINTUSASCII (33-126). "-" is the NILVALUE.
static void put_field(out_t *o, const char *s, size_t n, size_t max)
{
    if (s == NULL || n == 0) {
        put(o, "-", 1);
        return;
    }
    if (n > max) n = max;
    if (o->overflow || o->pos + n >= o->sz) {
        o->overflow = true;
        return;
    }
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        o->buf[o->pos++] = (c >= 33 && c <= 126) ? (char)c : '_';
    }
}

static size_t cstr_len(const char *s, size_t max)
{
    if (s == NULL) return 0;
    size_t n = 0;
    while (n < max && s[n]) n++;
    return n;
}

size_t dc_syslog_format(char *out, size_t out_sz, const dc_syslog_meta_t *meta,
                        const dc_syslog_line_t *line)
{
    if (out == NULL || out_sz == 0) return 0;
    out[0] = '\0';
    if (meta == NULL || line == NULL) return 0;

    out_t o = { .buf = out, .sz = out_sz };
    int sev = line->severity;
    if (sev < 0 || sev > 7) sev = DC_SYSLOG_SEV_INFO;

    put(&o, "<", 1);
    put_u64(&o, (uint64_t)(DC_SYSLOG_FACILITY * 8 + sev));
    put(&o, ">1 ", 3);
    put_field(&o, meta->timestamp, cstr_len(meta->timestamp, DC_SYSLOG_TIMESTAMP_MAX),
              DC_SYSLOG_TIMESTAMP_MAX);
    put(&o, " ", 1);
    put_field(&o, meta->hostname, cstr_len(meta->hostname, DC_SYSLOG_HOSTNAME_MAX),
              DC_SYSLOG_HOSTNAME_MAX);
    put(&o, " ", 1);
    put_field(&o, meta->app_name, cstr_len(meta->app_name, DC_SYSLOG_APPNAME_MAX),
              DC_SYSLOG_APPNAME_MAX);
    put(&o, " ", 1);
    put_field(&o, line->tag, line->tag_len, DC_SYSLOG_PROCID_MAX);
    put_str(&o, " - [" DC_SYSLOG_SD_ID " seq=\"");
    put_u64(&o, meta->seq);
    put_str(&o, "\" up=\"");
    put_u64(&o, meta->up_ms);
    put(&o, "\"", 1);

    // The message and the end of the structured data must fit after this point.
    // trunc="1" is 10 bytes, "] " is 2 bytes, and the NUL is 1 byte.
    size_t msg_len = line->msg_len;
    bool cut = meta->truncated;
    if (msg_len > 0 && (o.overflow || o.pos + 2 + msg_len + 1 > o.sz)) cut = true;
    if (cut) put_str(&o, " trunc=\"1\"");
    put(&o, "]", 1);
    if (o.overflow) {
        out[0] = '\0';
        return 0;
    }

    if (msg_len > 0 && o.pos + 2 < o.sz) {
        size_t room = o.sz - o.pos - 2;   // space for " " and the NUL
        if (msg_len > room) msg_len = room;
        put(&o, " ", 1);
        put(&o, line->msg, msg_len);
    }
    out[o.pos] = '\0';
    return o.pos;
}
