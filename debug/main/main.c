#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

#define I2C_SDA_PIN 6
#define I2C_SCL_PIN 7
#define SEN66_ADDR  0x6B
#define SCD30_ADDR  0x61

static const char *TAG = "i2c_test";

static i2c_master_bus_handle_t bus_handle;

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

// SCD30: send 2-byte command
static esp_err_t scd30_send_cmd(i2c_master_dev_handle_t dev, uint16_t cmd)
{
    uint8_t buf[2] = { cmd >> 8, cmd & 0xFF };
    return i2c_master_transmit(dev, buf, 2, 100);
}

// SCD30: send command with argument (+ CRC)
static esp_err_t scd30_send_cmd_arg(i2c_master_dev_handle_t dev, uint16_t cmd, uint16_t arg)
{
    // CRC-8 for SCD30 (poly 0x31, init 0xFF)
    uint8_t data[2] = { arg >> 8, arg & 0xFF };
    uint8_t crc = 0xFF;
    for (int i = 0; i < 2; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : crc << 1;
    }
    uint8_t buf[5] = { cmd >> 8, cmd & 0xFF, data[0], data[1], crc };
    return i2c_master_transmit(dev, buf, 5, 100);
}

// SEN66: read measured values (cmd 0x0300)
// Wire format: 9 values, each is 2 bytes data + 1 byte CRC = 27 bytes total
static void sen66_read(i2c_master_dev_handle_t dev)
{
    uint8_t cmd[2] = { 0x03, 0x00 };
    uint8_t buf[27]; // 9 values * 3 bytes (2 data + 1 crc)

    esp_err_t ret = i2c_master_transmit(dev, cmd, 2, 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SEN66 cmd send failed: %s", esp_err_to_name(ret));
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(50));

    ret = i2c_master_receive(dev, buf, sizeof(buf), 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SEN66 read failed: %s", esp_err_to_name(ret));
        return;
    }

    // Parse: each value is 2 bytes data + 1 byte CRC (skip CRC)
    // 0: PM1.0, 1: PM2.5, 2: PM4.0, 3: PM10, 4: Humidity, 5: Temp,
    // 6: VOC, 7: NOx, 8: CO2
    uint16_t uraw[9];
    int16_t sraw[9];
    for (int i = 0; i < 9; i++) {
        uraw[i] = (buf[i * 3] << 8) | buf[i * 3 + 1];
        sraw[i] = (int16_t)uraw[i];
    }

    ESP_LOGI(TAG, "=== SEN66 Readings ===");
    ESP_LOGI(TAG, "  PM1.0:  %.1f ug/m3", uraw[0] / 10.0f);
    ESP_LOGI(TAG, "  PM2.5:  %.1f ug/m3", uraw[1] / 10.0f);
    ESP_LOGI(TAG, "  PM4.0:  %.1f ug/m3", uraw[2] / 10.0f);
    ESP_LOGI(TAG, "  PM10:   %.1f ug/m3", uraw[3] / 10.0f);
    ESP_LOGI(TAG, "  Humidity: %.1f %%RH", sraw[4] / 100.0f);
    ESP_LOGI(TAG, "  Temp:   %.1f C", sraw[5] / 200.0f);
    ESP_LOGI(TAG, "  VOC:    %d (index)", sraw[6] / 10);
    ESP_LOGI(TAG, "  NOx:    %d (index)", sraw[7] / 10);
    ESP_LOGI(TAG, "  CO2:    %u ppm", uraw[8]);

    // Dump raw hex for debugging
    ESP_LOGI(TAG, "  RAW: %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x",
        buf[0],buf[1], buf[3],buf[4], buf[6],buf[7], buf[9],buf[10],
        buf[12],buf[13], buf[15],buf[16], buf[18],buf[19], buf[21],buf[22], buf[24],buf[25]);
}

// SCD30: read measurement
static void scd30_read(i2c_master_dev_handle_t dev)
{
    // Check data ready (cmd 0x0202)
    uint8_t cmd_ready[2] = { 0x02, 0x02 };
    uint8_t resp[3];
    esp_err_t ret = i2c_master_transmit_receive(dev, cmd_ready, 2, resp, 3, 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SCD30 ready check failed: %s", esp_err_to_name(ret));
        return;
    }
    uint16_t ready = (resp[0] << 8) | resp[1];
    if (!ready) {
        ESP_LOGI(TAG, "SCD30: data not ready yet");
        return;
    }

    // Read measurement (cmd 0x0300)
    uint8_t cmd_read[2] = { 0x03, 0x00 };
    uint8_t buf[18]; // 3 floats * (4 bytes + 2 CRC)
    ret = i2c_master_transmit_receive(dev, cmd_read, 2, buf, 18, 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SCD30 read failed: %s", esp_err_to_name(ret));
        return;
    }

    // Each float: 2 bytes MSB + CRC + 2 bytes LSB + CRC
    uint32_t co2_raw = (buf[0] << 24) | (buf[1] << 16) | (buf[3] << 8) | buf[4];
    uint32_t temp_raw = (buf[6] << 24) | (buf[7] << 16) | (buf[9] << 8) | buf[10];
    uint32_t hum_raw = (buf[12] << 24) | (buf[13] << 16) | (buf[15] << 8) | buf[16];

    float co2, temp, hum;
    memcpy(&co2, &co2_raw, 4);
    memcpy(&temp, &temp_raw, 4);
    memcpy(&hum, &hum_raw, 4);

    ESP_LOGI(TAG, "=== SCD30 Readings ===");
    ESP_LOGI(TAG, "  CO2:      %.1f ppm", co2);
    ESP_LOGI(TAG, "  Temp:     %.1f C", temp);
    ESP_LOGI(TAG, "  Humidity: %.1f %%RH", hum);
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

    // Scan first
    i2c_scan();

    // Add SEN66 device
    i2c_device_config_t sen66_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SEN66_ADDR,
        .scl_speed_hz = 100000,
    };
    i2c_master_dev_handle_t sen66_dev;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &sen66_cfg, &sen66_dev));

    // Start SEN66 measurement (cmd 0x0021)
    uint8_t sen66_start[2] = { 0x00, 0x21 };
    esp_err_t ret = i2c_master_transmit(sen66_dev, sen66_start, 2, 100);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "SEN66 measurement started");
    } else {
        ESP_LOGW(TAG, "SEN66 start failed (maybe not connected): %s", esp_err_to_name(ret));
    }

    // Add SCD30 device
    i2c_device_config_t scd30_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SCD30_ADDR,
        .scl_speed_hz = 100000,
    };
    i2c_master_dev_handle_t scd30_dev;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &scd30_cfg, &scd30_dev));

    // Start SCD30 continuous measurement (cmd 0x0010, arg 0 = ambient pressure)
    ret = scd30_send_cmd_arg(scd30_dev, 0x0010, 0);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "SCD30 continuous measurement started");
    } else {
        ESP_LOGW(TAG, "SCD30 start failed (maybe not connected): %s", esp_err_to_name(ret));
    }

    // Wait for sensors to warm up
    ESP_LOGI(TAG, "Waiting 5s for sensors to warm up...");
    vTaskDelay(pdMS_TO_TICKS(5000));

    // Read loop
    while (1) {
        ESP_LOGI(TAG, "--- Reading sensors ---");
        sen66_read(sen66_dev);
        scd30_read(scd30_dev);
        vTaskDelay(pdMS_TO_TICKS(5000)); // Read every 5 seconds
    }
}
