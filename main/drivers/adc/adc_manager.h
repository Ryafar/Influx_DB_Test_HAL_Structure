/**
 * @file adc_shared.h
 * @brief Shared ADC Manager - Hardware Abstraction Layer
 * 
 * This module provides a shared ADC management system that allows multiple
 * modules to use the same ADC unit and channels efficiently. It uses reference
 * counting to manage the ADC unit lifecycle and provides channel-specific
 * configuration management.
 */

#ifndef ADC_SHARED_H
#define ADC_SHARED_H

#include "adc_manager.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"

// Maximum number of channels per ADC unit
#define ADC_SHARED_MAX_CHANNELS 8

/**
 * @brief Channel configuration for shared ADC
 */
typedef struct {
    adc_channel_t channel;              ///< ADC channel
    adc_bitwidth_t bitwidth;            ///< ADC resolution
    adc_atten_t attenuation;            ///< ADC attenuation
    float reference_voltage;            ///< Reference voltage for calculations
    bool is_configured;                 ///< Channel configuration status
} adc_shared_channel_config_t;

/**
 * @brief Shared ADC unit structure
 */
typedef struct {
    adc_oneshot_unit_handle_t handle;   ///< ESP-IDF ADC handle
    adc_unit_t unit;                    ///< ADC unit
    int ref_count;                      ///< Reference counter
    adc_shared_channel_config_t channels[ADC_SHARED_MAX_CHANNELS]; ///< Channel configurations
    bool is_initialized;                ///< Initialization status
} adc_shared_unit_t;

/**
 * @brief Initialize shared ADC unit
 * 
 * @param unit ADC unit to initialize (ADC_UNIT_1 or ADC_UNIT_2)
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t adc_shared_init(adc_unit_t unit);

/**
 * @brief Deinitialize shared ADC unit
 * 
 * @param unit ADC unit to deinitialize
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t adc_shared_deinit(adc_unit_t unit);

/**
 * @brief Add and configure a channel to shared ADC unit
 * 
 * @param unit ADC unit
 * @param channel ADC channel to add
 * @param bitwidth ADC resolution
 * @param attenuation ADC attenuation
 * @param reference_voltage Reference voltage for voltage calculations
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t adc_shared_add_channel(adc_unit_t unit, adc_channel_t channel, 
                                 adc_bitwidth_t bitwidth, adc_atten_t attenuation, 
                                 float reference_voltage);

/**
 * @brief Read raw value from shared ADC channel
 * 
 * @param unit ADC unit
 * @param channel ADC channel to read
 * @param raw_value Pointer to store raw ADC value
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t adc_shared_read_raw(adc_unit_t unit, adc_channel_t channel, int* raw_value);

/**
 * @brief Read voltage from shared ADC channel
 * 
 * @param unit ADC unit
 * @param channel ADC channel to read
 * @param voltage Pointer to store voltage value
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t adc_shared_read_voltage(adc_unit_t unit, adc_channel_t channel, float* voltage);

/**
 * @brief Remove channel from shared ADC unit
 * 
 * @param unit ADC unit
 * @param channel ADC channel to remove
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t adc_shared_remove_channel(adc_unit_t unit, adc_channel_t channel);

/**
 * @brief Check if shared ADC unit is initialized
 * 
 * @param unit ADC unit to check
 * @return true if initialized, false otherwise
 */
bool adc_shared_is_initialized(adc_unit_t unit);

#endif // ADC_SHARED_H