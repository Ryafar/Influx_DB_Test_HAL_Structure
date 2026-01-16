
/**
 * @file csm-v2-driver.h
 * @brief Capacitive Soil Moisture Sensor V2 Driver
 * 
 * This driver provides a high-level interface for reading soil moisture
 * measurements from a capacitive soil moisture sensor.
 */

#ifndef CSM_V2_DRIVER_H
#define CSM_V2_DRIVER_H

#include "esp_err.h"
#include "driver/gpio.h"

#include "utils/esp_utils.h"
#include "../adc/adc_manager.h"

/**
 * Parameters
 */
#define CSM_V2_DRY_VOLTAGE_DEFAULT    3.0f   ///< Default dry voltage (in volts)
#define CSM_V2_WET_VOLTAGE_DEFAULT    1.0f   ///< Default wet voltage (in volts)



/**
 * @brief Soil moisture sensor configuration
 */
typedef struct {
    adc_unit_t adc_unit;                ///< ADC unit to use
    adc_channel_t adc_channel;          ///< ADC channel to use
    int esp_pin_power;                  ///< GPIO pin to power the sensor
    float dry_voltage;                  ///< Voltage reading when sensor is completely dry
    float wet_voltage;                  ///< Voltage reading when sensor is completely wet
    bool enable_calibration;            ///< Enable automatic calibration
} csm_v2_config_t;

/**
 * @brief Soil moisture sensor handle
 */
typedef struct {
    csm_v2_config_t config;             ///< Sensor configuration
    bool is_initialized;                ///< Initialization status
} csm_v2_driver_t;

/**
 * @brief Soil moisture reading structure
 */
typedef struct {
    uint64_t timestamp;                 ///< Timestamp of the reading
    float voltage;                      ///< Raw voltage reading
    float moisture_percent;             ///< Moisture percentage (0-100%)
    int raw_adc;                        ///< Raw ADC value
} csm_v2_reading_t;

/**
 * @brief Get default configuration for the soil moisture sensor
 * 
 * @param config Pointer to configuration structure to fill
 * @param adc_unit ADC unit to use
 * @param adc_channel ADC channel to use
 * @param power_pin GPIO pin number for power control
 * @return esp_err_t ESP_OK on success
 */
esp_err_t csm_v2_get_default_config(csm_v2_config_t* config, adc_unit_t adc_unit, adc_channel_t adc_channel, int power_pin);

/**
 * @brief Initialize the soil moisture sensor driver
 * 
 * @param driver Pointer to driver handle
 * @param config Pointer to configuration structure
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t csm_v2_init(csm_v2_driver_t* driver, const csm_v2_config_t* config);

/**
 * @brief Deinitialize the soil moisture sensor driver
 * 
 * @param driver Pointer to driver handle
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t csm_v2_deinit(csm_v2_driver_t* driver);

/**
 * @brief Read raw voltage from the sensor
 * 
 * @param driver Pointer to driver handle
 * @param voltage Pointer to store voltage reading
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t csm_v2_read_voltage(csm_v2_driver_t* driver, float* voltage);

/**
 * @brief Read complete sensor data
 * 
 * @param driver Pointer to driver handle
 * @param reading Pointer to store sensor reading
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t csm_v2_read(csm_v2_driver_t* driver, csm_v2_reading_t* reading);

/**
 * @brief Calibrate the sensor with dry and wet values
 * 
 * @param driver Pointer to driver handle
 * @param dry_voltage Voltage reading when sensor is dry
 * @param wet_voltage Voltage reading when sensor is wet
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t csm_v2_calibrate(csm_v2_driver_t* driver, float dry_voltage, float wet_voltage);

/**
 * @brief Initialize GPIO pin for power control
 * 
 * @param driver Pointer to driver handle
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t csm_v2_init_power_pin(csm_v2_driver_t* driver);

/**
 * @brief Enable power to the sensor
 * 
 * @param driver Pointer to driver handle
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t csm_v2_enable_power(csm_v2_driver_t* driver);

/**
 * @brief Disable power to the sensor
 * 
 * @param driver Pointer to driver handle
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t csm_v2_disable_power(csm_v2_driver_t* driver);

/**
 * @brief Get the current power state of the sensor
 * 
 * @param driver Pointer to driver handle
 * @param is_powered Pointer to store power state (true if powered, false if not)
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t csm_v2_get_power_state(csm_v2_driver_t* driver, bool* is_powered);

// MARK: UTILS
float csm_v2_voltage_to_percent(csm_v2_driver_t* driver, float voltage);

#endif // CSM_V2_DRIVER_H
