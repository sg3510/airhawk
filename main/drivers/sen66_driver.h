#pragma once

#include <esp_err.h>
#include <stdint.h>

// Callback type for scalar sensor values (temperature, humidity, VOC index, NOx index, CO2)
using sen66_sensor_cb_t = void (*)(uint16_t endpoint_id, float value, void *user_data);

typedef struct {
    struct {
        sen66_sensor_cb_t cb = NULL;
        uint16_t endpoint_id;
    } temperature;

    struct {
        sen66_sensor_cb_t cb = NULL;
        uint16_t endpoint_id;
    } humidity;

    struct {
        sen66_sensor_cb_t cb = NULL;
        uint16_t endpoint_id;
    } co2;

    struct {
        sen66_sensor_cb_t cb = NULL;
        uint16_t endpoint_id;
    } pm1;

    struct {
        sen66_sensor_cb_t cb = NULL;
        uint16_t endpoint_id;
    } pm25;

    struct {
        sen66_sensor_cb_t cb = NULL;
        uint16_t endpoint_id;
    } pm10;

    struct {
        sen66_sensor_cb_t cb = NULL;
        uint16_t endpoint_id;
    } voc;

    struct {
        sen66_sensor_cb_t cb = NULL;
        uint16_t endpoint_id;
    } nox;

    // Callback for computed air quality enum (0-6)
    using air_quality_cb_t = void (*)(uint16_t endpoint_id, uint8_t air_quality, void *user_data);
    struct {
        air_quality_cb_t cb = NULL;
        uint16_t endpoint_id;
    } air_quality;

    void *user_data = NULL;
    uint32_t interval_ms = 5000;
} sen66_driver_config_t;

/**
 * @brief Initialize the SEN66 sensor driver.
 *
 * Sets up I2C, starts continuous measurement, and begins periodic polling.
 * At least one callback must be provided.
 *
 * @param config Driver configuration. Must remain valid for the lifetime of the driver.
 * @return ESP_OK on success
 */
esp_err_t sen66_driver_init(sen66_driver_config_t *config);
