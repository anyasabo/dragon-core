// libFuzzer harness for the captive-portal DNS answer (dc_portal_dns_answer.h).
// The input is one UDP packet from an AP client. Build and run: tests/run_fuzz.sh.
#include "dc_portal_dns_answer.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define DNS_MAX_LEN 512   // the receive buffer of dc_portal_dns.c

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size > DNS_MAX_LEN) return 0;   // recvfrom() cuts larger packets
    // A heap buffer of the exact size, so ASan finds each byte out of range.
    uint8_t *buf = malloc(DNS_MAX_LEN);
    if (!buf) return 0;
    memcpy(buf, data, size);

    size_t n = dc_portal_dns_answer(buf, size, DNS_MAX_LEN, 0x0104a8c0u);
    if (n) {
        // An answer: question copied, flags 0x8180, one A record of 16 bytes.
        size_t qn = dc_portal_dns_qname_length(data + DC_PORTAL_DNS_HDR_LEN,
                                               size - DC_PORTAL_DNS_HDR_LEN);
        if (n > DNS_MAX_LEN || n != DC_PORTAL_DNS_HDR_LEN + qn + 4 + 16 ||
            buf[2] != 0x81 || buf[3] != 0x80 || buf[n - 4] != 0xc0)
            abort();
    }
    free(buf);
    return 0;
}
