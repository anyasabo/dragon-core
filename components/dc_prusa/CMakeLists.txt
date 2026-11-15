# ESP-IDF 6 removed the built-in json component. On ESP-IDF 6 and later,
# idf_component.yml adds the registry cJSON component instead.
set(DC_PRUSA_JSON json)
if(IDF_VERSION_MAJOR GREATER_EQUAL 6)
    set(DC_PRUSA_JSON "")
endif()

idf_component_register(
    SRCS "dc_prusa.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash esp_http_client esp_timer ${DC_PRUSA_JSON}
)
