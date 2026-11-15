idf_component_register(SRCS "dc_lighting.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_driver_gpio esp_driver_spi led_strip esp_timer)
