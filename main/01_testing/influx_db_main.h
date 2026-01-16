#ifndef INFLUX_DB_MAIN_H
#define INFLUX_DB_MAIN_H

#include <stdio.h>
#include "esp_system.h"
#include "esp_log.h"

// Project configuration
#include "../config/esp32-config.h"
#include "../config/credentials.h"

#include "../drivers/wifi/wifi_manager.h"
#include "../drivers/influxdb/influxdb_client.h"


/**
 * @brief Main application entry point
 */
void app_main(void);

#endif // INFLUX_DB_MAIN_H