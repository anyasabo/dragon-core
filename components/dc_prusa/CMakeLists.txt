set(dc_prusa_requires nvs_flash esp_http_client esp_timer)
# ESP-IDF 6 has no json component: idf_component.yml adds espressif/cjson there.
if(IDF_VERSION_MAJOR LESS 6)
    list(APPEND dc_prusa_requires json)
endif()

idf_component_register(
    SRCS "dc_prusa.c"
    INCLUDE_DIRS "include"
    REQUIRES ${dc_prusa_requires}
)
