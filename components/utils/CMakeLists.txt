idf_component_register(SRCS "esp_utils.c"
                            "ntp_time.c"
                       INCLUDE_DIRS "."
                       REQUIRES lwip esp_netif esp_event esp_timer)
