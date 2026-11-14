#include "dc_time.h"

#include <string.h>
#include <sys/time.h>

#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "sdkconfig.h"

static const char *TAG = "dc_time";

#define SERVER_MAX 64

// lwIP keeps the pointer to the server name, so the name must stay in memory.
static char s_server[SERVER_MAX];
static bool s_started = false;

// Offset from esp_timer to the wall clock. The SNTP callback writes it in the
// TCP/IP task. Readers can be in any task, so a spinlock guards the 64-bit value.
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static int64_t s_offset_us = 0;
static bool s_synced = false;

static void on_sync(struct timeval *tv)
{
    (void)tv;
    struct timeval now;
    gettimeofday(&now, NULL);
    int64_t wall_us = (int64_t)now.tv_sec * 1000000LL + now.tv_usec;
    int64_t offset = wall_us - esp_timer_get_time();

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

esp_err_t dc_time_start(const dc_time_config_t *cfg)
{
    if (s_started) return ESP_ERR_INVALID_STATE;
    const char *server = (cfg && cfg->server) ? cfg->server : "";
    if (strlen(server) >= sizeof s_server) return ESP_ERR_INVALID_ARG;
    bool from_dhcp = server[0] == '\0';
#if !CONFIG_LWIP_DHCP_GET_NTP_SRV
    if (from_dhcp) {
        ESP_LOGE(TAG, "no NTP server: set one or enable CONFIG_LWIP_DHCP_GET_NTP_SRV");
        return ESP_ERR_NOT_SUPPORTED;
    }
#endif
    strcpy(s_server, server);

    esp_sntp_config_t sntp = {
        .smooth_sync = false,
        .server_from_dhcp = from_dhcp,
        .wait_for_sync = false,       // no semaphore: dc_time_is_synced() polls
        .start = true,
        .sync_cb = on_sync,
        .renew_servers_after_new_IP = false,
        .ip_event_to_renew = IP_EVENT_STA_GOT_IP,
        .index_of_first_server = 0,
        .num_of_servers = from_dhcp ? 0 : 1,
        .servers = { from_dhcp ? NULL : s_server },
    };
    esp_err_t err = esp_netif_sntp_init(&sntp);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_netif_sntp_init: %s", esp_err_to_name(err));
        return err;
    }
    s_started = true;
    ESP_LOGI(TAG, "SNTP started, server: %s", from_dhcp ? "from DHCP" : s_server);
    return ESP_OK;
}

bool dc_time_is_synced(void)
{
    portENTER_CRITICAL(&s_mux);
    bool synced = s_synced;
    portEXIT_CRITICAL(&s_mux);
    return synced;
}

bool dc_time_now_ms(int64_t *unix_ms)
{
    portENTER_CRITICAL(&s_mux);
    bool synced = s_synced;
    int64_t offset = s_offset_us;
    portEXIT_CRITICAL(&s_mux);
    if (!synced || unix_ms == NULL) return false;
    *unix_ms = (esp_timer_get_time() + offset) / 1000;
    return true;
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
