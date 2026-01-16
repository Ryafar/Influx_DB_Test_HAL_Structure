#include "esp_utils.h"
#include <sys/time.h>
#include <stdint.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"

static const char *TAG = "ESP_UTILS";

uint64_t esp_utils_get_timestamp_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)(tv.tv_sec) * 1000 + (uint64_t)(tv.tv_usec) / 1000;
}

uint64_t esp_utils_get_uptime_ms(void)
{
    return esp_timer_get_time() / 1000; // Convert microseconds to milliseconds
}