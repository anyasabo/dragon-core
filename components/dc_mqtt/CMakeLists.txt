set(dc_mqtt_requires "")
# ESP-IDF 6 has no mqtt component: idf_component.yml adds espressif/mqtt there.
if(IDF_VERSION_MAJOR LESS 6)
    list(APPEND dc_mqtt_requires mqtt)
endif()

idf_component_register(
    SRCS "dc_mqtt.c"
    INCLUDE_DIRS "include"
    REQUIRES ${dc_mqtt_requires}
)
