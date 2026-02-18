# ESP-NOW Driver - Usage Guide

## Overview

This ESP-NOW driver provides **reliable data transfer** with automatic fragmentation, retry logic, and CRC validation. It implements all the best practices you mentioned!

## Features Implemented

✅ **Data Definition Layer** - Fixed-size packet structure with headers  
✅ **Fragmentation Logic** - Automatic splitting of large payloads (>200 bytes)  
✅ **Transmission State Machine** - Send and wait with exponential backoff  
✅ **Verification Layer** - CRC16 checksum validation  
✅ **Retry Mechanism** - Configurable retries with exponential backoff  
✅ **RSSI Monitoring** - Track signal strength  
✅ **Encryption Support** - Optional PMK/LMK encryption  

---

## Quick Start

### 1. Initialize WiFi (Required before ESP-NOW)

```c
#include "esp_wifi.h"
#include "nvs_flash.h"

void init_wifi_for_espnow(void) {
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Initialize TCP/IP
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // Initialize WiFi
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    // CRITICAL: Set WiFi to long range mode for better ESP-NOW performance
    ESP_ERROR_CHECK(esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B|WIFI_PROTOCOL_11G|WIFI_PROTOCOL_11N));
}
```

### 2. Initialize ESP-NOW Driver

```c
#include "espnow_driver.h"

void setup_espnow(void) {
    // Configure driver
    espnow_config_t config = {
        .node_id = 1,                    // Unique ID for this node
        .wifi_channel = 1,               // Must match on all devices!
        .enable_encryption = false,      // Set true for encryption
        .send_timeout_ms = 100,
        .max_retries = 3
    };
    
    // Optional: Set encryption key
    if (config.enable_encryption) {
        const char *pmk = "MySecretKey12345";
        memcpy(config.pmk, pmk, 16);
    }
    
    ESP_ERROR_CHECK(espnow_driver_init(&config));
}
```

### 3. Add Peer (Receiver's MAC Address)

```c
void add_receiver_peer(void) {
    espnow_peer_t peer = {
        .mac_addr = {0x24, 0x6F, 0x28, 0xAB, 0xCD, 0xEF}, // Replace with actual MAC
        .channel = 1,                    // Must match driver config
        .encrypt = false,
        .rssi = 0
    };
    
    // Optional: Set encryption key for this peer
    if (peer.encrypt) {
        const char *lmk = "PeerSecretKey123";
        memcpy(peer.lmk, lmk, 16);
    }
    
    ESP_ERROR_CHECK(espnow_driver_add_peer(&peer));
}
```

**How to find MAC address:**
```c
uint8_t mac[6];
esp_wifi_get_mac(WIFI_IF_STA, mac);
ESP_LOGI("MAC", "My MAC: %02X:%02X:%02X:%02X:%02X:%02X", 
         mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
```

### 4. Send Data (Sender Side)

```c
#include "espnow_driver.h"

void send_sensor_data(void) {
    // Example: Send 60 sensor readings (480 bytes)
    typedef struct {
        uint32_t timestamp;
        float temperature;
    } sensor_reading_t;
    
    sensor_reading_t readings[60];
    
    // Fill with data
    for (int i = 0; i < 60; i++) {
        readings[i].timestamp = esp_timer_get_time() / 1000;
        readings[i].temperature = 25.5 + i * 0.1;
    }
    
    // Destination MAC (receiver)
    uint8_t receiver_mac[6] = {0x24, 0x6F, 0x28, 0xAB, 0xCD, 0xEF};
    
    // Send - driver automatically fragments into multiple packets!
    esp_err_t err = espnow_driver_send(receiver_mac, 
                                       (uint8_t*)readings, 
                                       sizeof(readings));
    
    if (err == ESP_OK) {
        ESP_LOGI("SENDER", "All data sent successfully!");
    } else {
        ESP_LOGE("SENDER", "Send failed: %s", esp_err_to_name(err));
    }
}
```

### 5. Receive Data (Receiver Side)

