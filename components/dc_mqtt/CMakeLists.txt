# ESP-IDF 6 removed the built-in mqtt component. On ESP-IDF 6 and later,
# idf_component.yml adds the registry ESP-MQTT component instead.
set(DC_MQTT_MQTT mqtt)
if(IDF_VERSION_MAJOR GREATER_EQUAL 6)
    set(DC_MQTT_MQTT "")
endif()

idf_component_register(
    SRCS "dc_mqtt.c"
    INCLUDE_DIRS "include"
    REQUIRES ${DC_MQTT_MQTT}
)
