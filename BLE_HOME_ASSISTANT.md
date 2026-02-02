# AirCube BLE Integration with Home Assistant

## Overview

The AirCube now broadcasts sensor data via **Bluetooth LE (BLE)** using the **BTHome v2** format, which is automatically discovered and integrated by Home Assistant.

## How It Works

1. **AirCube broadcasts BLE advertisements** every 0.5-1 second
2. **BTHome v2 format** packages all sensor data in standard BLE advertisement packets
3. **Home Assistant automatically discovers** the device via Bluetooth integration
4. **No pairing required** - sensor data is broadcast publicly
5. **No configuration needed** - BTHome integration handles everything automatically

## Sensor Data Broadcasted

The following sensor readings are included in each BLE advertisement:

| Sensor Type | BTHome Object ID | Unit | Description                                    |
| ----------- | ---------------- | ---- | ---------------------------------------------- |
| Temperature | 0x02             | °C   | Air temperature (0.01°C resolution)            |
| Humidity    | 0x03             | %    | Relative humidity (0.01% resolution)           |
| AQI         | 0x09             | -    | Air Quality Index (0-500, UBA scale)           |
| TVOC        | 0x13             | ppb  | Total Volatile Organic Compounds (alternating) |
| CO2         | 0x12             | ppm  | Carbon Dioxide equivalent (alternating)        |

**Note:** TVOC and CO2 alternate in advertisements to keep packet size under 31 bytes. Each sensor updates every ~2 seconds while Temperature, Humidity, and AQI update every second.

## Home Assistant Setup

### Requirements

1. **Home Assistant** with Bluetooth integration enabled
2. **Bluetooth Adapter** within range of AirCube (built-in or USB dongle)
   - OR **ESPHome Bluetooth Proxy** for extended range

### Option 1: Direct Bluetooth (Simple)

If your Home Assistant server has Bluetooth:

1. **Enable Bluetooth Integration**
   - Go to **Settings → Devices & Services**
   - Click **+ Add Integration**
   - Search for **Bluetooth** and add it

1. **Enable BTHome Integration**
   - Go to **Settings → Devices & Services**
   - Click **+ Add Integration**
   - Search for **BTHome** and add it

1. **Power on AirCube**
   - The device will automatically appear as "AirCube" in Home Assistant
   - All sensors will be auto-created

1. **View Sensors**
   - Go to **Settings → Devices & Services → BTHome**
   - Click on **AirCube** device
   - All 5 sensors should be visible (Temperature, Humidity, Count, TVOC, CO2)
   - **Note:** AQI appears as "Count" - you can rename it in Home Assistant to "Air Quality Index"

### Option 2: ESPHome Bluetooth Proxy (Recommended for Range)

Use an ESP32 board as a Bluetooth proxy to extend range:

1. **Flash ESPHome to an ESP32**

   ```yaml
   esphome:
     name: bluetooth-proxy
     friendly_name: Bluetooth Proxy

   esp32:
     board: esp32dev

   wifi:
     ssid: "YourWiFiSSID"
     password: "YourWiFiPassword"

   api:
     encryption:
       key: "your-api-key"

   ota:
     password: "your-ota-password"

   logger:

   esp32_ble_tracker:
     scan_parameters:
       interval: 1100ms
       window: 1100ms
       active: true

   bluetooth_proxy:
     active: true
   ```

1. **Add ESPHome device to Home Assistant**
1. **AirCube will be discovered through the proxy**

### Option 3: Multiple Proxies (Best Coverage)

Deploy multiple ESP32 Bluetooth proxies throughout your home for seamless coverage and hand-off between rooms.

## Troubleshooting

### Device Not Discovered

1. **Check Bluetooth is enabled** on Home Assistant

   ```bash
   bluetoothctl show
   ```

1. **Verify AirCube is advertising**
   - Check serial output for "Advertising started successfully"
   - LED should be pulsing blue during warm-up, then color-coded by AQI

1. **Scan for BLE devices manually**

   ```bash
   bluetoothctl scan on
   # Look for "AirCube" in the list
   ```

1. **Check range** - BLE typically works within 10-30 feet (3-10 meters)

### Sensors Showing "Unavailable"

1. **Check advertisement interval** - Data updates every 1 second
2. **Verify BTHome integration is installed**
3. **Restart Home Assistant** to refresh Bluetooth cache

### Duplicate Devices Appearing

- This can happen if you rename the device or flash multiple times
- **Remove old devices** from Settings → Devices & Services → BTHome

## Customizing Device Name

To change the broadcast name from "AirCube":

1. Edit `/firmware/main/main.c`
2. Find line:
   ```c
   ble_advertiser_init("AirCube");
   ```
3. Change to your desired name (max 20 characters):
   ```c
   ble_advertiser_init("Living Room Air");
   ```
4. Rebuild and flash firmware

## Power Consumption

BLE advertising is very power-efficient:

- **Active broadcasting:** ~15-20 mA
- **Sleep between advertisements:** ~1-5 mA
- **Battery life estimate:** 2-4 weeks on 500mAh battery (typical usage)

## Advanced: Raw BLE Data Format

BTHome v2 advertisement packet structure:

```
Flags: [0x02, 0x01, 0x06]
Complete Local Name: [Length, 0x09, "AirCube"]
Service Data: [Length, 0x16, 0xD2, 0xFC, 0x40, ...sensor data...]
```

Sensor data format (little-endian):

- **0x02** (Temperature): 2 bytes signed int16 (value \* 100)
- **0x03** (Humidity): 2 bytes unsigned int16 (value \* 100)
- **0x13** (TVOC): 2 bytes unsigned int16
- **0x12** (CO2): 2 bytes unsigned int16
- **0x0D** (PM2.5/AQI): 2 bytes unsigned int16
- **0x01** (Battery): 1 byte unsigned int8

## References

- [BTHome v2 Specification](https://bthome.io/)
- [Home Assistant Bluetooth Integration](https://www.home-assistant.io/integrations/bluetooth/)
- [ESPHome Bluetooth Proxy](https://esphome.io/components/bluetooth_proxy.html)
- [ESP32-H2 Datasheet](https://www.espressif.com/en/products/socs/esp32-h2)
