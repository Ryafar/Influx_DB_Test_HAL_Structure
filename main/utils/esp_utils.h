#ifndef ESP_UTILS_H
#define ESP_UTILS_H

#include <stdint.h>


/**
 * @brief Get current timestamp in milliseconds
 * 
 * Returns the current time as milliseconds since Unix epoch (Jan 1, 1970).
 * !: Requires NTP synchronization for accurate real-world time,
 *    otherwise returns time since ESP32 boot.
 * 
 * @return uint64_t Timestamp in milliseconds
 */
uint64_t esp_utils_get_timestamp_ms(void);

/**
 * @brief Get system uptime in milliseconds
 * 
 * Returns the time elapsed since the ESP32 was powered on or reset.
 * This is independent of real-world time synchronization.
 * 
 * @return uint64_t Uptime in milliseconds
 */
uint64_t esp_utils_get_uptime_ms(void);


#endif // ESP_UTILS_H