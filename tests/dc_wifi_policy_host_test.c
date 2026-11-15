// Host test for the pure dc_wifi boot decisions.
#include <stdio.h>
#include <stdlib.h>

#include "dc_wifi_policy.h"

// Not assert(): the checks must also run when a build adds -DNDEBUG.
#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

int main(void)
{
    // A failed boot join: OFF keeps retrying, FALLBACK restarts Wi-Fi as the
    // portal, ALWAYS/TEMP promote the concurrent AP (and stop the STA attempt).
    CHECK(dc_wifi_boot_fail_action(DC_WIFI_AP_OFF) == DC_WIFI_BOOT_FAIL_STA_ONLY);
    CHECK(dc_wifi_boot_fail_action(DC_WIFI_AP_FALLBACK) == DC_WIFI_BOOT_FAIL_START_PORTAL);
    CHECK(dc_wifi_boot_fail_action(DC_WIFI_AP_ALWAYS) == DC_WIFI_BOOT_FAIL_PROMOTE_AP);
    CHECK(dc_wifi_boot_fail_action(DC_WIFI_AP_TEMP) == DC_WIFI_BOOT_FAIL_PROMOTE_AP);

    // The setup portal never shares the device with a LAN link: a late
    // association or IP lease is dropped only in that state.
    CHECK(!dc_wifi_sta_link_allowed(DC_WIFI_STATE_AP_PORTAL));
    CHECK(dc_wifi_sta_link_allowed(DC_WIFI_STATE_INIT));
    CHECK(dc_wifi_sta_link_allowed(DC_WIFI_STATE_STA_CONNECTING));
    CHECK(dc_wifi_sta_link_allowed(DC_WIFI_STATE_STA_CONNECTED));

    // Only a join attempt sets the reason that the setup page shows. Our own
    // portal-state disconnect (ASSOC_LEAVE) keeps e.g. a wrong-password reason.
    CHECK(dc_wifi_disc_reason_relevant(DC_WIFI_STATE_STA_CONNECTING));
    CHECK(dc_wifi_disc_reason_relevant(DC_WIFI_STATE_STA_CONNECTED));
    CHECK(!dc_wifi_disc_reason_relevant(DC_WIFI_STATE_AP_PORTAL));
    CHECK(!dc_wifi_disc_reason_relevant(DC_WIFI_STATE_INIT));

    puts("dc_wifi policy host test passed");
    return 0;
}
