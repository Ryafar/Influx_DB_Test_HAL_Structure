#include "battery_monitor_main.h"
#include "esp_log.h"



static const char *TAG = "BATTERY_MONITOR_TESTING";

void app_main(void) {
    ESP_LOGI(TAG, "Starting Battery Monitoring Testing");

    battery_monitor_start();

    // Cleanup
    
}
