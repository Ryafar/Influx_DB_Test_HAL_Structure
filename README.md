# ESP32-C6 Environment Monitor with InfluxDB

A power-efficient ESP32-C6 application that monitors temperature and humidity using an AHT20 sensor, transmits data to InfluxDB via WiFi, and uses deep sleep to minimize power consumption.

## Features

- 🌡️ **AHT20 Temperature & Humidity Sensing** via I2C
- � **Battery Voltage Monitoring** with ADC calibration and voltage divider support
- 🌱 **Soil Moisture Monitoring** with capacitive sensor and power management- 📺 **E-Paper Display** (2.13" DEPG0213BN, 122x250 pixels) with SSD1680 controller- 📡 **WiFi Connectivity** with automatic reconnection
- 📊 **InfluxDB Integration** for time-series data storage (HTTPS support)
- ⚡ **Deep Sleep Power Management** for battery operation
- 🔄 **Configurable Wake Cycles** and measurement intervals
- 🕐 **Optional NTP Time Sync** (can use server timestamps)
- 📝 **Async Data Transmission** with queue management
- 📏 **ESP32-C6 eFuse ADC Calibration** with curve fitting for accurate voltage readings
- 📊 **64-Sample Multisampling** for noise reduction on ADC channels
- 🏗️ **Modular Architecture** with shared WiFi and InfluxDB instances

## Hardware Requirements

- **ESP32-C6** development board (e.g., DFRobot FireBeetle 2 ESP32-C6)
- **AHT20** temperature/humidity sensor (optional, enabled via config)
- **Capacitive Soil Moisture Sensor** (optional, enabled via config)
- **Battery voltage divider** (optional, enabled via config)
- **2.13" E-Paper Display** - DEPG0213BN (WeAct Studio, 122x250, black/white, optional)

### Pin Connections

**I2C (AHT20):**
  - SDA: GPIO19
  - SCL: GPIO20
  - Power: 3.3V
  - Ground: GND

**ADC Sensors:**
  - Battery Voltage: GPIO0 (ADC1 Channel 0) with 2:1 voltage divider
  - Soil Moisture: GPIO2 (ADC1 Channel 2)
  - Soil Power Control: GPIO18 (powers sensor on/off)

**E-Paper Display (SPI):**
  - MOSI: GPIO6
  - SCK: GPIO4
  - CS: GPIO7
  - DC (Data/Command): GPIO15
  - RST (Reset): GPIO5
  - BUSY: GPIO3
  - Power Control: GPIO8 (HIGH = ON)

**Note:** GPIO0 is a strapping pin on ESP32-C6 and may have a slight voltage offset (~1V) even when grounded. The current configuration uses GPIO0 for battery monitoring (non-critical) and GPIO2/GPIO18 for soil sensing to avoid conflicts with e-paper SPI pins.

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
│       ├── epaper_display_app.c/h      # E-paper display application (sensor data UI)
│       └── influx_sender.c/h           # Async InfluxDB sender with queue
│
├── components/
│   ├── drivers/
│   │   ├── sensors/aht20/              # AHT20 I2C driver
│   │   ├── csm_v2_driver/              # Capacitive soil moisture sensor driver
│   │   ├── adc/                        # Shared ADC manager (multi-channel support)
│   │   ├── epaper/                     # E-paper display driver (SSD1680, SPI)
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

#define ENABLE_WIFI             1    // WiFi connectivity (needed for InfluxDB)
#define ENABLE_ENV_MONITOR      1    // AHT20 temperature/humidity sensor
#define ENABLE_BATTERY_MONITOR  1    // Battery voltage monitoring via ADC
#define ENABLE_SOIL_MONITOR     1    // Soil moisture monitoring via ADC
#define ENABLE_EPAPER_DISPLAY   1    // WeAct ePaper display (SPI)
```

**That's it!** Set to `1` to enable, `0` to disable. No menuconfig needed.

#### Configure Hardware Settings

Each enabled feature has its own configuration block in the same file:

**Battery Monitor (when `ENABLE_BATTERY_MONITOR = 1`):**
```c
#define BATTERY_ADC_UNIT                        ADC_UNIT_1
#define BATTERY_ADC_CHANNEL                     ADC_CHANNEL_0      // GPIO0 on ESP32-C6
#define BATTERY_ADC_ATTENUATION                 ADC_ATTEN_DB_12    // 0-3.3V range
#define BATTERY_MONITOR_VOLTAGE_SCALE_FACTOR    2.0f               // Voltage divider (2x)
#define BATTERY_MONITOR_LOW_VOLTAGE_THRESHOLD   3.2f               // Low battery alert
```

**ADC Calibration:**
- Uses ESP32-C6 factory eFuse calibration data (curve fitting scheme)
- 64-sample averaging for noise reduction
- Automatic channel-specific calibration on initialization

**Soil Monitor (when `ENABLE_SOIL_MONITOR = 1`):**
```c
#define SOIL_ADC_UNIT               ADC_UNIT_1
#define SOIL_ADC_CHANNEL            ADC_CHANNEL_2      // GPIO2 on ESP32-C6
#define SOIL_ADC_ATTENUATION        ADC_ATTEN_DB_12    // 0-3.3V range
#define SOIL_SENSOR_POWER_PIN       GPIO_NUM_18        // Power control (on/off)
#define SOIL_DRY_VOLTAGE_DEFAULT    3.0f               // Calibration: dry soil
#define SOIL_WET_VOLTAGE_DEFAULT    1.0f               // Calibration: wet soil
```

**Features:**
- Power control via GPIO19 (sensor powered off between readings)
- Same eFuse calibration and multisampling as battery monitor
- Automatic moisture percentage calculation from voltage

**Environment Monitor:**
```c
#define I2C_SDA_PIN              GPIO_NUM_19
#define I2C_SCL_PIN              GPIO_NUM_20
#define I2C_FREQ_HZ              100000
```

**E-Paper Display (when `ENABLE_EPAPER_DISPLAY = 1`):**
```c
#define EPAPER_SPI_HOST          SPI2_HOST
#define EPAPER_SPI_MOSI_PIN      GPIO_NUM_6
#define EPAPER_SPI_SCK_PIN       GPIO_NUM_4
#define EPAPER_SPI_CS_PIN        GPIO_NUM_7
#define EPAPER_SPI_DC_PIN        GPIO_NUM_15     // Data/Command
#define EPAPER_SPI_RST_PIN       GPIO_NUM_5      // Reset
#define EPAPER_SPI_BUSY_PIN      GPIO_NUM_3      // Busy signal
#define EPAPER_POWER_PIN         GPIO_NUM_8      // Power control
#define EPAPER_MODEL_213_BN      1               // 2.13" 122x250 B/W
#define EPAPER_ROTATION          0               // Display orientation
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
2. **Initialize** (shared resources, once per boot)
   - Connect to WiFi (shared instance)
   - Setup InfluxDB sender queue (shared instance)
   - Initialize enabled sensors (ADC manager, AHT20, etc.)
   - Optional: Sync NTP time
3. **Measure** (runs for each enabled sensor)
   - **Battery Monitor**: Read voltage with 64-sample averaging, apply voltage divider scaling
   - **Soil Monitor**: Power on sensor → wait → read moisture with calibration → power off
   - **Environment Monitor**: Read temperature & humidity from AHT20
4. **Transmit**
   - All sensors queue data to shared InfluxDB sender
   - Async transmission via HTTPS with TLS certificate validation
   - Wait for all transmissions to complete (HTTP 204 = success)
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

**Environment (AHT20):**
```
environment,device=ENV_<MAC> temperature=<value>,humidity=<value> <timestamp_ns>
```

**Battery:**
```
battery,device=BATT_<MAC> voltage=<value> <timestamp_ns>
```

**Soil Moisture:**
```
soil_moisture,device=SOIL_<MAC> voltage=<value>,moisture_percent=<value>,raw_adc=<value> <timestamp_ns>
```

All measurements are sent to the same InfluxDB bucket (`ESP32Data`) but use different measurement names for easy querying.

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

**ADC readings incorrect:**
- Ensure using `ADC_ATTEN_DB_12` (not deprecated `ADC_ATTEN_DB_11`)
- Verify eFuse calibration is enabled (check logs for "Calibration enabled")
- Check voltage divider values match configuration
- GPIO0 may have ~1V offset due to strapping pin pull-ups
- Multisampling (64 samples) helps reduce noise

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
