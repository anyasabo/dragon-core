set(dc_bambu_requires nvs_flash esp-tls esp_netif esp_timer)
# ESP-IDF 6 has no mqtt component: idf_component.yml adds espressif/mqtt there.
if(IDF_VERSION_MAJOR LESS 6)
    list(APPEND dc_bambu_requires mqtt)
endif()

idf_component_register(
    SRCS "dc_bambu.c" "dc_bambu_discovery.c"
    INCLUDE_DIRS "include"
    # esp-mqtt over TLS (esp-tls/mbedTLS) to the printer's on-device broker.
    # Reports are scanned with strstr/strtof (no cJSON).
    # dc_bambu_discovery.c uses lwip sockets + esp_netif (STA multicast join) and
    # esp_timer for the scan window / age-out timestamps.
    REQUIRES ${dc_bambu_requires}
)
