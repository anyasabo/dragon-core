// SPDX-License-Identifier: MIT
#include "dc_wifi_policy.h"

dc_wifi_boot_fail_action_t dc_wifi_boot_fail_action(dc_wifi_ap_mode_t mode)
{
    switch (mode) {
    case DC_WIFI_AP_OFF:      return DC_WIFI_BOOT_FAIL_STA_ONLY;
    case DC_WIFI_AP_FALLBACK: return DC_WIFI_BOOT_FAIL_START_PORTAL;
    case DC_WIFI_AP_ALWAYS:
    case DC_WIFI_AP_TEMP:
    default:                  return DC_WIFI_BOOT_FAIL_PROMOTE_AP;
    }
}

bool dc_wifi_sta_link_allowed(dc_wifi_state_t state)
{
    return state != DC_WIFI_STATE_AP_PORTAL;
}
