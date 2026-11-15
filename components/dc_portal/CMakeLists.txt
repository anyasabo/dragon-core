set(dc_portal_requires esp_http_server esp_timer esp_netif esp_wifi app_update esp_partition lwip mbedtls
    dc_wifi dc_ui dc_evlog)
# ESP-IDF 6 has no json component: idf_component.yml adds espressif/cjson there.
if(IDF_VERSION_MAJOR LESS 6)
    list(APPEND dc_portal_requires json)
endif()

idf_component_register(
    SRCS "dc_portal.c" "dc_portal_console_stream.c" "dc_portal_dns.c"
    INCLUDE_DIRS "include"
    REQUIRES ${dc_portal_requires}
)