```c
// Reassembly buffer for fragmented data
typedef struct {
    uint8_t buffer[8192];
    uint16_t current_sequence;
    uint8_t chunks_received;
    uint8_t total_chunks;
} reassembly_ctx_t;

static reassembly_ctx_t rx_ctx = {0};

// Receive callback
void on_data_received(const uint8_t *src_mac, const uint8_t *data, size_t len, int8_t rssi) {
    char mac_str[18];
    espnow_mac_to_str(src_mac, mac_str);
    
    ESP_LOGI("RECEIVER", "Received %d bytes from %s (RSSI: %d dBm)", len, mac_str, rssi);
    
    // Process the received data chunk
    // For simple single-packet data, just use it directly
    // For fragmented data, you need reassembly logic (see below)
    
    // Example: Cast to your sensor data structure
    sensor_reading_t *readings = (sensor_reading_t *)data;
    int count = len / sizeof(sensor_reading_t);
    
    for (int i = 0; i < count; i++) {
        ESP_LOGI("RECEIVER", "Reading %d: Temp = %.2f°C", 
                 i, readings[i].temperature);
    }
}

void setup_receiver(void) {
    // Initialize driver (same as sender)
    espnow_config_t config = {
        .node_id = 2,
        .wifi_channel = 1,
        .enable_encryption = false,
        .send_timeout_ms = 100,
        .max_retries = 3
    };
    
    ESP_ERROR_CHECK(espnow_driver_init(&config));
    
    // Register receive callback
    espnow_driver_register_recv_cb(on_data_received);
    
    // Add sender as peer (for two-way communication)
    espnow_peer_t peer = {
        .mac_addr = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}, // Sender's MAC
        .channel = 1,
        .encrypt = false
    };
    ESP_ERROR_CHECK(espnow_driver_add_peer(&peer));
}
```

---

## Advanced Features

### Deep Sleep with RTC Memory Persistence

For your 60-measurement scenario:

```c
// Store measurements in RTC memory (survives deep sleep)
RTC_DATA_ATTR static sensor_reading_t rtc_readings[60];
RTC_DATA_ATTR static uint8_t rtc_reading_count = 0;

void measure_and_accumulate(void) {
    // Take measurement
    float temp = read_temperature_sensor();
    
    rtc_readings[rtc_reading_count].timestamp = esp_timer_get_time() / 1000;
    rtc_readings[rtc_reading_count].temperature = temp;
    rtc_reading_count++;
    
    if (rtc_reading_count >= 60) {
        // Send all 60 readings
        uint8_t receiver_mac[6] = {0x24, 0x6F, 0x28, 0xAB, 0xCD, 0xEF};
        espnow_driver_send(receiver_mac, (uint8_t*)rtc_readings, sizeof(rtc_readings));
        
        // Reset counter
        rtc_reading_count = 0;
    }
    
    // Go to deep sleep for 1 minute
    esp_sleep_enable_timer_wakeup(60 * 1000000ULL); // 60 seconds
    esp_deep_sleep_start();
}
```

### Broadcast to Multiple Receivers

```c
void broadcast_alert(void) {
    const char *message = "Sensor threshold exceeded!";
    
    // Sends to all registered peers
    espnow_driver_broadcast((uint8_t*)message, strlen(message));
}
```

### Check Send Status

```c
void send_with_monitoring(void) {
    uint8_t receiver_mac[6] = {0x24, 0x6F, 0x28, 0xAB, 0xCD, 0xEF};
    uint8_t data[] = "Test data";
    
    esp_err_t err = espnow_driver_send(receiver_mac, data, sizeof(data));
    
    if (err == ESP_OK) {
        // Wait for completion
        err = espnow_driver_wait_send_done(5000); // 5 second timeout
        if (err == ESP_OK) {
            ESP_LOGI("SEND", "Confirmed delivery!");
        }
    }
    
    // Check current state
    espnow_send_state_t state = espnow_driver_get_send_state();
    switch (state) {
        case ESPNOW_STATE_IDLE:
            ESP_LOGI("STATE", "Idle - ready to send");
            break;
        case ESPNOW_STATE_SENDING:
            ESP_LOGI("STATE", "Currently sending...");
            break;
        case ESPNOW_STATE_FAILED:
            ESP_LOGE("STATE", "Last send failed!");
            break;
    }
}
```

