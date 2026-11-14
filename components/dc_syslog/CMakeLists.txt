idf_component_register(
    SRCS "dc_syslog.c" "dc_syslog_format.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common
    PRIV_REQUIRES esp_netif esp_timer freertos log lwip
)
