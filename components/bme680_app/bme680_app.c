/**
 * @file bme680_app.c
 * @brief BME680 Application wrapper implementation
 */

#include "bme680_app.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "i2c_config.h"
#include <stdlib.h>
#include <string.h>


static const char *TAG = "BME680_APP";

/* BME680 device structures */
static struct bme68x_dev g_gas_sensor;
static struct bme68x_conf g_conf;
static struct bme68x_heatr_conf g_heatr_conf;

/* Sensor data with mutex protection */
static bme680_sensor_data_t g_sensor_data = {0};
static SemaphoreHandle_t g_sensor_mutex = NULL;

/* I2C address storage */
static uint8_t g_dev_addr = BME680_I2C_ADDR;

/* ========================== I2C Interface Functions =========================
 */

static BME68X_INTF_RET_TYPE bme68x_i2c_read(uint8_t reg_addr, uint8_t *reg_data,
                                            uint32_t len, void *intf_ptr) {
  uint8_t dev_addr = *(uint8_t *)intf_ptr;

  esp_err_t ret =
      i2c_master_write_read_device(i2c_get_port(), dev_addr, &reg_addr, 1,
                                   reg_data, len, i2c_get_timeout_ticks());

  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "I2C read failed: %s", esp_err_to_name(ret));
    return BME68X_E_COM_FAIL;
  }

  return BME68X_OK;
}

static BME68X_INTF_RET_TYPE bme68x_i2c_write(uint8_t reg_addr,
                                             const uint8_t *reg_data,
                                             uint32_t len, void *intf_ptr) {
  uint8_t dev_addr = *(uint8_t *)intf_ptr;
  uint8_t *write_buf = malloc(len + 1);
  if (write_buf == NULL) {
    ESP_LOGE(TAG, "Memory allocation failed");
    return BME68X_E_COM_FAIL;
  }

  write_buf[0] = reg_addr;
  memcpy(&write_buf[1], reg_data, len);

  esp_err_t ret = i2c_master_write_to_device(
      i2c_get_port(), dev_addr, write_buf, len + 1, i2c_get_timeout_ticks());
  free(write_buf);

  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "I2C write failed: %s", esp_err_to_name(ret));
    return BME68X_E_COM_FAIL;
  }

  return BME68X_OK;
}

static void bme68x_delay_us(uint32_t period, void *intf_ptr) {
  (void)intf_ptr;
  if (period >= 1000) {
    vTaskDelay(pdMS_TO_TICKS(period / 1000));
  } else {
    esp_rom_delay_us(period);
  }
}

/* ========================== Public Functions ================================
 */

esp_err_t bme680_app_create_mutex(void) {
  g_sensor_mutex = xSemaphoreCreateMutex();
  if (g_sensor_mutex == NULL) {
    ESP_LOGE(TAG, "Failed to create sensor mutex");
    return ESP_FAIL;
  }
  ESP_LOGI(TAG, "Sensor mutex created");
  return ESP_OK;
}