---

## Complete Example: Sensor Node

```c
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "espnow_driver.h"
#include "esp_log.h"

#define RECEIVER_MAC {0x24, 0x6F, 0x28, 0xAB, 0xCD, 0xEF}

static const char *TAG = "SENSOR_NODE";

void sensor_task(void *pvParameters) {
    while (1) {
        // Read sensor
        float temperature = read_temperature();
        float humidity = read_humidity();
        
        // Prepare data packet
        typedef struct {
            float temp;
            float hum;
            uint32_t timestamp;
        } sensor_packet_t;
        
        sensor_packet_t packet = {
            .temp = temperature,
            .hum = humidity,
            .timestamp = esp_timer_get_time() / 1000
        };
        
        // Send
        uint8_t receiver[6] = RECEIVER_MAC;
        esp_err_t err = espnow_driver_send(receiver, (uint8_t*)&packet, sizeof(packet));
        
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Sent: Temp=%.2f°C, Hum=%.2f%%", temperature, humidity);
        } else {
            ESP_LOGE(TAG, "Send failed!");
        }
        
        vTaskDelay(pdMS_TO_TICKS(60000)); // Send every minute
    }
}

void app_main(void) {
    // Initialize WiFi
    init_wifi_for_espnow();
    
    // Initialize ESP-NOW
    espnow_config_t config = {
        .node_id = 1,
        .wifi_channel = 1,
        .enable_encryption = false,
        .send_timeout_ms = 100,
        .max_retries = 3
    };
    ESP_ERROR_CHECK(espnow_driver_init(&config));
    
    // Add receiver peer
    espnow_peer_t peer = {
        .mac_addr = RECEIVER_MAC,
        .channel = 1,
        .encrypt = false
    };
    ESP_ERROR_CHECK(espnow_driver_add_peer(&peer));
    
    // Start sensor task
    xTaskCreate(sensor_task, "sensor_task", 4096, NULL, 5, NULL);
}
```

---

## Comparison with Your Requirements

| Your Requirement | Implementation Status |
|-----------------|----------------------|
| ✅ Fixed-size struct with metadata | `espnow_packet_header_t` includes node_id, sequence, chunk info |
| ✅ RTC_DATA_ATTR support | Example provided above for deep sleep |
| ✅ Fragmentation for >250 bytes | Automatic chunking into 200-byte payloads |
| ✅ Send and Wait state machine | `send_packet_with_retry()` with callback handling |
| ✅ Retry with exponential backoff | Implemented with `10 * (1 << retry)` delay |
| ✅ Checksum verification | CRC16 validation on every packet |
| ✅ MAC pairing | `espnow_driver_add_peer()` |
| ✅ Channel synchronization | Configurable via `wifi_channel` |
| ✅ Packed structs | All structs use `__attribute__((packed))` |

---

## Troubleshooting

**Problem:** Packets not received  
**Solution:** Ensure WiFi channel matches on both devices and peer is added

**Problem:** Send fails immediately  
**Solution:** Check that WiFi is initialized and started before ESP-NOW init

**Problem:** CRC errors  
**Solution:** WiFi interference or distance too far - check RSSI with `espnow_driver_get_last_rssi()`

**Problem:** Memory issues with large data  
**Solution:** Driver supports up to 32 fragments (6.4KB). For larger data, send multiple batches

---

## Next Steps

1. Find your ESP32's MAC address (run code above)
2. Update `RECEIVER_MAC` in your code
3. Flash one ESP32 as sender, one as receiver
4. Test with small data first, then scale up!

Need help integrating this with your existing sensor code? Let me know! 🚀
