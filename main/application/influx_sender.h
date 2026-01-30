/**
 * @file influx_sender.h
 * @brief InfluxDB Data Sender Service
 * 
 * Provides a queue-based service task for asynchronously sending sensor data
 * to InfluxDB. Decouples measurement tasks from HTTP transmission to prevent
 * stack overflow and improve system responsiveness.
 */

#ifndef INFLUX_SENDER_H
#define INFLUX_SENDER_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "influxdb_client.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialize and start the sender task (idempotent)
esp_err_t influx_sender_init(void);

// Enqueue messages to be sent by the sender task
esp_err_t influx_sender_enqueue_soil(const influxdb_soil_data_t* data);
esp_err_t influx_sender_enqueue_battery(const influxdb_battery_data_t* data);
esp_err_t influx_sender_enqueue_env(const influxdb_env_data_t* data);

// Wait until the queue is empty (all data has been sent)
esp_err_t influx_sender_wait_until_empty(uint32_t timeout_ms);

// Stop sender task and free queue (for clean deep-sleep deinit)
esp_err_t influx_sender_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // INFLUX_SENDER_H
