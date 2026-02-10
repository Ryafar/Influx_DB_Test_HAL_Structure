/**
 * @file esp32-config.h
 * @brief ESP32 Hardware Configuration for Multi-Sensor Project
 * 
 * This file contains all hardware-specific configurations including
 * pin assignments, ADC settings, feature toggles, and project constants.
 */

#ifndef ESP32_CONFIG_H
#define ESP32_CONFIG_H

#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"

// Note: WiFi/InfluxDB credentials are in credentials.h (git-ignored)
// Include credentials.h directly in files that need it (wifi_manager, influxdb_client, etc.)

// ============================================================================
// Feature Toggles - Enable/Disable Monitoring Modules
// ============================================================================

#define ENABLE_ENV_MONITOR      0    // AHT20 temperature/humidity sensor
#define ENABLE_BATTERY_MONITOR  1    // Battery voltage monitoring via ADC
#define ENABLE_SOIL_MONITOR     1    // Soil moisture monitoring via ADC

// ============================================================================
// Deep Sleep Configuration
// ============================================================================

#define DEEP_SLEEP_ENABLED              0                   // Enable/disable deep sleep mode (0 = continuous loop with delay)
#define DEEP_SLEEP_DURATION_SECONDS     10                  // Sleep duration between measurement cycles
#define DEEP_SLEEP_WAKEUP_DELAY_MS      100                 // Delay before entering deep sleep

// ============================================================================
// GPIO Pin Assignments
// ============================================================================




// ============================================================================
// Battery Monitor Configuration (if enabled)
// ============================================================================

#if ENABLE_BATTERY_MONITOR

#define BATTERY_ADC_UNIT                        ADC_UNIT_1
#define BATTERY_ADC_CHANNEL                     ADC_CHANNEL_0      // GPIO0
#define BATTERY_ADC_BITWIDTH                    ADC_BITWIDTH_12
#define BATTERY_ADC_ATTENUATION                 ADC_ATTEN_DB_12
#define BATTERY_ADC_VREF                        3.3f

#define BATTERY_MONITOR_VOLTAGE_SCALE_FACTOR    2.0f   // 2:1 voltage divider (two 10kΩ resistors)
#define BATTERY_MONITOR_LOW_VOLTAGE_THRESHOLD   3.2f    // Low battery threshold in volts
#define BATTERY_MONITOR_USE_DEEP_SLEEP_ON_LOW_BATTERY  1

#define BATTERY_MONITOR_TASK_STACK_SIZE         (8 * 1024)
#define BATTERY_MONITOR_TASK_PRIORITY           5
#define BATTERY_MONITOR_TASK_NAME               "battery_monitor"
#define BATTERY_MONITOR_MEASUREMENT_INTERVAL_MS (10 * 1000)
#define BATTERY_MEASUREMENTS_PER_CYCLE          1

#endif // ENABLE_BATTERY_MONITOR

// ============================================================================
// Soil Monitor Configuration
// ============================================================================

#define SOIL_ADC_UNIT               ADC_UNIT_1
#define SOIL_ADC_CHANNEL            ADC_CHANNEL_1      // GPIO1 (moved from GPIO0)
#define SOIL_ADC_BITWIDTH           ADC_BITWIDTH_12
#define SOIL_ADC_ATTENUATION        ADC_ATTEN_DB_12
#define SOIL_ADC_VREF               3.3f

#define SOIL_SENSOR_POWER_PIN       GPIO_NUM_2     // GPIO2 controls power to the soil sensor (moved from GPIO9 which is a boot strapping pin)

#define SOIL_TASK_STACK_SIZE            (4 * 1024)
#define SOIL_TASK_PRIORITY              5
#define SOIL_TASK_NAME                  "soil_monitor"
#define SOIL_DRY_VOLTAGE_DEFAULT        3.0f
#define SOIL_WET_VOLTAGE_DEFAULT        1.0f
#define SOIL_MEASUREMENT_INTERVAL_MS    (10 * 1000)
#define SOIL_MEASUREMENTS_PER_CYCLE     1

// ============================================================================
// I2C + Environment Task (AHT20) Configuration
// ============================================================================

#define I2C_PORT                     I2C_NUM_0
#define I2C_SDA_PIN                  GPIO_NUM_6 // Changed from GPIO7
#define I2C_SCL_PIN                  GPIO_NUM_19 // CRITICAL: GPIO9 is boot strapping pin! Changed to GPIO19    
#define I2C_FREQ_HZ                  100000

#define ENV_TASK_STACK_SIZE          (8 * 1024)   // Increased to reduce stack pressure during logging/formatting
#define ENV_TASK_PRIORITY            5
// Interval between measurements inside a single wake cycle (used if measurements per cycle > 1)
#define ENV_MEASUREMENT_INTERVAL_MS  (10 * 1000) // 10 seconds

// Kconfig driven overrides (fallback defaults if not defined)
#ifndef CONFIG_ENV_SLEEP_SECONDS
#define CONFIG_ENV_SLEEP_SECONDS 10
#endif
#ifndef CONFIG_ENV_MEASUREMENTS_PER_CYCLE
#define CONFIG_ENV_MEASUREMENTS_PER_CYCLE 1
#endif
#ifndef CONFIG_ENV_ENABLE_LOGGING
#define CONFIG_ENV_ENABLE_LOGGING 1
#endif

// ============================================================================
// NTP Time Synchronization Configuration
// ============================================================================

#define NTP_ENABLED                     0                   // Enable/disable NTP time synchronization (0 = use server time, 1 = use NTP time)
#define NTP_SYNC_TIMEOUT_MS             15000               // NTP sync timeout in milliseconds

// ============================================================================
// Logging Configuration
// ============================================================================

#define ENV_ENABLE_DETAILED_LOGGING    1
#define ENV_LOG_LEVEL          ESP_LOG_INFO

// ============================================================================
// WiFi Configuration
// ============================================================================

#define WIFI_MAX_RETRY          15
#define WIFI_CONNECTED_BIT      BIT0
#define WIFI_FAIL_BIT           BIT1

// ============================================================================
// InfluxDB Configuration
// ============================================================================
// Note: INFLUXDB_SERVER, INFLUXDB_BUCKET, INFLUXDB_ORG, and INFLUXDB_TOKEN
//       are defined in credentials.h (git-ignored)

#define USE_INFLUXDB            1                   // Enable InfluxDB data logging
#define INFLUXDB_PORT           443                 // HTTPS port (Caddy reverse proxy)
#define INFLUXDB_USE_HTTPS      1                   // HTTPS required for Caddy proxy
#define INFLUXDB_ENDPOINT       "/api/v2/write"

// ============================================================================
// Wi-Fi Failure Backoff
// ============================================================================
// If Wi-Fi cannot connect after configured retries, use a longer backoff sleep
// to save power before attempting again.
#define WIFI_FAILURE_BACKOFF_SECONDS  60   // Sleep 60s on Wi-Fi failure

#define HTTP_TIMEOUT_MS         15000               // Increased timeout to 15s
#define HTTP_MAX_RETRIES        3                   // More retries
#define HTTP_ENABLE_BUFFERING   1
#define HTTP_MAX_BUFFERED_PACKETS  100

#endif // ESP32_CONFIG_H




