  #include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "nvs_flash.h"
#include "led_color_lib.h"
#include <math.h>

#include "led.h"
#include "ens210.h"
#include "ens16x_driver.h"
#include "i2c_driver.h"
#include "serial_protocol.h"
#include "button.h"
#include "ble_advertiser.h"
#include "device_config.h"

static const char *TAG = "main";

// Global sensor data for BLE advertising
static float g_temperature = 0.0f;
static float g_humidity = 0.0f;
static uint16_t g_tvoc = 0;
static uint16_t g_co2 = 0;
static uint8_t g_aqi = 0;

#define SENSOR_TASK_STACK_SIZE 4096
#define SENSOR_TASK_PRIORITY 5
#define COMMAND_TASK_STACK_SIZE 2048
#define COMMAND_TASK_PRIORITY 4

// Configurable sensor readout period (default 1000ms)
static uint32_t sensor_readout_period_ms = 1000;
static SemaphoreHandle_t readout_period_mutex = NULL;

// AQI color mapping constants
#define AQI_MIN 0
#define AQI_MAX 200
#define AQI_GREEN_THRESHOLD 10  // Values 0-10 are pure green

// Global variables to store sensor data for LED color mapping
static int current_aqi = 0;
static enum ENS_STATUS current_ens16x_status = ENS_RESERVED;

// Static variables for pulsing effect
static uint32_t pulse_time_ms = 0;  // Current pulse time in milliseconds (accumulates)
#define PULSE_MS 50  // Pulse period in milliseconds

// Static variables for smooth LED color transitions
static float current_hue = 21845.0f;  // Current hue value (21845 = green, 0 = red) - using float for smooth transitions
static uint16_t target_hue = 21845;   // Target hue value we want to transition to
#define TRANSITION_SPEED 0.02f  // Transition speed per update (0.0 to 1.0, higher = faster)
// With 20ms update interval and 0.02 speed, full transition takes ~1 second (50 steps)
#define HUE_GREEN 21845  // 2/6 of 65536 (120 degrees - green)

// Getter and setter for sensor readout period (for serial_protocol.c)
uint32_t get_sensor_readout_period_ms(void)
{
    uint32_t period = 1000;
    if (readout_period_mutex != NULL) {
        if (xSemaphoreTake(readout_period_mutex, portMAX_DELAY) == pdTRUE) {
            period = sensor_readout_period_ms;
            xSemaphoreGive(readout_period_mutex);
        }
    }
    return period;
}

void set_sensor_readout_period_ms(uint32_t period)
{
    if (readout_period_mutex != NULL) {
        if (xSemaphoreTake(readout_period_mutex, portMAX_DELAY) == pdTRUE) {
            sensor_readout_period_ms = period;
            xSemaphoreGive(readout_period_mutex);
        }
    }
}

/**
 * @brief Map AQI value to hue
 * 
 * Maps AQI with the following behavior:
 * - AQI 0-10: pure green (no color change)
 * - AQI 10-200: smooth gradient from green to red
 * 
 * @param aqi Air Quality Index value
 * @return 16-bit hue value (21845 = green, 0 = red)
 */
static uint16_t aqi_to_hue(int aqi)
{
    // Clamp AQI to valid range
    if (aqi < AQI_MIN) aqi = AQI_MIN;
    if (aqi > AQI_MAX) aqi = AQI_MAX;
    
    // Values 0-10 stay at green
    if (aqi <= AQI_GREEN_THRESHOLD) {
        return HUE_GREEN;  // Pure green
    }
    
    // Map AQI 10-200 to hue values from 21845 (green) to 0 (red)
    // Linear interpolation from green (21845) at AQI=10 to red (0) at AQI=200
    float normalized = (float)(aqi - AQI_GREEN_THRESHOLD) / (float)(AQI_MAX - AQI_GREEN_THRESHOLD);
    uint16_t hue = HUE_GREEN - (uint16_t)(normalized * HUE_GREEN);
    
    return hue;
}