esp_err_t bme680_app_init(void) {
  int8_t rslt;

  /* Configure BME68x device structure */
  g_gas_sensor.intf = BME68X_I2C_INTF;
  g_gas_sensor.read = bme68x_i2c_read;
  g_gas_sensor.write = bme68x_i2c_write;
  g_gas_sensor.delay_us = bme68x_delay_us;
  g_gas_sensor.intf_ptr = &g_dev_addr;
  g_gas_sensor.amb_temp = 25;

  /* Initialize sensor */
  rslt = bme68x_init(&g_gas_sensor);
  if (rslt != BME68X_OK) {
    ESP_LOGE(TAG, "BME680 init failed with error code: %d", rslt);
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "BME680 initialized successfully!");
  ESP_LOGI(TAG, "  - Chip ID: 0x%02X", g_gas_sensor.chip_id);
  ESP_LOGI(TAG, "  - Variant ID: 0x%02X", g_gas_sensor.variant_id);

  /* Get current configuration */
  rslt = bme68x_get_conf(&g_conf, &g_gas_sensor);
  if (rslt != BME68X_OK) {
    ESP_LOGE(TAG, "Failed to get sensor configuration: %d", rslt);
    return ESP_FAIL;
  }

  /* Configure oversampling for high accuracy */
  g_conf.os_hum = BME68X_OS_2X;
  g_conf.os_pres = BME68X_OS_4X;
  g_conf.os_temp = BME68X_OS_8X;
  g_conf.filter = BME68X_FILTER_SIZE_3;
  g_conf.odr = BME68X_ODR_NONE;

  rslt = bme68x_set_conf(&g_conf, &g_gas_sensor);
  if (rslt != BME68X_OK) {
    ESP_LOGE(TAG, "Failed to set sensor configuration: %d", rslt);
    return ESP_FAIL;
  }

  /* Configure gas heater */
  g_heatr_conf.enable = BME68X_ENABLE;
  g_heatr_conf.heatr_temp = 320;
  g_heatr_conf.heatr_dur = 150;

  rslt =
      bme68x_set_heatr_conf(BME68X_FORCED_MODE, &g_heatr_conf, &g_gas_sensor);
  if (rslt != BME68X_OK) {
    ESP_LOGE(TAG, "Failed to set heater configuration: %d", rslt);
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "BME680 sensor configured:");
  ESP_LOGI(TAG, "  - Temp Oversampling: x8");
  ESP_LOGI(TAG, "  - Pressure Oversampling: x4");
  ESP_LOGI(TAG, "  - Humidity Oversampling: x2");
  ESP_LOGI(TAG, "  - Heater: 320C, 150ms");

  return ESP_OK;
}

esp_err_t bme680_app_read(struct bme68x_data *data) {
  int8_t rslt;
  uint8_t n_fields;

  /* Set sensor to forced mode */
  rslt = bme68x_set_op_mode(BME68X_FORCED_MODE, &g_gas_sensor);
  if (rslt != BME68X_OK) {
    ESP_LOGE(TAG, "Failed to set sensor mode: %d", rslt);
    return ESP_FAIL;
  }

  /* Calculate required delay */
  uint32_t del_period =
      bme68x_get_meas_dur(BME68X_FORCED_MODE, &g_conf, &g_gas_sensor) +
      (g_heatr_conf.heatr_dur * 1000);

  /* Delay for measurement completion */
  g_gas_sensor.delay_us(del_period, g_gas_sensor.intf_ptr);

  /* Read data */
  rslt = bme68x_get_data(BME68X_FORCED_MODE, data, &n_fields, &g_gas_sensor);
  if (rslt != BME68X_OK) {
    ESP_LOGE(TAG, "Failed to get sensor data: %d", rslt);
    return ESP_FAIL;
  }

  if (n_fields == 0) {
    ESP_LOGW(TAG, "No new data available");
    return ESP_ERR_NOT_FOUND;
  }

  return ESP_OK;
}

void bme680_app_update_data(const struct bme68x_data *raw_data) {
  if (g_sensor_mutex == NULL)
    return;

  if (xSemaphoreTake(g_sensor_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    g_sensor_data.temperature = raw_data->temperature;
    g_sensor_data.humidity = raw_data->humidity;
    g_sensor_data.pressure = raw_data->pressure;
    g_sensor_data.gas_resistance = (float)raw_data->gas_resistance;
    g_sensor_data.gas_valid =
        (raw_data->status & BME68X_GASM_VALID_MSK) ? true : false;
    g_sensor_data.data_valid = true;
    g_sensor_data.read_count++;
    xSemaphoreGive(g_sensor_mutex);
  }
}

esp_err_t bme680_app_get_data(bme680_sensor_data_t *data) {
  if (g_sensor_mutex == NULL || data == NULL)
    return ESP_FAIL;

  if (xSemaphoreTake(g_sensor_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    *data = g_sensor_data;
    xSemaphoreGive(g_sensor_mutex);
    return ESP_OK;
  }
  return ESP_FAIL;
}

float bme680_app_get_threshold(void) { return TEMP_THRESHOLD; }

uint8_t bme680_app_get_address(void) { return BME680_I2C_ADDR; }
