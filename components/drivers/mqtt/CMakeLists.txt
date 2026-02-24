idf_component_register(
    SRCS "mqtt_driver.c"
    INCLUDE_DIRS "."
    REQUIRES mqtt esp_event freertos
)
