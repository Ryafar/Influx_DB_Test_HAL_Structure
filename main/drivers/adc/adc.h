/**
 * @file adc_hal.h
 * @brief Hardware Abstraction Layer for ADC operations
 * 
 * This module provides a clean interface for ADC initialization and configuration,
 * abstracting the ESP-IDF specific ADC operations.
 */

#ifndef ADC_HAL_H
#define ADC_HAL_H

#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"
#include "adc_manager.h"

/**
 * @brief ADC configuration structure
 */
typedef struct {
    adc_unit_t unit;                    ///< ADC unit (ADC_UNIT_1 or ADC_UNIT_2)
    adc_channel_t channel;              ///< ADC channel
    adc_bitwidth_t bitwidth;            ///< ADC resolution
    adc_atten_t attenuation;            ///< ADC attenuation
    float reference_voltage;            ///< Reference voltage for calculations
} adc_hal_config_t;

/**
 * @brief ADC handle structure
 */
typedef struct {
    adc_oneshot_unit_handle_t handle;   ///< ESP-IDF ADC handle
    adc_hal_config_t config;            ///< ADC configuration
} adc_hal_t;

/**
 * @brief Initialize ADC with the given configuration
 * 
 * @param adc_hal Pointer to ADC handle structure
 * @param config ADC configuration
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t adc_hal_init(adc_hal_t* adc_hal, const adc_hal_config_t* config);

/**
 * @brief Deinitialize ADC
 * 
 * @param adc_hal Pointer to ADC handle structure
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t adc_hal_deinit(adc_hal_t* adc_hal);

/**
 * @brief Read raw ADC value
 * 
 * @param adc_hal Pointer to ADC handle structure
 * @param raw_value Pointer to store raw ADC value
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t adc_hal_read_raw(adc_hal_t* adc_hal, int* raw_value);

/**
 * @brief Read ADC value and convert to voltage
 * 
 * @param adc_hal Pointer to ADC handle structure
 * @param voltage Pointer to store voltage value
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t adc_hal_read_voltage(adc_hal_t* adc_hal, float* voltage);

/**
 * @brief Get default ADC configuration
 * 
 * @param config Pointer to configuration structure to fill
 * @param unit ADC unit to use
 * @param channel ADC channel to use
 */
void adc_hal_get_default_config(adc_hal_config_t* config, adc_unit_t unit, adc_channel_t channel);

#endif // ADC_HAL_H