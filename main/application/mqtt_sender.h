/**
 * @file mqtt_sender.h
 * @brief MQTT Data Sender Service
 * 
 * Provides a queue-based service task for asynchronously sending sensor data
 * to MQTT broker. Decouples measurement tasks from MQTT transmission to prevent
 * stack overflow and improve system responsiveness.
 */

#ifndef MQTT_SENDER_H
#define MQTT_SENDER_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Soil moisture measurement data for MQTT
 */
typedef struct {
    uint64_t timestamp_ms;          ///< Timestamp in milliseconds
    float voltage;                  ///< Sensor voltage
    float moisture_percent;         ///< Moisture percentage
    int raw_adc;                   ///< Raw ADC reading
    char device_id[32];            ///< Device identifier
} mqtt_soil_data_t;

/**
 * @brief Battery measurement data for MQTT
 */
typedef struct {
    uint64_t timestamp_ms;          ///< Timestamp in milliseconds
    float voltage;                  ///< Battery voltage
    float percentage;               ///< Battery percentage (if available)
    char device_id[32];            ///< Device identifier
} mqtt_battery_data_t;

/**
 * @brief Environment measurement data for MQTT
 */
typedef struct {
    uint64_t timestamp_ms;          ///< Timestamp in milliseconds
    float temperature;              ///< Temperature in Celsius
    float humidity;                 ///< Relative humidity percentage
    char device_id[32];            ///< Device identifier
} mqtt_env_data_t;

// Initialize and start the sender task (idempotent)
esp_err_t mqtt_sender_init(void);

// Enqueue messages to be sent by the sender task
esp_err_t mqtt_sender_enqueue_soil(const mqtt_soil_data_t* data);
esp_err_t mqtt_sender_enqueue_battery(const mqtt_battery_data_t* data);
esp_err_t mqtt_sender_enqueue_env(const mqtt_env_data_t* data);

// Wait until the queue is empty (all data has been sent)
esp_err_t mqtt_sender_wait_until_empty(uint32_t timeout_ms);

// Stop sender task and free queue (for clean deep-sleep deinit)
esp_err_t mqtt_sender_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // MQTT_SENDER_H
