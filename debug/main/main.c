#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

#define I2C_SDA_PIN 6
#define I2C_SCL_PIN 7
#define SEN66_ADDR  0x6B

static const char *TAG = "i2c_test";

static i2c_master_bus_handle_t bus_handle;
static i2c_master_dev_handle_t sen66_dev;

// Send a 2-byte command to SEN66
static esp_err_t sen66_cmd(uint16_t cmd)
{
    uint8_t buf[2] = { cmd >> 8, cmd & 0xFF };
    return i2c_master_transmit(sen66_dev, buf, 2, 100);
}

// Read N words (each word = 2 data bytes + 1 CRC byte on wire)
// Returns stripped data in out[] as uint16
static esp_err_t sen66_read_words(uint16_t cmd, uint16_t *out, int num_words, int delay_ms)
{
    esp_err_t ret = sen66_cmd(cmd);
    if (ret != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(delay_ms));

    int wire_len = num_words * 3;
    uint8_t *buf = malloc(wire_len);
    if (!buf) return ESP_ERR_NO_MEM;

    ret = i2c_master_receive(sen66_dev, buf, wire_len, 100);
    if (ret == ESP_OK) {
        for (int i = 0; i < num_words; i++) {
            out[i] = (buf[i * 3] << 8) | buf[i * 3 + 1];
        }
    }
    free(buf);
    return ret;
}

// Read string (N words, each 2 chars + CRC)
static esp_err_t sen66_read_string(uint16_t cmd, char *out, int max_chars, int delay_ms)
{
    int num_words = (max_chars + 1) / 2;
    uint16_t *words = malloc(num_words * sizeof(uint16_t));
    if (!words) return ESP_ERR_NO_MEM;

    esp_err_t ret = sen66_read_words(cmd, words, num_words, delay_ms);
    if (ret == ESP_OK) {
        int pos = 0;
        for (int i = 0; i < num_words && pos < max_chars; i++) {
            out[pos++] = (words[i] >> 8) & 0xFF;
            if (pos < max_chars) out[pos++] = words[i] & 0xFF;
        }
        out[pos] = '\0';
        while (pos > 0 && (out[pos-1] == '\0' || out[pos-1] == ' ')) out[--pos] = '\0';
    }
    free(words);
    return ret;
}

static void i2c_scan(void)
{
    ESP_LOGI(TAG, "Scanning I2C bus on SDA=%d SCL=%d ...", I2C_SDA_PIN, I2C_SCL_PIN);
    for (uint8_t addr = 1; addr < 127; addr++) {
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = addr,
            .scl_speed_hz = 100000,
        };
        i2c_master_dev_handle_t dev;
        if (i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev) == ESP_OK) {
            uint8_t dummy;
            esp_err_t ret = i2c_master_receive(dev, &dummy, 1, 50);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "  Found device at 0x%02X", addr);
            }
            i2c_master_bus_rm_device(dev);
        }
    }
    ESP_LOGI(TAG, "Scan complete.");
}

