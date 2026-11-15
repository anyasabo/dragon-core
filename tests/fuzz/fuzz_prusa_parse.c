// libFuzzer harness for the PrusaLink JSON parsers in dc_prusa_parse.h.
// The input is the HTTP body of /api/v1/job or /api/printer, as the device
// receives it. Both parsers get each input, because the device can receive any
// body on either URL. Build and run: tests/run_fuzz.sh.
#include "dc_prusa_parse.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void check_out(const char *out, size_t out_sz, bool ok)
{
    // A true result must give a non-empty, terminated token in the buffer.
    if (ok && (out[0] == '\0' || memchr(out, '\0', out_sz) == NULL)) abort();
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    // The device reads the body into a fixed buffer and terminates it.
    char *json = malloc(size + 1);
    if (!json) return 0;
    memcpy(json, data, size);
    json[size] = '\0';

    char out[DC_PRUSA_MAT_MAX];
    char tiny[2];
    bool ok;

    ok = dc_prusa_parse_job_material(json, out, sizeof out);
    check_out(out, sizeof out, ok);
    ok = dc_prusa_parse_job_material(json, tiny, sizeof tiny);
    check_out(tiny, sizeof tiny, ok);

    ok = dc_prusa_parse_printer_material(json, out, sizeof out);
    check_out(out, sizeof out, ok);
    ok = dc_prusa_parse_printer_material(json, tiny, sizeof tiny);
    check_out(tiny, sizeof tiny, ok);

    free(json);
    return 0;
}
