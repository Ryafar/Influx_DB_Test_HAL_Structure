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
│   ├── Kconfig.projbuild               # Optional menuconfig (ENV settings only)
│   ├── config/
│   │   ├── esp32-config.h              # All hardware/feature config (feature toggles!)
│   │   └── credentials.h               # WiFi & InfluxDB credentials (git-ignored)
│   └── application/
│       ├── env_monitor_app.c/h         # Environment monitoring task (AHT20)
│       ├── battery_monitor_task.c/h    # Battery voltage monitoring (toggle in config)
│       ├── soil_monitor_app.c/h        # Soil moisture monitoring (toggle in config)
│       └── influx_sender.c/h           # Async InfluxDB sender with queue
│
├── components/
│   ├── drivers/
│   │   ├── sensors/aht20/              # AHT20 I2C driver
│   │   ├── csm_v2_driver/              # Capacitive soil moisture sensor driver
│   │   ├── adc/                        # Shared ADC manager (multi-channel support)
│   │   ├── wifi/wifi_manager/          # WiFi connection management
│   │   ├── http/http_client/           # HTTP client wrapper
│   │   └── influxdb/influxdb_client/   # InfluxDB Line Protocol formatter
│   └── utils/
│       ├── esp_utils.c/h               # Timestamp & MAC address helpers
│       └── ntp_time.c/h                # NTP time synchronization
│
├── sdkconfig.defaults                  # Default ESP-IDF configuration
├── CMakeLists.txt                      # Root build configuration
└── README.md                           # This file
```

**Simple Configuration:**
- Edit `main/config/esp32-config.h` to enable/disable features and configure hardware
- All sources are always compiled, `#if ENABLE_*` decides what runs
- Optional: Use `idf.py menuconfig` for ENV sleep/measurement settings

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

### Simple Header-Based Configuration

All project settings are in **one place**: `main/config/esp32-config.h`

#### Enable/Disable Features

At the top of `esp32-config.h`, toggle features on/off:

```c
// ============================================================================
// Feature Toggles - Enable/Disable Monitoring Modules
// ============================================================================

#define ENABLE_ENV_MONITOR      1    // AHT20 temperature/humidity sensor
#define ENABLE_BATTERY_MONITOR  0    // Battery voltage monitoring via ADC
#define ENABLE_SOIL_MONITOR     0    // Soil moisture monitoring via ADC
```

**That's it!** Set to `1` to enable, `0` to disable. No menuconfig needed.

#### Configure Hardware Settings

Each enabled feature has its own configuration block in the same file:

**Battery Monitor (when `ENABLE_BATTERY_MONITOR = 1`):**
```c
#define BATTERY_ADC_UNIT                        ADC_UNIT_1
#define BATTERY_ADC_CHANNEL                     ADC_CHANNEL_0      // GPIO0 on ESP32-C6
#define BATTERY_MONITOR_VOLTAGE_SCALE_FACTOR    2.0f              // Voltage divider
#define BATTERY_MONITOR_LOW_VOLTAGE_THRESHOLD   3.2f              // Low battery alert
```

**Soil Monitor (when `ENABLE_SOIL_MONITOR = 1`):**
```c
#define SOIL_ADC_UNIT               ADC_UNIT_1
#define SOIL_ADC_CHANNEL            ADC_CHANNEL_0
#define SOIL_SENSOR_POWER_PIN       GPIO_NUM_19    // GPIO to power sensor
#define SOIL_DRY_VOLTAGE_DEFAULT    3.0f
#define SOIL_WET_VOLTAGE_DEFAULT    1.0f
```

**Environment Monitor (always on):**
```c
#define I2C_SDA_PIN              GPIO_NUM_7
#define I2C_SCL_PIN              GPIO_NUM_9
#define I2C_FREQ_HZ              100000
```

**WiFi & InfluxDB:**
```c
#define USE_INFLUXDB            1
#define INFLUXDB_SERVER         "sensors.example.org"
#define INFLUXDB_PORT           443
#define WIFI_MAX_RETRY          15
```

**Deep Sleep:**
```c
#define DEEP_SLEEP_ENABLED              1
#define DEEP_SLEEP_DURATION_SECONDS     10      // Sleep between cycles
```

### Optional: Menuconfig for ENV Settings

For environment monitoring, you can optionally use `idf.py menuconfig`:

```bash
idf.py menuconfig
# → Environment Monitor Configuration
# → Adjust sleep duration, measurements per cycle, logging
```

These settings override the defaults in code:
- `CONFIG_ENV_SLEEP_SECONDS` (default: 10)
- `CONFIG_ENV_MEASUREMENTS_PER_CYCLE` (default: 1)
- `CONFIG_ENV_ENABLE_LOGGING` (default: yes)

**But you don't have to use menuconfig** – the defaults work fine.

### Configuration Workflow

**To enable Battery Monitor:**

1. Edit `main/config/esp32-config.h`:
   ```c
   #define ENABLE_BATTERY_MONITOR  1    // Changed from 0 to 1
   ```

2. Configure ADC channel (if needed):
   ```c
   #define BATTERY_ADC_CHANNEL     ADC_CHANNEL_0  // GPIO0
   ```

3. Build and flash:
   ```bash
   idf.py build flash monitor
   ```

That's it! No menuconfig, no separate files, just edit one header and rebuild.

### Quick Reference

| Setting | File | Type |
|---------|------|------|
| **Enable Features** | `esp32-config.h` | `#define ENABLE_* 0/1` |
| **Hardware Pins** | `esp32-config.h` | `#define *_GPIO_PIN` |
| **ADC Config** | `esp32-config.h` | `#define *_ADC_*` |
| **WiFi/InfluxDB** | `credentials.h` | Server URLs, tokens |
| **Sleep Duration** | `esp32-config.h` or menuconfig | Deep sleep seconds |

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