/**
 * @brief Get pulsing color with intensity control
 * 
 * Applies a sinusoidal pulsing effect (1Hz) to the specified RGB color.
 * The LED task will then apply the user's brightness setting to the result.
 * 
 * @param red Red component (0-255)
 * @param green Green component (0-255)
 * @param blue Blue component (0-255)
 * @return 32-bit GRB color value with pulsing applied
 */
static uint32_t get_pulsing_color_with_intensity(uint8_t red, uint8_t green, uint8_t blue)
{
    // Increment pulse time (50ms per update gives us a 1Hz pulse: 1000ms / 50ms = 20 steps per second)
    pulse_time_ms += PULSE_MS;
    
    // Calculate pulse brightness using sine wave (0.5 to 1.0 range)
    // Period = 1000ms (1Hz), so 2π radians per second
    float angle = (2.0f * 3.14159f * pulse_time_ms) / 1000.0f;
    float pulse_brightness = 0.5f + 0.5f * sinf(angle);

    // Apply the pulse brightness to the specified color (0 to 255)
    // The LED task will apply the intensity setting, so we pulse to full brightness here
    float r = pulse_brightness * red;
    float g = pulse_brightness * green;
    float b = pulse_brightness * blue;

    // Convert to GRB format for WS2812 LEDs
    return ((uint32_t)(g + 0.5f) << 16) | ((uint32_t)(r + 0.5f) << 8) | (uint32_t)(b + 0.5f);
}

