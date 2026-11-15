set(dc_moonraker_requires nvs_flash esp_websocket_client dc_evlog)
# ESP-IDF 6 has no json component: idf_component.yml adds espressif/cjson there.
if(IDF_VERSION_MAJOR LESS 6)
    list(APPEND dc_moonraker_requires json)
endif()

idf_component_register(
    SRCS "dc_moonraker.c"
    INCLUDE_DIRS "include"
    REQUIRES ${dc_moonraker_requires}
)
