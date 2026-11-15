// SPDX-License-Identifier: MIT
// Private, pure decisions of the dc_wifi boot path (no ESP-IDF calls, host-tested).
#pragma once

#include <stdbool.h>
#include "dc_wifi.h"

// What the boot path does if the STA did not connect inside the boot wait.
typedef enum {
    DC_WIFI_BOOT_FAIL_STA_ONLY = 0,   // OFF: no portal, keep retrying the STA
    DC_WIFI_BOOT_FAIL_START_PORTAL,   // FALLBACK: stop Wi-Fi, start the AP portal
    DC_WIFI_BOOT_FAIL_PROMOTE_AP,     // ALWAYS/TEMP: the concurrent AP becomes the
                                      // portal, and the STA attempt must stop
} dc_wifi_boot_fail_action_t;

dc_wifi_boot_fail_action_t dc_wifi_boot_fail_action(dc_wifi_ap_mode_t mode);

// False while the setup portal runs. The portal routes accept requests without
// auth, so the STA must not join the LAN in this state: a late association or
// IP lease must be dropped.
bool dc_wifi_sta_link_allowed(dc_wifi_state_t state);
