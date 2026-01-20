/**
 * @file ble_advertiser.h
 * @brief BLE advertising module for AirCube sensor data
 * 
 * This module broadcasts sensor data using BLE advertisements that can be
 * picked up by Home Assistant via a Bluetooth proxy or ESPHome device.
 * 
 * Uses BTHome v2 format for compatibility with Home Assistant.
 */

#ifndef BLE_ADVERTISER_H
#define BLE_ADVERTISER_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Initialize BLE advertising
 * 
 * Sets up the BLE stack and prepares for advertising.
 * Call this once during system initialization.
 * 
 * @param device_name Device name to broadcast (max 20 characters)
 */
void ble_advertiser_init(const char *device_name);

/**
 * @brief Start BLE advertising
 * 
 * Begins broadcasting BLE advertisements. Must be called after
 * ble_advertiser_init().
 */
void ble_advertiser_start(void);

/**
 * @brief Stop BLE advertising
 * 
 * Stops broadcasting BLE advertisements to save power.
 */
void ble_advertiser_stop(void);

/**
 * @brief Update sensor data in BLE advertisement
 * 
 * Updates the advertised sensor values. The new values will be
 * included in the next advertisement packet.
 * 
 * Uses smart update strategy:
 * - Every update: Temperature, Humidity, AQI
 * - Alternating: TVOC or CO2
 * 
 * @param temperature Temperature in degrees Celsius
 * @param humidity Relative humidity percentage (0-100)
 * @param tvoc TVOC in ppb
 * @param co2 CO2 in ppm
 * @param aqi Air Quality Index (0-255)
 */
void ble_advertiser_update_data(float temperature, float humidity,
                                uint16_t tvoc, uint16_t co2,
                                uint8_t aqi);

/**
 * @brief Check if BLE is currently advertising
 * 
 * @return true if advertising is active, false otherwise
 */
bool ble_advertiser_is_active(void);

#endif // BLE_ADVERTISER_H
