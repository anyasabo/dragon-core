# ESP-IDF 6 removed the built-in json component. On ESP-IDF 6 and later,
# idf_component.yml adds the registry cJSON component instead.
set(DC_MOONRAKER_JSON json)
if(IDF_VERSION_MAJOR GREATER_EQUAL 6)
    set(DC_MOONRAKER_JSON "")
endif()

idf_component_register(
    SRCS "dc_moonraker.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash ${DC_MOONRAKER_JSON} esp_websocket_client dc_evlog
)
