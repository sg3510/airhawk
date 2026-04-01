#include <esp_err.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <drivers/sen66_driver.h>
#include <sensors/drivers/sensirion/sen66_i2c.h>
#include <sensors/drivers/sensirion/sensirion_common.h>
#include <sensors/drivers/sensirion/sensirion_i2c_hal.h>

static const char *TAG = "sen66_driver";

static const uint16_t INVALID_U16 = 0xFFFF;
static const int16_t INVALID_I16 = 0x7FFF;

typedef struct {
    sen66_driver_config_t *config;
    esp_timer_handle_t timer;
    bool is_initialized = false;
} sen66_driver_ctx_t;

static sen66_driver_ctx_t s_ctx;

// Compute an overall air quality enum from PM2.5 (µg/m³) based on US AQI breakpoints
static uint8_t compute_air_quality(float pm25)
{
    if (pm25 <= 12.0f) return 1;       // Good
    if (pm25 <= 35.4f) return 2;       // Fair
    if (pm25 <= 55.4f) return 3;       // Moderate
    if (pm25 <= 150.4f) return 4;      // Poor
    if (pm25 <= 250.4f) return 5;      // VeryPoor
    return 6;                           // ExtremelyPoor
}

static void poll_sensor(void *arg)
{
    auto *ctx = (sen66_driver_ctx_t *)arg;
    if (!(ctx && ctx->config)) {
        return;
    }

    uint16_t pm1_raw, pm25_raw, pm4_raw, pm10_raw, co2_raw;
    int16_t humidity_raw, temperature_raw, voc_raw, nox_raw;

    int16_t err = sen66_read_measured_values_as_integers(
        &pm1_raw, &pm25_raw, &pm4_raw, &pm10_raw,
        &humidity_raw, &temperature_raw, &voc_raw, &nox_raw, &co2_raw);

    if (err != NO_ERROR) {
        ESP_LOGW(TAG, "Failed to read SEN66 values: %d", err);
        return;
    }

    auto *cfg = ctx->config;

    if (cfg->temperature.cb && temperature_raw != INVALID_I16) {
        float temp = temperature_raw / 200.0f;
        cfg->temperature.cb(cfg->temperature.endpoint_id, temp, cfg->user_data);
    }

    if (cfg->humidity.cb && humidity_raw != INVALID_I16) {
        float hum = humidity_raw / 100.0f;
        cfg->humidity.cb(cfg->humidity.endpoint_id, hum, cfg->user_data);
    }

    if (cfg->co2.cb && co2_raw != INVALID_U16) {
        cfg->co2.cb(cfg->co2.endpoint_id, (float)co2_raw, cfg->user_data);
    }

    if (cfg->pm1.cb && pm1_raw != INVALID_U16) {
        float pm1 = pm1_raw / 10.0f;
        cfg->pm1.cb(cfg->pm1.endpoint_id, pm1, cfg->user_data);
    }

    if (cfg->pm25.cb && pm25_raw != INVALID_U16) {
        float pm25 = pm25_raw / 10.0f;
        cfg->pm25.cb(cfg->pm25.endpoint_id, pm25, cfg->user_data);
    }

    if (cfg->pm10.cb && pm10_raw != INVALID_U16) {
        float pm10 = pm10_raw / 10.0f;
        cfg->pm10.cb(cfg->pm10.endpoint_id, pm10, cfg->user_data);
    }

    if (cfg->voc.cb && voc_raw != INVALID_I16) {
        float voc = voc_raw / 10.0f;
        cfg->voc.cb(cfg->voc.endpoint_id, voc, cfg->user_data);
    }

    if (cfg->nox.cb && nox_raw != INVALID_I16) {
        float nox = nox_raw / 10.0f;
        cfg->nox.cb(cfg->nox.endpoint_id, nox, cfg->user_data);
    }

    if (cfg->air_quality.cb && pm25_raw != INVALID_U16) {
        float pm25 = pm25_raw / 10.0f;
        uint8_t aq = compute_air_quality(pm25);
        cfg->air_quality.cb(cfg->air_quality.endpoint_id, aq, cfg->user_data);
    }
}

esp_err_t sen66_driver_init(sen66_driver_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_ctx.is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    // Initialize I2C and SEN66
    sensirion_i2c_hal_init();
    sen66_init(SEN66_I2C_ADDR_6B);

    // SEN66 needs time after power-on before it responds on I2C
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Reset the sensor to a known state
    int16_t status = sen66_device_reset();
    if (status != NO_ERROR) {
        ESP_LOGW(TAG, "Device reset failed: %d (may be normal on first boot)", status);
    }
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Enable automatic CO2 self-calibration
    status = sen66_set_co2_sensor_automatic_self_calibration(1);
    if (status != NO_ERROR) {
        ESP_LOGW(TAG, "Failed to enable CO2 auto-calibration: %d", status);
    }

    // Start continuous measurement
    status = sen66_start_continuous_measurement();
    if (status != NO_ERROR) {
        ESP_LOGE(TAG, "Failed to start continuous measurement: %d", status);
        return ESP_FAIL;
    }

    s_ctx.config = config;

    esp_timer_create_args_t timer_args = {
        .callback = poll_sensor,
        .arg = &s_ctx,
        .name = "sen66_poll",
    };

    esp_err_t err = esp_timer_create(&timer_args, &s_ctx.timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create timer: %d", err);
        return err;
    }

    err = esp_timer_start_periodic(s_ctx.timer, config->interval_ms * 1000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start timer: %d", err);
        return err;
    }

    s_ctx.is_initialized = true;
    ESP_LOGI(TAG, "SEN66 driver initialized, polling every %lu ms", (unsigned long)config->interval_ms);

    return ESP_OK;
}
