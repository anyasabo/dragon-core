// libFuzzer harness for the Bambu MQTT report parser in dc_bambu_parse.h.
// The input is one MQTT report payload. The harness calls the parser functions
// in the same sequence as the report handler in dc_bambu.c.
// Build and run: tests/run_fuzz.sh.
#include "dc_bambu_parse.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    // dc_bambu.c terminates the reassembled payload before it parses it.
    char *json = malloc(size + 1);
    if (!json) return 0;
    memcpy(json, data, size);
    json[size] = '\0';

    char fila[16];
    dc_fila_result_t fr = dc_bambu_active_filament(json, fila, sizeof fila);
    if (fr == DC_FILA_PRESENT && (fila[0] == '\0' || !memchr(fila, '\0', sizeof fila)))
        abort();

    char gs[16];
    bool got_gs = dc_bambu_find_string(json, "\"gcode_state\"", gs, sizeof gs);
    dc_bambu_gcode_phase_t phase = got_gs ? dc_bambu_gcode_phase(gs)
                                          : DC_BAMBU_GCODE_UNKNOWN;
    (void)dc_bambu_gcode_active(got_gs ? gs : NULL);

    uint32_t code = 0;
    bool got_code = dc_bambu_print_error_code(json, &code);
    (void)dc_bambu_next_print_error_code(0, got_gs, phase, got_code, code);

    static const char *const names[] = { "PLA", "PETG", "PETG-CF", "ABS", "ASA", "" };
    (void)dc_bambu_zone_match(fr == DC_FILA_PRESENT ? fila : NULL, names, 6);
    (void)dc_bambu_zone_match(json, names, 6);

    free(json);
    return 0;
}
