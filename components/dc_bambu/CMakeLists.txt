# ESP-IDF 6 removed the built-in mqtt component. On ESP-IDF 6 and later,
# idf_component.yml adds the registry ESP-MQTT component instead.
set(DC_BAMBU_MQTT mqtt)
if(IDF_VERSION_MAJOR GREATER_EQUAL 6)
    set(DC_BAMBU_MQTT "")
endif()

idf_component_register(
    SRCS "dc_bambu.c" "dc_bambu_discovery.c"
    INCLUDE_DIRS "include"
    # esp-mqtt over TLS (esp-tls/mbedTLS) to the printer's on-device broker.
    # Reports are scanned with strstr/strtof (no cJSON).
    # dc_bambu_discovery.c uses lwip sockets + esp_netif (STA multicast join) and
    # esp_timer for the scan window / age-out timestamps.
    REQUIRES nvs_flash ${DC_BAMBU_MQTT} esp-tls esp_netif esp_timer
)