static void sen66_print_device_info(void)
{
    ESP_LOGI(TAG, "========== SEN66 DEVICE INFO ==========");

    // Product name (cmd 0xD014, up to 32 chars = 16 words)
    char name[33] = {0};
    if (sen66_read_string(0xD014, name, 32, 20) == ESP_OK) {
        ESP_LOGI(TAG, "  Product:  %s", name);
    } else {
        ESP_LOGW(TAG, "  Product:  read failed");
    }

    // Serial number (cmd 0xD033, up to 32 chars = 16 words)
    char serial[33] = {0};
    if (sen66_read_string(0xD033, serial, 32, 20) == ESP_OK) {
        ESP_LOGI(TAG, "  Serial:   %s", serial);
    } else {
        ESP_LOGW(TAG, "  Serial:   read failed");
    }

    // Firmware version (cmd 0xD100, 1 word: major.minor)
    uint16_t ver;
    if (sen66_read_words(0xD100, &ver, 1, 20) == ESP_OK) {
        ESP_LOGI(TAG, "  Firmware: %d.%d", (ver >> 8) & 0xFF, ver & 0xFF);
    } else {
        ESP_LOGW(TAG, "  Firmware: read failed");
    }

    // Device status (cmd 0xD206, 2 words = 32 bits)
    uint16_t status[2];
    if (sen66_read_words(0xD206, status, 2, 20) == ESP_OK) {
        uint32_t s = ((uint32_t)status[0] << 16) | status[1];
        ESP_LOGI(TAG, "  Status:   0x%08lX", (unsigned long)s);
        if (s & (1 << 4))  ESP_LOGW(TAG, "    -> FAN ERROR");
        if (s & (1 << 6))  ESP_LOGW(TAG, "    -> RH/T SENSOR ERROR");
        if (s & (1 << 7))  ESP_LOGW(TAG, "    -> GAS SENSOR ERROR (VOC/NOx)");
        if (s & (1 << 9))  ESP_LOGW(TAG, "    -> CO2-2 SENSOR ERROR");
        if (s & (1 << 11)) ESP_LOGW(TAG, "    -> PM SENSOR ERROR");
        if (s & (1 << 21)) ESP_LOGW(TAG, "    -> FAN SPEED WARNING");
        if (s == 0) ESP_LOGI(TAG, "    -> All OK");
    }

    // CO2 auto self-calibration (cmd 0x6711, 1 word)
    uint16_t asc;
    if (sen66_read_words(0x6711, &asc, 1, 20) == ESP_OK) {
        ESP_LOGI(TAG, "  CO2 ASC:  %s", asc ? "enabled" : "disabled");
    }

    // Sensor altitude (cmd 0x6736, 1 word)
    uint16_t alt;
    if (sen66_read_words(0x6736, &alt, 1, 20) == ESP_OK) {
        ESP_LOGI(TAG, "  Altitude: %u m", alt);
    }

    // Ambient pressure (cmd 0x6720, 1 word, in hPa)
    uint16_t press;
    if (sen66_read_words(0x6720, &press, 1, 20) == ESP_OK) {
        ESP_LOGI(TAG, "  Pressure: %u hPa", press);
    }

    // VOC algorithm tuning (cmd 0x60D0, 6 words)
    uint16_t voc_tune[6];
    if (sen66_read_words(0x60D0, voc_tune, 6, 20) == ESP_OK) {
        ESP_LOGI(TAG, "  VOC Tuning:");
        ESP_LOGI(TAG, "    index_offset=%d learning_time_offset=%d",
                 (int16_t)voc_tune[0], (int16_t)voc_tune[1]);
        ESP_LOGI(TAG, "    learning_time_gain=%d gating_max_dur=%d",
                 (int16_t)voc_tune[2], (int16_t)voc_tune[3]);
        ESP_LOGI(TAG, "    std_initial=%d gain_factor=%d",
                 (int16_t)voc_tune[4], (int16_t)voc_tune[5]);
    }

    // NOx algorithm tuning (cmd 0x60E1, 6 words)
    uint16_t nox_tune[6];
    if (sen66_read_words(0x60E1, nox_tune, 6, 20) == ESP_OK) {
        ESP_LOGI(TAG, "  NOx Tuning:");
        ESP_LOGI(TAG, "    index_offset=%d learning_time_offset=%d",
                 (int16_t)nox_tune[0], (int16_t)nox_tune[1]);
        ESP_LOGI(TAG, "    learning_time_gain=%d gating_max_dur=%d",
                 (int16_t)nox_tune[2], (int16_t)nox_tune[3]);
        ESP_LOGI(TAG, "    std_initial=%d gain_factor=%d",
                 (int16_t)nox_tune[4], (int16_t)nox_tune[5]);
    }

    ESP_LOGI(TAG, "========================================");
}

