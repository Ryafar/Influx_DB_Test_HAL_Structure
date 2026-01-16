/**
 * @file soil_project_main.h
 * @brief Main application header for Soil Moisture Sensor project
 */

#ifndef SOIL_PROJECT_MAIN_H
#define SOIL_PROJECT_MAIN_H

#include <stdio.h>
#include "esp_system.h"
#include "esp_log.h"

// Project configuration
#include "config/esp32-config.h"

// Application layer
#include "application/env_monitor_app.h"

/**
 * @brief Main application entry point
 */
void app_main(void);

#endif // SOIL_PROJECT_MAIN_H


