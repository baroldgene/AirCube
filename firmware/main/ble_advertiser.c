/**
 * @file ble_advertiser.c
 * @brief BLE advertising implementation using BTHome v2 format
 * 
 * This broadcasts sensor data in BTHome v2 format for automatic discovery
 * by Home Assistant via Bluetooth proxies.
 */

#include "ble_advertiser.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_bt_main.h"
#include "nvs_flash.h"
#include <string.h>

static const char *TAG = "ble_adv";

// BTHome v2 service UUID: 0xFCD2
#define BTHOME_SERVICE_UUID 0xFCD2

// BTHome v2 object IDs
#define BTHOME_OBJ_TEMPERATURE   0x02  // Temperature in 0.01°C
#define BTHOME_OBJ_HUMIDITY      0x03  // Humidity in 0.01%
#define BTHOME_OBJ_TVOC          0x13  // TVOC in µg/m³
#define BTHOME_OBJ_CO2           0x12  // CO2 in ppm
#define BTHOME_OBJ_COUNT         0x09  // Generic count (uint8) - used for AQI

static bool s_advertising = false;
static char s_device_name[11] = "AirCube";  // Max 10 chars + null terminator to fit in 31-byte BLE packet
static uint8_t s_packet_toggle = 0;  // Alternates between packet types for smart updates

// Sensor data storage
static struct {
    float temperature;
    float humidity;
    uint16_t tvoc;
    uint16_t co2;
    uint8_t aqi;
} s_sensor_data = {0};

// BLE advertising parameters
static esp_ble_adv_params_t adv_params = {
    .adv_int_min = 0x0320,  // 500ms
    .adv_int_max = 0x0640,  // 1000ms
    .adv_type = ADV_TYPE_NONCONN_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

/**
 * @brief Build BTHome v2 advertisement packet with smart update strategy
 * 
 * Packet A (every update): Temp, Humidity, AQI - high priority environmental data
 * Packet B (alternating): TVOC, CO2 - secondary air quality metrics
 * This keeps packet size under 31 bytes while providing all sensor data
 */
static void build_advertisement_data(uint8_t *adv_data, uint8_t *adv_data_len)
{
    uint8_t idx = 0;
    
    // Flags (3 bytes)
    adv_data[idx++] = 0x02;  // Length
    adv_data[idx++] = 0x01;  // Type: Flags
    adv_data[idx++] = 0x06;  // BR/EDR not supported, General Discoverable
    
    // Complete local name (10 bytes for "AirCube")
    uint8_t name_len = strlen(s_device_name);
    adv_data[idx++] = name_len + 1;
    adv_data[idx++] = 0x09;  // Type: Complete local name
    memcpy(&adv_data[idx], s_device_name, name_len);
    idx += name_len;
    
    // BTHome service data - Length placeholder
    uint8_t service_data_len_idx = idx;
    adv_data[idx++] = 0x00;  // Length (will be calculated at end)
    adv_data[idx++] = 0x16;  // Type: Service Data - 16-bit UUID
    adv_data[idx++] = 0xD2;  // BTHome UUID LSB
    adv_data[idx++] = 0xFC;  // BTHome UUID MSB
    adv_data[idx++] = 0x40;  // BTHome device info (v2, unencrypted)
    
    uint8_t service_data_start = idx;
    
    // Always include high-priority sensors (Temp, Humidity, AQI)
    // Temperature (0.01°C resolution) - 3 bytes
    int16_t temp_encoded = (int16_t)(s_sensor_data.temperature * 100);
    adv_data[idx++] = BTHOME_OBJ_TEMPERATURE;
    adv_data[idx++] = temp_encoded & 0xFF;
    adv_data[idx++] = (temp_encoded >> 8) & 0xFF;
    
    // Humidity (0.01% resolution) - 3 bytes
    uint16_t hum_encoded = (uint16_t)(s_sensor_data.humidity * 100);
    adv_data[idx++] = BTHOME_OBJ_HUMIDITY;
    adv_data[idx++] = hum_encoded & 0xFF;
    adv_data[idx++] = (hum_encoded >> 8) & 0xFF;
    
    // AQI (as generic count, 0-255) - 2 bytes
    adv_data[idx++] = BTHOME_OBJ_COUNT;
    adv_data[idx++] = s_sensor_data.aqi & 0xFF;
    
    // Alternating sensors (TVOC & CO2) to keep packet size down
    if (s_packet_toggle == 0) {
        // TVOC (ppb) - 3 bytes
        adv_data[idx++] = BTHOME_OBJ_TVOC;
        adv_data[idx++] = s_sensor_data.tvoc & 0xFF;
        adv_data[idx++] = (s_sensor_data.tvoc >> 8) & 0xFF;
    } else {
        // CO2 (ppm) - 3 bytes
        adv_data[idx++] = BTHOME_OBJ_CO2;
        adv_data[idx++] = s_sensor_data.co2 & 0xFF;
        adv_data[idx++] = (s_sensor_data.co2 >> 8) & 0xFF;
    }
    
    // Calculate and fill in service data length
    // Length = everything from after length byte to end, INCLUDING the type byte
    adv_data[service_data_len_idx] = (idx - service_data_len_idx - 1);
    
    *adv_data_len = idx;
    
    ESP_LOGI(TAG, "Built packet type %d: %d bytes total", s_packet_toggle, idx);
}

/**
 * @brief GAP event handler
 */
static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    esp_err_t ret;
    
    ESP_LOGI(TAG, "GAP event: %d", event);
    
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
        ESP_LOGI(TAG, "Advertisement data set complete (status: %d), starting advertising", 
                 param->adv_data_raw_cmpl.status);
        if (param->adv_data_raw_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            ret = esp_ble_gap_start_advertising(&adv_params);
            if (ret) {
                ESP_LOGE(TAG, "Failed to start advertising: %s", esp_err_to_name(ret));
            }
        } else {
            ESP_LOGE(TAG, "Setting adv data failed with status: %d", param->adv_data_raw_cmpl.status);
        }
        break;
        
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "Advertising started successfully");
            s_advertising = true;
        } else {
            ESP_LOGE(TAG, "Advertising start failed: %d", param->adv_start_cmpl.status);
            s_advertising = false;
        }
        break;
        
    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        if (param->adv_stop_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "Advertising stopped");
            s_advertising = false;
        } else {
            ESP_LOGE(TAG, "Advertising stop failed");
        }
        break;
        
    default:
        ESP_LOGD(TAG, "Unhandled GAP event: %d", event);
        break;
    }
}