static void sen66_read_measurements(void)
{
    // Main measured values (cmd 0x0300, 9 words)
    uint16_t val[9];
    if (sen66_read_words(0x0300, val, 9, 50) == ESP_OK) {
        ESP_LOGI(TAG, "=== Measured Values ===");
        ESP_LOGI(TAG, "  PM1.0:     %.1f ug/m3", val[0] / 10.0f);
        ESP_LOGI(TAG, "  PM2.5:     %.1f ug/m3", val[1] / 10.0f);
        ESP_LOGI(TAG, "  PM4.0:     %.1f ug/m3", val[2] / 10.0f);
        ESP_LOGI(TAG, "  PM10:      %.1f ug/m3", val[3] / 10.0f);
        ESP_LOGI(TAG, "  Humidity:  %.1f %%RH", (int16_t)val[4] / 100.0f);
        ESP_LOGI(TAG, "  Temp:      %.2f C", (int16_t)val[5] / 200.0f);
        ESP_LOGI(TAG, "  VOC Index: %d", (int16_t)val[6] / 10);
        ESP_LOGI(TAG, "  NOx Index: %d", (int16_t)val[7] / 10);
        ESP_LOGI(TAG, "  CO2:       %u ppm", val[8]);
    } else {
        ESP_LOGE(TAG, "Failed to read measured values");
    }

    // Particle number concentrations (cmd 0x0316, 5 words)
    uint16_t pc[5];
    if (sen66_read_words(0x0316, pc, 5, 50) == ESP_OK) {
        ESP_LOGI(TAG, "=== Particle Counts (per cm3) ===");
        ESP_LOGI(TAG, "  PM0.5:  %.1f", pc[0] / 10.0f);
        ESP_LOGI(TAG, "  PM1.0:  %.1f", pc[1] / 10.0f);
        ESP_LOGI(TAG, "  PM2.5:  %.1f", pc[2] / 10.0f);
        ESP_LOGI(TAG, "  PM4.0:  %.1f", pc[3] / 10.0f);
        ESP_LOGI(TAG, "  PM10:   %.1f", pc[4] / 10.0f);
    } else {
        ESP_LOGE(TAG, "Failed to read particle counts");
    }

    // Raw values (cmd 0x0405, 4 words: raw_humidity, raw_temp, raw_voc, raw_nox)
    uint16_t raw[4];
    if (sen66_read_words(0x0405, raw, 4, 50) == ESP_OK) {
        ESP_LOGI(TAG, "=== Raw Sensor Values ===");
        ESP_LOGI(TAG, "  Raw Humidity: %.1f %%RH", (int16_t)raw[0] / 100.0f);
        ESP_LOGI(TAG, "  Raw Temp:     %.2f C", (int16_t)raw[1] / 200.0f);
        ESP_LOGI(TAG, "  Raw VOC:      %u ticks", raw[2]);
        ESP_LOGI(TAG, "  Raw NOx:      %u ticks", raw[3]);
    } else {
        ESP_LOGE(TAG, "Failed to read raw values");
    }
}

void app_main(void)
{
    // Init I2C bus
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus_handle));
    ESP_LOGI(TAG, "I2C bus initialized");

    // Scan bus
    i2c_scan();

    // Add SEN66
    i2c_device_config_t sen66_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SEN66_ADDR,
        .scl_speed_hz = 100000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &sen66_cfg, &sen66_dev));

    // Print device info before starting measurement
    sen66_print_device_info();

    // Start continuous measurement (cmd 0x0021)
    esp_err_t ret = sen66_cmd(0x0021);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "SEN66 measurement started");
    } else {
        ESP_LOGE(TAG, "SEN66 start failed: %s", esp_err_to_name(ret));
        return;
    }

    // Wait for initial warmup
    ESP_LOGI(TAG, "Waiting 5s for sensor warmup...");
    vTaskDelay(pdMS_TO_TICKS(5000));

    // Read loop
    while (1) {
        ESP_LOGI(TAG, "-------------------------------------------");
        sen66_read_measurements();
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
