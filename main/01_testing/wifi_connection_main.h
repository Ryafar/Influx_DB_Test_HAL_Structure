#ifndef WIFI_CONNECTION_H
#define WIFI_CONNECTION_H

#include <stdio.h>
#include "esp_system.h"
#include "esp_log.h"

// Project configuration
#include "../config/esp32-config.h"
#include "../config/credentials.h"

#include "../drivers/http/http_client.h"
#include "../drivers/wifi/wifi_manager.h"


/**
 * @brief Main application entry point
 */
void app_main(void);

#endif // WIFI_CONNECTION_H
