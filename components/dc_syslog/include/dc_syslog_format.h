#pragma once

// Pure parts of dc_syslog: parse one ESP-IDF log line and format it as an
// RFC 5424 message. No ESP-IDF dependency, no allocation. The host test
// (tests/dc_syslog_host_test.c) compiles this file.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// RFC 5424 facility of all messages: 1 = user-level messages.
#define DC_SYSLOG_FACILITY      1
// SD-ID of the structured data. 32473 is the example enterprise number of
// RFC 5612. A product that has its own number can change it here.
#define DC_SYSLOG_SD_ID         "dc@32473"
// Maximum length of the RFC 5424 fields. These limits are lower than the RFC
// limits, so that the datagram stays small.
#define DC_SYSLOG_HOSTNAME_MAX  63
#define DC_SYSLOG_APPNAME_MAX   48
#define DC_SYSLOG_PROCID_MAX    32
#define DC_SYSLOG_TIMESTAMP_MAX 32
// Space for all fields before MSG. A datagram buffer of
// DC_SYSLOG_HEADER_MAX + line length + 1 always holds the full line.
#define DC_SYSLOG_HEADER_MAX    256

// RFC 5424 severity values that dc_syslog uses.
#define DC_SYSLOG_SEV_ERR       3
#define DC_SYSLOG_SEV_WARNING   4
#define DC_SYSLOG_SEV_INFO      6
#define DC_SYSLOG_SEV_DEBUG     7

// One log line, after dc_syslog_parse_line(). The pointers point into the line.
typedef struct {
    int severity;        // RFC 5424 severity, 0 to 7
    const char *tag;     // ESP log tag, or NULL if the line has no ESP prefix
    size_t tag_len;
    const char *msg;     // text after the prefix
    size_t msg_len;
} dc_syslog_line_t;

// Fields of one message that do not come from the line.
typedef struct {
    const char *timestamp;  // RFC 5424 TIMESTAMP. NULL or "" gives "-".
    const char *hostname;   // NULL or "" gives "-".
    const char *app_name;   // NULL or "" gives "-".
    uint32_t seq;           // sequence number of the line
    uint64_t up_ms;         // uptime in ms when the line was logged
    bool truncated;         // the line was cut before it was formatted
} dc_syslog_meta_t;

// Map an ESP log level letter (E, W, I, D, V) to an RFC 5424 severity.
// Returns -1 for other characters.
int dc_syslog_severity(char level);

// Remove ANSI escape sequences (ESC [ ... final byte) from s, in place. An
// incomplete sequence at the end is also removed. A cut line can end that way.
// Then remove CR and LF at the end. Returns the new length. Does not add a NUL.
size_t dc_syslog_strip(char *s, size_t len);

// Parse a line that dc_syslog_strip() cleaned. The ESP-IDF format is
// "L (TIME) TAG: MSG", where L is the level letter and TIME is the log
// timestamp. The parser removes the prefix, because RFC 5424 has fields for
// these values. If the line has no prefix, the result is severity INFO, tag NULL
// and the full line as the message.
void dc_syslog_parse_line(const char *line, size_t len, dc_syslog_line_t *out);

// Write one RFC 5424 message into out:
//   <PRI>1 TIMESTAMP HOSTNAME APP-NAME PROCID - [dc@32473 seq="N" up="MS"] MSG
// PROCID is the log tag ("-" if there is no tag). Characters that RFC 5424 does
// not allow in a header field become '_'. If MSG does not fit, it is cut and
// the structured data gets trunc="1". meta->truncated also adds trunc="1".
// If out_sz > 0, out is always NUL-terminated. Returns the length without the
// NUL. If the header does not fit, returns 0 and "". When the message must be
// cut, the header includes trunc="1". A buffer of DC_SYSLOG_HEADER_MAX bytes or
// more always holds the header.
size_t dc_syslog_format(char *out, size_t out_sz, const dc_syslog_meta_t *meta,
                        const dc_syslog_line_t *line);

#ifdef __cplusplus
}
#endif
