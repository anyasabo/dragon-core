idf_component_register(
    SRCS "dc_time.c" "dc_time_format.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_netif
    PRIV_REQUIRES esp_timer log lwip
)
