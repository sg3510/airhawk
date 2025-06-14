#include "MatterRGBLEDDriver.h"
#include <device.h>
#include <led_driver.h>
#include <esp_matter.h>

using namespace esp_matter;

static const char *TAG = "MatterRGBLEDDriver";

/** Standard max values (used for remapping attributes) */
#define STANDARD_BRIGHTNESS 100
#define STANDARD_HUE 360
#define STANDARD_SATURATION 100
#define STANDARD_TEMPERATURE_FACTOR 1000000

/** Matter max values (used for remapping attributes) */
#define MATTER_BRIGHTNESS 254
#define MATTER_HUE 254
#define MATTER_SATURATION 254
#define MATTER_TEMPERATURE_FACTOR 1000000

MatterRGBLEDDriver::MatterRGBLEDDriver(void* lightHandle)
    : m_lightHandle(lightHandle)
{
}

MatterRGBLEDDriver* MatterRGBLEDDriver::Create()
{
    led_driver_config_t config = led_driver_get_config();
    void* lightHandle = led_driver_init(&config);
    if (!lightHandle) {
        ESP_LOGE(TAG, "Failed to initialize LED driver");
        return nullptr;
    }

    return new MatterRGBLEDDriver(lightHandle);
}

esp_err_t MatterRGBLEDDriver::SetPower(bool onOff)
{
    esp_err_t err = led_driver_set_power(m_lightHandle, onOff);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set power state: %s", esp_err_to_name(err));
        return err; // Return error if setting power fails
    }
    ESP_LOGI(TAG, "LED power state set to %s", onOff ? "ON" : "OFF");
    return err;
}

esp_err_t MatterRGBLEDDriver::SetBrightness(uint8_t matterBrightness)
{
    int value = REMAP_TO_RANGE(matterBrightness, MATTER_BRIGHTNESS, STANDARD_BRIGHTNESS);
    esp_err_t err = led_driver_set_brightness(m_lightHandle, value);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set brightness: %s", esp_err_to_name(err));
        return err; // Return error if setting brightness fails
    }
    ESP_LOGI(TAG, "LED brightness set to %d", value);
    return err;
}

esp_err_t MatterRGBLEDDriver::SetHue(uint8_t matterHue)
{
    int value = REMAP_TO_RANGE(matterHue, MATTER_HUE, STANDARD_HUE);
    esp_err_t err = led_driver_set_hue(m_lightHandle, value);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set hue: %s", esp_err_to_name(err));
        return err; // Return error if setting hue fails
    }
    ESP_LOGI(TAG, "LED hue set to %d", value);
    return err;
}

esp_err_t MatterRGBLEDDriver::SetSaturation(uint8_t matterSaturation)
{
    int value = REMAP_TO_RANGE(matterSaturation, MATTER_SATURATION, STANDARD_SATURATION);
    esp_err_t err = led_driver_set_saturation(m_lightHandle, value);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set saturation: %s", esp_err_to_name(err));
        return err; // Return error if setting saturation fails
    }
    ESP_LOGI(TAG, "LED saturation set to %u", value);
    return err;
}

esp_err_t MatterRGBLEDDriver::SetTemperature(uint16_t matterSaturation)
{
    uint32_t value = REMAP_TO_RANGE_INVERSE(matterSaturation, STANDARD_TEMPERATURE_FACTOR);
    esp_err_t err = led_driver_set_temperature(m_lightHandle, value);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set temperature: %s", esp_err_to_name(err));
        return err; // Return error if setting temperature fails
    }
    ESP_LOGI(TAG, "LED temperature set to %" PRIu32, value);
    return err;
}