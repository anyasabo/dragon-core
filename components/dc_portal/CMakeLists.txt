# ESP-IDF 6 removed the built-in json component. On ESP-IDF 6 and later,
# idf_component.yml adds the registry cJSON component instead.
set(DC_PORTAL_JSON json)
if(IDF_VERSION_MAJOR GREATER_EQUAL 6)
    set(DC_PORTAL_JSON "")
endif()

idf_component_register(
    SRCS "dc_portal.c" "dc_portal_console_stream.c" "dc_portal_dns.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server esp_netif esp_wifi app_update esp_partition lwip ${DC_PORTAL_JSON} mbedtls
             dc_wifi dc_ui dc_evlog
)
