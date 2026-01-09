/**
 * @file i2c_scanner.c
 * @brief Simple I2C Scanner to detect devices on the bus
 */

#include <stdio.h>
#include "esp_log.h"
#include "driver/i2c.h"

#define TAG "I2C_SCANNER"

#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_SCL_IO 7
#define I2C_MASTER_SDA_IO 6
#define I2C_MASTER_FREQ_HZ 100000

void i2c_scanner_init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };

    esp_err_t ret = i2c_param_config(I2C_MASTER_NUM, &conf);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "I2C config failed");
        return;
    }

    ret = i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "I2C install failed");
        return;
    }

    ESP_LOGI(TAG, "I2C Scanner initialized (SDA=GPIO%d, SCL=GPIO%d)",
             I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO);
}

void i2c_scanner_scan(void)
{
    ESP_LOGI(TAG, "Starting I2C scan...");
    ESP_LOGI(TAG, "     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f");

    int devices_found = 0;

    for (int i = 0; i < 128; i += 16)
    {
        printf("%02x: ", i);

        for (int j = 0; j < 16; j++)
        {
            uint8_t address = i + j;

            // Skip reserved addresses
            if (address < 0x03 || address > 0x77)
            {
                printf("   ");
                continue;
            }

            // Try to communicate with the device
            i2c_cmd_handle_t cmd = i2c_cmd_link_create();
            i2c_master_start(cmd);
            i2c_master_write_byte(cmd, (address << 1) | I2C_MASTER_WRITE, true);
            i2c_master_stop(cmd);

            esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, 50 / portTICK_PERIOD_MS);
            i2c_cmd_link_delete(cmd);

            if (ret == ESP_OK)
            {
                printf("%02x ", address);
                devices_found++;
            }
            else
            {
                printf("-- ");
            }
        }
        printf("\n");
    }

    if (devices_found == 0)
    {
        ESP_LOGW(TAG, "No I2C devices found!");
        ESP_LOGW(TAG, "Check your wiring:");
        ESP_LOGW(TAG, "  - SDA: GPIO%d", I2C_MASTER_SDA_IO);
        ESP_LOGW(TAG, "  - SCL: GPIO%d", I2C_MASTER_SCL_IO);
        ESP_LOGW(TAG, "  - VCC: 3.3V");
        ESP_LOGW(TAG, "  - GND: GND");
    }
    else
    {
        ESP_LOGI(TAG, "Found %d device(s) on I2C bus", devices_found);
        ESP_LOGI(TAG, "BME680 typically uses address 0x76 or 0x77");
    }
}