void ble_advertiser_init(const char *device_name)
{
    if (device_name) {
        size_t name_len = strlen(device_name);
        if (name_len > 10) {
            ESP_LOGW(TAG, "Device name '%s' exceeds 10 character limit, truncating to prevent buffer overflow", device_name);
            strncpy(s_device_name, device_name, 10);
            s_device_name[10] = '\0';
        } else {
            strncpy(s_device_name, device_name, sizeof(s_device_name) - 1);
            s_device_name[sizeof(s_device_name) - 1] = '\0';
        }
    }
    
    ESP_LOGI(TAG, "Initializing BLE advertiser for device: %s", s_device_name);
    
    // Release BLE/Classic memory (we only need BLE)
    esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    
    // Initialize BT controller
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_err_t ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        ESP_LOGE(TAG, "BT controller init failed: %s", esp_err_to_name(ret));
        return;
    }
    
    // Enable BLE mode
    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) {
        ESP_LOGE(TAG, "BT controller enable failed: %s", esp_err_to_name(ret));
        return;
    }
    
    // Initialize Bluedroid
    ret = esp_bluedroid_init();
    if (ret) {
        ESP_LOGE(TAG, "Bluedroid init failed: %s", esp_err_to_name(ret));
        return;
    }
    
    ret = esp_bluedroid_enable();
    if (ret) {
        ESP_LOGE(TAG, "Bluedroid enable failed: %s", esp_err_to_name(ret));
        return;
    }
    
    // Register GAP callback
    ret = esp_ble_gap_register_callback(gap_event_handler);
    if (ret) {
        ESP_LOGE(TAG, "GAP register failed: %s", esp_err_to_name(ret));
        return;
    }
    
    ESP_LOGI(TAG, "BLE advertiser initialized");
}

void ble_advertiser_start(void)
{
    uint8_t adv_data[31];
    uint8_t adv_data_len;
    
    // Build advertisement packet
    build_advertisement_data(adv_data, &adv_data_len);
    
    ESP_LOGI(TAG, "Setting advertisement data (%d bytes)", adv_data_len);
    
    // Log packet contents for debugging
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, adv_data, adv_data_len, ESP_LOG_INFO);
    
    // Set advertisement data
    esp_err_t ret = esp_ble_gap_config_adv_data_raw(adv_data, adv_data_len);
    if (ret) {
        ESP_LOGE(TAG, "Failed to set adv data: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Advertisement data queued, waiting for event...");
    }
}

void ble_advertiser_stop(void)
{
    if (s_advertising) {
        esp_ble_gap_stop_advertising();
    }
}

void ble_advertiser_update_data(float temperature, float humidity,
                                uint16_t tvoc, uint16_t co2,
                                uint8_t aqi)
{
    s_sensor_data.temperature = temperature;
    s_sensor_data.humidity = humidity;
    s_sensor_data.tvoc = tvoc;
    s_sensor_data.co2 = co2;
    s_sensor_data.aqi = aqi;
    
    // If already advertising, update the advertisement data
    if (s_advertising) {
        uint8_t adv_data[31];
        uint8_t adv_data_len;
        
        // Toggle packet type for next update (smart updates)
        s_packet_toggle = 1 - s_packet_toggle;
        
        build_advertisement_data(adv_data, &adv_data_len);
        
        ESP_LOGI(TAG, "Updating advertisement with new sensor data (%d bytes)", adv_data_len);
        esp_ble_gap_config_adv_data_raw(adv_data, adv_data_len);
    }
}

bool ble_advertiser_is_active(void)
{
    return s_advertising;
}
