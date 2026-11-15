#pragma once

// Pure captive-portal DNS answer, factored out of dc_portal_dns.c so a fuzzer
// can test it on the host (tests/fuzz). Each AP client can send these packets.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define DC_PORTAL_DNS_HDR_LEN 12

// Length of the QNAME at buf, with its final zero byte. Returns 0 if the name
// is not complete in len bytes or uses compression (label length >= 64).
static inline size_t dc_portal_dns_qname_length(const uint8_t *buf, size_t len)
{
    size_t i = 0;
    while (i < len) {
        uint8_t n = buf[i];
        if (n == 0) return i + 1;
        if (n >= 64) return 0;
        i += n + 1;
    }
    return 0;
}

// Change the DNS query in buf (received bytes, buffer of cap bytes) in place
// into an answer: one A record with redirect_ip (network byte order, as in
// esp_ip4_addr_t), TTL 60 s. Returns the answer length. Returns 0 if the packet
// is not a query with a class IN, type A question, or if the answer does not fit.
static inline size_t dc_portal_dns_answer(uint8_t *buf, size_t received, size_t cap,
                                          uint32_t redirect_ip)
{
    if (received < DC_PORTAL_DNS_HDR_LEN) return 0;
    uint16_t flags = (uint16_t)((buf[2] << 8) | buf[3]);
    uint16_t qdcount = (uint16_t)((buf[4] << 8) | buf[5]);
    if ((flags & 0x8000) || qdcount == 0) return 0;
    size_t off = DC_PORTAL_DNS_HDR_LEN;
    size_t qn = dc_portal_dns_qname_length(buf + off, received - off);
    if (!qn || off + qn + 4 > received) return 0;
    if (buf[off + qn] != 0 || buf[off + qn + 1] != 1 ||
        buf[off + qn + 2] != 0 || buf[off + qn + 3] != 1) return 0;
    // flags 0x8180, qdcount 1, ancount 1, nscount 0, arcount 0
    static const uint8_t hdr_tail[10] = { 0x81, 0x80, 0, 1, 0, 1, 0, 0, 0, 0 };
    memcpy(buf + 2, hdr_tail, sizeof hdr_tail);
    size_t response_len = off + qn + 4;
    uint8_t answer[] = {
        0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4,
        (uint8_t)redirect_ip, (uint8_t)(redirect_ip >> 8),
        (uint8_t)(redirect_ip >> 16), (uint8_t)(redirect_ip >> 24),
    };
    if (response_len + sizeof(answer) > cap) return 0;
    memcpy(buf + response_len, answer, sizeof(answer));
    return response_len + sizeof(answer);
}
