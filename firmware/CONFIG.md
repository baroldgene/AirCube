# AirCube WiFi/MQTT Configuration

This firmware supports compile-time configuration to avoid needing serial setup.

## Quick Start

### Option 1: Configure Before Flashing (Recommended)

1. **Edit `main/user_config.h`** with your settings:

```c
// Enable compile-time WiFi configuration
#define USE_COMPILE_TIME_WIFI       1
#define DEFAULT_WIFI_SSID           "YourActualWiFiName"
#define DEFAULT_WIFI_PASSWORD       "YourActualPassword"

// Enable compile-time MQTT configuration  
#define USE_COMPILE_TIME_MQTT       1
#define DEFAULT_MQTT_BROKER         "192.168.1.100"  // Your MQTT broker IP
#define DEFAULT_MQTT_PORT           1883
#define DEFAULT_MQTT_USERNAME       ""  // Leave empty if not using auth
#define DEFAULT_MQTT_PASSWORD       ""

// Device name for Home Assistant
#define DEFAULT_DEVICE_NAME         "Living Room AirCube"

// Auto-connect on boot
#define AUTO_CONNECT_ON_BOOT        1
```

2. **Build and flash:**
```bash
cd firmware
. "$IDF_PATH/export.sh"
idf.py build
idf.py -p /dev/ttyUSB0 flash  # Adjust port: macOS uses /dev/cu.*, Linux uses /dev/ttyUSB* or /dev/ttyACM*
```

3. **Done!** The device will automatically connect to WiFi and MQTT on boot.

### Option 2: Configure via Serial (No WiFi/MQTT pre-configuration)

If you prefer to configure via USB-C serial commands:

1. Set flags to `0` in `user_config.h`:
```c
#define USE_COMPILE_TIME_WIFI       0
#define USE_COMPILE_TIME_MQTT       0
#define AUTO_CONNECT_ON_BOOT        0
```

2. Flash the firmware

3. Connect via serial and send JSON commands:
```json
{"cmd":"set_wifi","ssid":"YourNetwork","password":"YourPassword"}
{"cmd":"set_mqtt","broker":"192.168.1.100","port":1883}
{"cmd":"wifi_connect"}
```

## Security Note

**`main/user_config.h` is gitignored** - your WiFi/MQTT credentials won't be committed to git.

The template file `main/user_config.h.template` is safe to commit (contains no real credentials).

## Monitoring

To see device output:
```bash
idf.py -p /dev/ttyUSB0 monitor  # Adjust port: macOS uses /dev/cu.*, Linux uses /dev/ttyUSB* or /dev/ttyACM*
```

Press `Ctrl+]` to exit monitor.

## Home Assistant Integration

Once connected, the device will:
- Auto-discover in Home Assistant via MQTT
- Create 6 sensors (Temperature, Humidity, TVOC, CO2, AQI, WiFi RSSI)
- Accept LED control commands via MQTT topic: `aircube/{device_name}/led/set`

Check **Settings → Devices & Services → MQTT** in Home Assistant.
