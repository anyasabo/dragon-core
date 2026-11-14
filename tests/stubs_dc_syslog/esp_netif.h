#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct esp_netif_obj esp_netif_t;
typedef struct { struct { uint32_t addr; } ip, netmask, gw; } esp_netif_ip_info_t;

esp_netif_t *esp_netif_get_default_netif(void);
bool esp_netif_is_netif_up(esp_netif_t *netif);
esp_err_t esp_netif_get_ip_info(esp_netif_t *netif, esp_netif_ip_info_t *ip_info);
