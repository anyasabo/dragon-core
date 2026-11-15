// SPDX-License-Identifier: MIT
#include "dc_portal_dns.h"
#include "dc_portal_dns_answer.h"

#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

#define DNS_PORT 53
#define DNS_MAX_LEN 512

static const char *TAG = "dc_portal_dns";
static volatile TaskHandle_t s_task;
static int s_sock = -1;
static uint32_t s_redirect_ip;
static volatile bool s_running;

static void dns_task(void *arg)
{
    (void)arg;
    uint8_t buf[DNS_MAX_LEN];
    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons(DNS_PORT) };
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    s_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (s_sock < 0 || bind(s_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "socket/bind failed");
        if (s_sock >= 0) close(s_sock);
        s_sock = -1;
        s_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    while (s_running) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(s_sock, &read_fds);
        struct timeval timeout = { .tv_sec = 0, .tv_usec = 200000 };
        int ready = select(s_sock + 1, &read_fds, NULL, NULL, &timeout);
        if (!s_running) break;
        if (ready <= 0) continue;
        struct sockaddr_in src = {0};
        socklen_t src_len = sizeof(src);
        int received = recvfrom(s_sock, buf, sizeof(buf), 0,
                                (struct sockaddr *)&src, &src_len);
        size_t answer_len = dc_portal_dns_answer(buf, received > 0 ? (size_t)received : 0,
                                                 sizeof(buf), s_redirect_ip);
        if (answer_len == 0) continue;
        sendto(s_sock, buf, answer_len, 0,
               (struct sockaddr *)&src, src_len);
    }
    close(s_sock);
    s_sock = -1;
    s_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t dc_portal_dns_start(uint32_t redirect_ip)
{
    if (s_task) return ESP_ERR_INVALID_STATE;
    s_redirect_ip = redirect_ip;
    s_running = true;
    if (xTaskCreate(dns_task, "dc_portal_dns", 4096, NULL, 4,
                    (TaskHandle_t *)&s_task) == pdPASS) return ESP_OK;
    s_running = false;
    return ESP_ERR_NO_MEM;
}

void dc_portal_dns_stop(void)
{
    s_running = false;
    // The task owns its socket and exits itself. Its bounded select timeout
    // avoids deleting a task while lwIP may hold internal state in recvfrom().
    for (int i = 0; s_task && i < 20; ++i)
        vTaskDelay(pdMS_TO_TICKS(25));
    if (s_task) ESP_LOGW(TAG, "DNS task did not stop within 500 ms");
}
