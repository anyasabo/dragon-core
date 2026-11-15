#include "dc_time.h"

#include <string.h>
#include <sys/time.h>

#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_net_stack.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "lwip/dhcp.h"
#include "sdkconfig.h"

static const char *TAG = "dc_time";

#define SERVER_MAX 64

// lwIP keeps the pointer to the server name, so the name must stay in memory.
static char s_server[SERVER_MAX];
static bool s_started = false;
static esp_timer_handle_t s_warn_timer;

// Offset from esp_timer to the wall clock. The SNTP callback writes it in the
// TCP/IP task. Readers can be in any task or an ISR, so a spinlock guards the
// 64-bit value.
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static int64_t s_offset_us = 0;
static bool s_synced = false;

static void on_sync(struct timeval *tv)
{
    (void)tv;
    struct timeval now;
    gettimeofday(&now, NULL);
    int64_t wall_us = (int64_t)now.tv_sec * 1000000LL + now.tv_usec;
    int64_t offset = dc_time_offset_us(wall_us, esp_timer_get_time());

    portENTER_CRITICAL(&s_mux);
    bool first = !s_synced;
    s_offset_us = offset;
    s_synced = true;
    portEXIT_CRITICAL(&s_mux);

    char ts[DC_TIME_RFC3339_SIZE];
    dc_time_format_rfc3339(wall_us / 1000, ts, sizeof ts);
    if (first) ESP_LOGI(TAG, "time synced: %s", ts);
    else ESP_LOGD(TAG, "time synced: %s", ts);
}

#if CONFIG_LWIP_DHCP_GET_NTP_SRV
// Runs in the TCP/IP task (esp_netif_tcpip_exec).
static esp_err_t dhcp_renew_api(void *ctx)
{
    struct netif *netif = esp_netif_get_netif_impl((esp_netif_t *)ctx);
    if (netif == NULL || netif_dhcp_data(netif) == NULL) return ESP_ERR_INVALID_STATE;
    return dhcp_renew(netif) == ERR_OK ? ESP_OK : ESP_FAIL;
}

// lwIP gives option 42 to SNTP only from a DHCP reply after
// sntp_servermode_dhcp(1). If the station already has its lease (dc_wifi
// waits for it), ask for a renewal now. Otherwise the server comes with the
// next renewal, often hours later.
static void dhcp_renew_if_bound(void)
{
    esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip;
    esp_netif_dhcp_status_t st;
    if (sta == NULL || esp_netif_get_ip_info(sta, &ip) != ESP_OK || ip.ip.addr == 0 ||
        esp_netif_dhcpc_get_status(sta, &st) != ESP_OK || st != ESP_NETIF_DHCP_STARTED)
        return;   // no lease yet: the first DHCP reply brings option 42
    esp_err_t err = esp_netif_tcpip_exec(dhcp_renew_api, sta);
    if (err == ESP_OK) ESP_LOGI(TAG, "DHCP renewal requested for the NTP server");
    else ESP_LOGW(TAG, "DHCP renewal: %s", esp_err_to_name(err));
}
#endif

// esp_timer one-shot: make a missing NTP server visible in the log.
static void on_warn_timer(void *arg)
{
    (void)arg;
    if (!dc_time_is_synced())
        ESP_LOGW(TAG, "no SNTP sync after %d min: check the NTP server "
                 "(DHCP option 42 is often missing)", DC_TIME_SYNC_WARN_MIN);
}

esp_err_t dc_time_start(const dc_time_config_t *cfg)
{
    if (s_started) return ESP_ERR_INVALID_STATE;
    if (cfg == NULL) return ESP_ERR_INVALID_ARG;
    const char *server = cfg->server ? cfg->server : "";
    if (strlen(server) >= sizeof s_server) return ESP_ERR_INVALID_ARG;
    bool have_server = server[0] != '\0';
    if (!have_server && !cfg->use_dhcp) return ESP_ERR_INVALID_ARG;
#if !CONFIG_LWIP_DHCP_GET_NTP_SRV
    if (cfg->use_dhcp) {
        ESP_LOGE(TAG, "use_dhcp needs CONFIG_LWIP_DHCP_GET_NTP_SRV=y");
        return ESP_ERR_NOT_SUPPORTED;
    }
#endif
    strcpy(s_server, server);

    esp_sntp_config_t sntp = {
        .smooth_sync = false,
        .server_from_dhcp = cfg->use_dhcp,
        .wait_for_sync = false,       // no semaphore: dc_time_is_synced() polls
        .start = true,
        .sync_cb = on_sync,
        .renew_servers_after_new_IP = false,
        .ip_event_to_renew = IP_EVENT_STA_GOT_IP,
        .index_of_first_server = 0,
        .num_of_servers = have_server ? 1 : 0,
        .servers = { have_server ? s_server : NULL },
    };
    if (cfg->use_dhcp && have_server) {
#if CONFIG_LWIP_SNTP_MAX_SERVERS >= 2
        // DHCP writes slot 0 and clears the other slots. esp_netif_sntp puts
        // the fallback in slot 1 again after each new IP address. Until the
        // first lease, the fallback is also in slot 0.
        sntp.renew_servers_after_new_IP = true;
        sntp.index_of_first_server = 1;
#else
        ESP_LOGE(TAG, "a DHCP and a fallback server need CONFIG_LWIP_SNTP_MAX_SERVERS=2");
        return ESP_ERR_NOT_SUPPORTED;
#endif
    }

    esp_err_t err = esp_netif_sntp_init(&sntp);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_netif_sntp_init: %s", esp_err_to_name(err));
        return err;
    }
    s_started = true;
#if CONFIG_LWIP_DHCP_GET_NTP_SRV
    if (cfg->use_dhcp) dhcp_renew_if_bound();
#endif

    const esp_timer_create_args_t warn = {
        .callback = on_warn_timer,
        .name = "dc_time_warn",
    };
    if (esp_timer_create(&warn, &s_warn_timer) == ESP_OK)
        esp_timer_start_once(s_warn_timer,
                             (uint64_t)DC_TIME_SYNC_WARN_MIN * 60 * 1000000);

    ESP_LOGI(TAG, "SNTP started, server: %s%s%s", cfg->use_dhcp ? "from DHCP" : "",
             cfg->use_dhcp && have_server ? ", fallback " : "", s_server);
    return ESP_OK;
}

bool dc_time_is_synced(void)
{
    portENTER_CRITICAL_SAFE(&s_mux);
    bool synced = s_synced;
    portEXIT_CRITICAL_SAFE(&s_mux);
    return synced;
}

bool dc_time_now_ms(int64_t *unix_ms)
{
    portENTER_CRITICAL_SAFE(&s_mux);
    bool synced = s_synced;
    int64_t offset = s_offset_us;
    portEXIT_CRITICAL_SAFE(&s_mux);
    return dc_time_wall_ms(synced, offset, esp_timer_get_time(), unix_ms);
}

bool dc_time_format_now(char *buf, size_t len)
{
    int64_t ms;
    if (!dc_time_now_ms(&ms)) {
        if (buf && len) buf[0] = '\0';
        return false;
    }
    return dc_time_format_rfc3339(ms, buf, len) != 0;
}