// Command processing task
void command_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Command task started");
    
    while (1) {
        // Process incoming commands
        serial_process_commands();
        
        // Small delay to prevent CPU spinning
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

// Sensor reading task
void sensor_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Sensor task started");
    
    while (1) {
        // Read ENS210 temperature and humidity
        ens210_read_envir();
        float temp_c = ens210_get_temperature(1); // 1 = Celsius
        float humidity = ens210_get_humidity();
        uint8_t ens210_status = ens210_get_status();
        
        // Write ENS210 data to ENS161 for environmental compensation
        uint8_t ens210_t[2];
        uint8_t ens210_h[2];
        ens210_get_envir(ens210_t, ens210_h);
        ens16x_write_ens210_data(ens210_t, ens210_h);
        
        // Read ENS16X air quality data
        int etvoc = ens16x_read_etvoc();
        int eco2 = ens16x_read_eco2();
        int aqi = ens16x_read_aqi();
        enum ENS_STATUS ens16x_status = ens16x_get_status();
        
        // Update global variables for LED color mapping
        current_aqi = aqi;
        current_ens16x_status = ens16x_status;
        
        // Update global sensor data for BLE
        g_temperature = temp_c;
        g_humidity = humidity;
        g_tvoc = etvoc;
        g_co2 = eco2;
        g_aqi = aqi;
        
        // Update BLE advertisement data
        ble_advertiser_update_data(temp_c, humidity, etvoc, eco2, aqi);
        
        // Helper function to convert ENS16X status to string
        const char* ens16x_status_str;
        switch(ens16x_status) {
            case ENS_OP_OK:
                ens16x_status_str = "OK";
                break;
            case ENS_WARM_UP:
                ens16x_status_str = "Warming Up";
                break;
            case ENS_NO_VALID_OUTPUT:
                ens16x_status_str = "No Valid Output";
                break;
            case ENS_RESERVED:
                ens16x_status_str = "Reserved";
                break;
            default:
                ens16x_status_str = "Unknown";
                break;
        }
        
        // Display all sensor data with status
        ESP_LOGI(TAG, "=== Sensor Data ===");
        ESP_LOGI(TAG, "ENS210 - Status: 0x%02X, Temperature: %.2f°C, Humidity: %.2f%%", 
                 ens210_status, temp_c, humidity);
        ESP_LOGI(TAG, "ENS16X - Status: %s, eTVOC: %d ppb, eCO2: %d ppm, AQI: %d", 
                 ens16x_status_str, etvoc, eco2, aqi);
        
        // Send sensor data as JSON over serial
        serial_send_sensor_data(ens210_status, temp_c, humidity,
                               ens16x_status_str, etvoc, eco2, aqi);
        
        // Wait for configurable period before next reading
        uint32_t period = get_sensor_readout_period_ms();
        vTaskDelay(period / portTICK_PERIOD_MS);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "%s starting with BLE", DEVICE_NAME);

    // Configure power management with automatic light sleep
    // Note: ESP32-H2 uses the same structure as ESP32-C2 (both RISC-V based)
    esp_pm_config_t pm_config = {
        .max_freq_mhz = 10,           // Maximum CPU frequency (MHz)
        .min_freq_mhz = 10,            // Minimum CPU frequency (MHz)
        .light_sleep_enable = false    // Enable automatic light sleep when idle
    };
    
    esp_err_t ret = esp_pm_configure(&pm_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure power management: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Power management configured with automatic light sleep disabled");
    }

    // Initialize NVS (Non-Volatile Storage) for saving settings
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialized");

    // Initialize BLE advertising
    ESP_LOGI(TAG, "Initializing BLE...");
    ble_advertiser_init(DEVICE_NAME);
    ble_advertiser_start();
    ESP_LOGI(TAG, "BLE advertising started");

    // Initialize I2C driver (must be done before initializing sensors)
    if (i2c_driver_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2C driver");
        return;
    }

    // Create mutex for readout period
    readout_period_mutex = xSemaphoreCreateMutex();
    if (readout_period_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create readout period mutex");
        return;
    }
    
    // Initialize serial protocol
    serial_protocol_init();
    
    // Initialize LED control system
    led_init();
    
    // Set initial LED color to green (animation start color) before animation
    uint32_t start_color = get_color_from_hue(HUE_GREEN);
    led_set_color(start_color);

    
    // // Play startup animation (3 second sweep from green to red and back)
    // ESP_LOGI(TAG, "Playing startup animation");
    // startup_animation();
    
    // Initialize button for brightness control
    button_init();
    
    // Initialize ENS210 temperature and humidity sensor
    ens210_init();
    ESP_LOGI(TAG, "ENS210 initialized");
    
    // Initialize ENS16X air quality sensor
    ens16x_init();
    ESP_LOGI(TAG, "ENS16X initialized");
    
    // Create command processing task
    xTaskCreate(command_task, "command_task", COMMAND_TASK_STACK_SIZE, NULL, 
                COMMAND_TASK_PRIORITY, NULL);
    ESP_LOGI(TAG, "Command task created");
    
    // Create sensor reading task
    xTaskCreate(sensor_task, "sensor_task", SENSOR_TASK_STACK_SIZE, NULL, 
                SENSOR_TASK_PRIORITY, NULL);
    ESP_LOGI(TAG, "Sensor task created");

    // Main loop for LED color based on sensor status and AQI
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(20));  // Update LED every 20ms for smooth transitions
        
        // Determine target hue based on sensor status and AQI
        if (current_ens16x_status == ENS_WARM_UP) {
            // If sensor is warming up, target blue hue
            // Blue is at 4/6 of the spectrum: (4/6) * 65536 = 43690
            target_hue = 43690;
        } else if (current_aqi >= AQI_MAX) {
            // If AQI is 200+, target red hue (0)
            target_hue = 0;
        } else {
            // Otherwise, use AQI-based hue (green at 0, red at 200)
            target_hue = aqi_to_hue(current_aqi);
        }
        
        // Smoothly transition current_hue towards target_hue
        float hue_diff = (float)target_hue - current_hue;
        current_hue += hue_diff * TRANSITION_SPEED;
        
        // Convert current hue to color (cast to uint16_t for color conversion)
        uint32_t color = get_color_from_hue((uint16_t)current_hue);
        
        // Apply pulsing effect if needed
        if (current_ens16x_status == ENS_WARM_UP) {
            // Pulse blue
            color = get_pulsing_color_with_intensity(0, 0, 255);
        } 
        
        // Update LED with the smoothly transitioning color
        led_set_color(color);
    }
}
