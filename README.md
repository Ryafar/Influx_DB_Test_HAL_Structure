# ESP32-C6 Environment Monitor with InfluxDB

A power-efficient ESP32-C6 application that monitors temperature and humidity using an AHT20 sensor, transmits data to InfluxDB via WiFi, and uses deep sleep to minimize power consumption.

## Features

- 🌡️ **AHT20 Temperature & Humidity Sensing** via I2C
- 📡 **WiFi Connectivity** with automatic reconnection
- 📊 **InfluxDB Integration** for time-series data storage
- ⚡ **Deep Sleep Power Management** for battery operation
- 🔄 **Configurable Wake Cycles** and measurement intervals
- 🕐 **Optional NTP Time Sync** (can use server timestamps)
- 📝 **Async Data Transmission** with queue management

## Hardware Requirements

- **ESP32-C6** development board
- **AHT20** temperature/humidity sensor
- I2C connections:
  - SDA: GPIO7
  - SCL: GPIO9
  - Power: 3.3V
  - Ground: GND

## Project Structure

```
├── main/
│   ├── main.c                          # Entry point, lifecycle management
│   ├── config/
│   │   ├── esp32-config.h              # Hardware pins, intervals, feature flags
│   │   └── credentials.h               # WiFi & InfluxDB credentials (git-ignored)
│   └── application/
│       ├── env_monitor_app.c/h         # Environment monitoring task
│       ├── influx_sender.c/h           # Async InfluxDB sender with queue
│       └── [legacy apps...]            # Soil/battery monitoring (disabled)
│
├── components/
│   ├── drivers/
│   │   ├── sensors/aht20/              # AHT20 I2C driver
│   │   ├── wifi/wifi_manager/          # WiFi connection management
│   │   ├── http/http_client/           # HTTP client wrapper
│   │   └── influxdb/influxdb_client/   # InfluxDB Line Protocol formatter
│   └── utils/
│       ├── esp_utils.c/h               # Timestamp & MAC address helpers
│       └── ntp_time.c/h                # NTP time synchronization
│
├── sdkconfig.defaults                  # Custom ESP-IDF configuration
├── CMakeLists.txt                      # Build configuration
└── README.md                           # This file
```

## Setup Instructions

### 1. Prerequisites

- [ESP-IDF v5.5.1](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/) or later
- Python 3.11+
- Git

### 2. Clone and Configure

```bash
git clone <repository-url>
cd Influx_DB_Test_HAL_Structure
```

### 3. Configure Credentials

Create `main/config/credentials.h` with your WiFi and InfluxDB settings:

```c
#ifndef CREDENTIALS_H
#define CREDENTIALS_H

// WiFi Configuration
#define WIFI_SSID               "your_wifi_ssid"
#define WIFI_PASSWORD           "your_wifi_password"
#define WIFI_MAXIMUM_RETRY      5
#define WIFI_CONNECT_TIMEOUT_MS 10000

// InfluxDB Configuration
#define INFLUXDB_URL            "http://your_server:8086"
#define INFLUXDB_TOKEN          "your_influx_token"
#define INFLUXDB_ORG            "your_organization"
#define INFLUXDB_BUCKET         "your_bucket"

// Device Identification
#define DEVICE_NAME             "esp32_sensor"
#define DEVICE_LOCATION         "location"

#endif
```

### 4. Build and Flash

```bash
# Set target (first time only)
idf.py set-target esp32c6

# Build the project
idf.py build

# Flash to device
idf.py -p COM3 flash

# Monitor output
idf.py -p COM3 monitor
```

Replace `COM3` with your actual serial port.

## Configuration

### Main Settings (`main/config/esp32-config.h`)

**I2C Configuration:**
```c
#define I2C_SDA_PIN              GPIO_NUM_7
#define I2C_SCL_PIN              GPIO_NUM_9
#define I2C_FREQ_HZ              100000
```

**Deep Sleep:**
```c
#define DEEP_SLEEP_ENABLED              1      // Enable deep sleep
#define DEEP_SLEEP_DURATION_SECONDS     10     // Sleep duration
```

**Measurements:**
```c
#define ENV_MEASUREMENT_INTERVAL_MS     10000  // Interval between measurements
#define CONFIG_ENV_MEASUREMENTS_PER_CYCLE 1    // Measurements per wake cycle
```

**NTP Time Sync:**
```c
#define NTP_ENABLED                     0      // 0 = use server time, 1 = use NTP
```

### Kconfig Options

Use `idf.py menuconfig` or edit `sdkconfig.defaults`:
- Partition table: `CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y` (for large builds)

## How It Works

### Execution Flow

1. **Wake Up** - ESP32 wakes from deep sleep or boots
2. **Initialize**
   - Connect to WiFi
   - Initialize AHT20 sensor
   - Setup InfluxDB sender queue
   - Optional: Sync NTP time
3. **Measure**
   - Read temperature & humidity from AHT20
   - Take configured number of measurements
4. **Transmit**
   - Queue data to InfluxDB via HTTP POST
   - Wait for transmission to complete
5. **Sleep**
   - Clean up resources
   - Enter deep sleep for configured duration
   - Repeat cycle

### Power Management

The device uses ESP32 deep sleep mode to minimize power consumption:
- **Active time**: ~5-30 seconds (WiFi connection + measurements + transmission)
- **Sleep time**: Configurable (default: 10 seconds)
- **Typical cycle**: Wake → Measure → Send → Sleep (10s) → Repeat

### Data Format

Data is sent to InfluxDB in Line Protocol format:
```
environment,device=<device_id> temperature=<value>,humidity=<value> <timestamp_ns>
```

## Troubleshooting

### Build Failures

**"Cannot find source file: soil_project_main.c"**
- Fixed: Ensure `main/CMakeLists.txt` references `main.c`

**"app partition is too small"**
- Solution: Use `CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y` in `sdkconfig.defaults`

**"credentials.h not found"**
- Create `main/config/credentials.h` with your WiFi and InfluxDB credentials

### Runtime Issues

**WiFi won't connect:**
- Check credentials in `credentials.h`
- Verify signal strength
- Check router settings (2.4GHz band required)

**Sensor not reading:**
- Verify I2C wiring (SDA=GPIO7, SCL=GPIO9)
- Check AHT20 power supply (3.3V)
- Run `idf.py monitor` to see error messages

**InfluxDB data not appearing:**
- Verify INFLUXDB_URL, INFLUXDB_TOKEN, INFLUXDB_ORG, and INFLUXDB_BUCKET in `credentials.h`
- Check InfluxDB server is accessible from ESP32's network
- Monitor serial output for HTTP response codes

## Development

### Adding Custom Sensors

1. Create driver in `components/drivers/sensors/`
2. Add monitoring app in `main/application/`
3. Update `main.c` to call your app
4. Update CMakeLists.txt with new source files

### Adjusting Logging

Edit `main/config/esp32-config.h`:
```c
#define ENV_ENABLE_DETAILED_LOGGING    1  // Detailed logs
#define CONFIG_ENV_ENABLE_LOGGING      1  // Basic logs
```

## License

[Specify your license here]

## Author

[Your name/organization]
