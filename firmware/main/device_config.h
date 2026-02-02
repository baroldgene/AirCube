/**
 * @file device_config.h
 * @brief Device-specific configuration settings
 * 
 * ============================================================================
 * CHANGE THIS FILE TO CONFIGURE YOUR AIRCUBE DEVICE
 * ============================================================================
 * 
 * This file contains easy-to-modify settings for your AirCube device.
 * When flashing multiple devices, simply change the DEVICE_NAME below.
 */

#ifndef DEVICE_CONFIG_H
#define DEVICE_CONFIG_H

// ============================================================================
// DEVICE IDENTIFICATION
// ============================================================================

/**
 * Device name that appears in Home Assistant and BLE advertisements
 * 
 * IMPORTANT: Change this when flashing different devices!
 * 
 * Examples:
 *   - "AirCube"
 *   - "Living"
 *   - "Bedroom"
 *   - "Kitchen"
 *   - "AirCube1", "AirCube2", etc.
 * 
 * Max length: 10 characters (due to BLE advertisement packet size limit of 31 bytes)
 */
#define DEVICE_NAME "AirCube"

// ============================================================================
// SENSOR CONFIGURATION
// ============================================================================

/**
 * Sensor readout period in milliseconds
 * Default: 1000ms (1 second)
 * 
 * Adjust this to change how often sensors are read and BLE data is updated
 */
#define DEFAULT_SENSOR_PERIOD_MS 1000

// ============================================================================
// LED CONFIGURATION
// ============================================================================

/**
 * Default LED brightness on startup
 * Range: 0-100 (percentage)
 * Default: 50
 */
#define DEFAULT_LED_BRIGHTNESS 50

#endif // DEVICE_CONFIG_H
