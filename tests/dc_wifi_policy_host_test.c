// Host test for the pure dc_wifi boot decisions.
#include <assert.h>
#include <stdio.h>

#include "dc_wifi_policy.h"

int main(void)
{
    // A failed boot join: OFF keeps retrying, FALLBACK restarts Wi-Fi as the
    // portal, ALWAYS/TEMP promote the concurrent AP (and stop the STA attempt).
    assert(dc_wifi_boot_fail_action(DC_WIFI_AP_OFF) == DC_WIFI_BOOT_FAIL_STA_ONLY);
    assert(dc_wifi_boot_fail_action(DC_WIFI_AP_FALLBACK) == DC_WIFI_BOOT_FAIL_START_PORTAL);
    assert(dc_wifi_boot_fail_action(DC_WIFI_AP_ALWAYS) == DC_WIFI_BOOT_FAIL_PROMOTE_AP);
    assert(dc_wifi_boot_fail_action(DC_WIFI_AP_TEMP) == DC_WIFI_BOOT_FAIL_PROMOTE_AP);

    // The setup portal never shares the device with a LAN link: a late
    // association or IP lease is dropped only in that state.
    assert(!dc_wifi_sta_link_allowed(DC_WIFI_STATE_AP_PORTAL));
    assert(dc_wifi_sta_link_allowed(DC_WIFI_STATE_INIT));
    assert(dc_wifi_sta_link_allowed(DC_WIFI_STATE_STA_CONNECTING));
    assert(dc_wifi_sta_link_allowed(DC_WIFI_STATE_STA_CONNECTED));

    puts("dc_wifi policy host test passed");
    return 0;
}
