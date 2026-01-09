/**
 * @file i2c_config.c
 * @brief I2C Configuration and initialization implementation
 */

#include "i2c_config.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "I2C_CONFIG";

esp_err_t i2c_master_init(void) {
  i2c_config_t i2c_conf = {
      .mode = I2C_MODE_MASTER,
      .sda_io_num = I2C_MASTER_SDA_IO,
      .scl_io_num = I2C_MASTER_SCL_IO,
      .sda_pullup_en = GPIO_PULLUP_ENABLE,
      .scl_pullup_en = GPIO_PULLUP_ENABLE,
      .master.clk_speed = I2C_MASTER_FREQ_HZ,
  };

  esp_err_t ret = i2c_param_config(I2C_MASTER_NUM, &i2c_conf);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "I2C param config failed: %s", esp_err_to_name(ret));
    return ret;
  }

  ret = i2c_driver_install(I2C_MASTER_NUM, i2c_conf.mode, 0, 0, 0);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "I2C driver install failed: %s", esp_err_to_name(ret));
    return ret;
  }

  ESP_LOGI(TAG, "I2C master initialized (SDA: GPIO%d, SCL: GPIO%d, Freq: %dHz)",
           I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO, I2C_MASTER_FREQ_HZ);
  return ESP_OK;
}

esp_err_t i2c_master_deinit(void) { return i2c_driver_delete(I2C_MASTER_NUM); }

i2c_port_t i2c_get_port(void) { return I2C_MASTER_NUM; }

TickType_t i2c_get_timeout_ticks(void) {
  return I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS;
}
