/**
 * @file main.c
 * @brief Đọc dữ liệu từ cảm biến BME680 và logging ra terminal
 *
 * Ứng dụng đơn giản đọc dữ liệu từ cảm biến BME680 (nhiệt độ, độ ẩm, áp suất,
 * chất lượng không khí) và hiển thị trên ESP-IDF Monitor (terminal).
 *
 * Features:
 * - Đọc dữ liệu BME680 qua I2C mỗi 3 giây
 * - Logging chi tiết ra terminal
 * - Buzzer cảnh báo nhiệt độ cao (GPIO 2)
 *
 * @author Senior Embedded Developer
 * @version 2.0 - Terminal Logging Only
 */

#include "bme68x.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "i2c_scanner.h"
#include "nvs_flash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =========================== Configuration ============================= */

#define TAG "BME680_LOGGER"

/* I2C Configuration */
#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_SCL_IO 7       // GPIO7 cho SCL
#define I2C_MASTER_SDA_IO 6       // GPIO6 cho SDA
#define I2C_MASTER_FREQ_HZ 100000 // 100kHz
#define I2C_MASTER_TX_BUF_DISABLE 0
#define I2C_MASTER_RX_BUF_DISABLE 0
#define I2C_MASTER_TIMEOUT_MS 1000

/* BME680 Configuration */
#define BME680_I2C_ADDR BME68X_I2C_ADDR_HIGH // 0x77

/* Buzzer Configuration */
#define BUZZER_GPIO GPIO_NUM_2  // GPIO2 cho Buzzer I/O
#define TEMP_THRESHOLD 100.0f   // Ngưỡng nhiệt độ cảnh báo (°C)
#define BUZZER_ON_TIME_MS 1000  // Thời gian buzzer kêu (ms)
#define BUZZER_OFF_TIME_MS 1000 // Thời gian buzzer tắt (ms)

/* Sensor Reading Configuration */
#define SENSOR_READ_INTERVAL_MS 3000 // Đọc sensor mỗi 3 giây

/* ========================== Global Variables ============================ */

/* BME680 device structure */
static struct bme68x_dev gas_sensor;
static struct bme68x_conf conf;
static struct bme68x_heatr_conf heatr_conf;

/* Sensor data structure */
typedef struct {
  float temperature;
  float humidity;
  float pressure;
  float gas_resistance;
  bool gas_valid;
  bool data_valid;
  uint32_t read_count;
} sensor_data_t;

static sensor_data_t g_sensor_data = {0};
static SemaphoreHandle_t g_sensor_mutex = NULL;

/* Buzzer state */
static volatile bool g_buzzer_active = false;

/* ========================== I2C Functions =============================== */

/**
 * @brief I2C read function cho BME680 driver
 */
static BME68X_INTF_RET_TYPE bme68x_i2c_read(uint8_t reg_addr, uint8_t *reg_data,
                                            uint32_t len, void *intf_ptr) {
  uint8_t dev_addr = *(uint8_t *)intf_ptr;

  esp_err_t ret = i2c_master_write_read_device(
      I2C_MASTER_NUM, dev_addr, &reg_addr, 1, reg_data, len,
      I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);

  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "I2C read failed: %s", esp_err_to_name(ret));
    return BME68X_E_COM_FAIL;
  }

  return BME68X_OK;
}

/**
 * @brief I2C write function cho BME680 driver
 */
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

  esp_err_t ret =
      i2c_master_write_to_device(I2C_MASTER_NUM, dev_addr, write_buf, len + 1,
                                 I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
  free(write_buf);

  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "I2C write failed: %s", esp_err_to_name(ret));
    return BME68X_E_COM_FAIL;
  }

  return BME68X_OK;
}

/**
 * @brief Delay function cho BME680 driver
 */
static void bme68x_delay_us(uint32_t period, void *intf_ptr) {
  (void)intf_ptr;
  if (period >= 1000) {
    vTaskDelay(pdMS_TO_TICKS(period / 1000));
  } else {
    esp_rom_delay_us(period);
  }
}

/**
 * @brief Khởi tạo I2C master
 */
static esp_err_t i2c_master_init(void) {
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

  ret = i2c_driver_install(I2C_MASTER_NUM, i2c_conf.mode,
                           I2C_MASTER_RX_BUF_DISABLE, I2C_MASTER_TX_BUF_DISABLE,
                           0);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "I2C driver install failed: %s", esp_err_to_name(ret));
    return ret;
  }

  ESP_LOGI(TAG, "I2C master initialized (SDA: GPIO%d, SCL: GPIO%d)",
           I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO);
  return ESP_OK;
}

/* ==================== BME680 Sensor Functions =========================== */

/**
 * @brief Khởi tạo cảm biến BME680
 */
static esp_err_t bme680_init_sensor(void) {
  int8_t rslt;
  static uint8_t dev_addr = BME680_I2C_ADDR;

  /* Cấu hình BME68x device structure */
  gas_sensor.intf = BME68X_I2C_INTF;
  gas_sensor.read = bme68x_i2c_read;
  gas_sensor.write = bme68x_i2c_write;
  gas_sensor.delay_us = bme68x_delay_us;
  gas_sensor.intf_ptr = &dev_addr;
  gas_sensor.amb_temp = 25; // Nhiệt độ môi trường mặc định

  /* Khởi tạo sensor */
  rslt = bme68x_init(&gas_sensor);
  if (rslt != BME68X_OK) {
    ESP_LOGE(TAG, "BME680 init failed with error code: %d", rslt);
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "BME680 initialized successfully!");
  ESP_LOGI(TAG, "  - Chip ID: 0x%02X", gas_sensor.chip_id);
  ESP_LOGI(TAG, "  - Variant ID: 0x%02X", gas_sensor.variant_id);

  /* Lấy cấu hình hiện tại */
  rslt = bme68x_get_conf(&conf, &gas_sensor);
  if (rslt != BME68X_OK) {
    ESP_LOGE(TAG, "Failed to get sensor configuration: %d", rslt);
    return ESP_FAIL;
  }

  /* Cấu hình oversampling cho độ chính xác cao */
  conf.os_hum = BME68X_OS_2X;  // Humidity oversampling x2
  conf.os_pres = BME68X_OS_4X; // Pressure oversampling x4
  conf.os_temp = BME68X_OS_8X; // Temperature oversampling x8
  conf.filter = BME68X_FILTER_SIZE_3;
  conf.odr = BME68X_ODR_NONE;

  rslt = bme68x_set_conf(&conf, &gas_sensor);
  if (rslt != BME68X_OK) {
    ESP_LOGE(TAG, "Failed to set sensor configuration: %d", rslt);
    return ESP_FAIL;
  }

  /* Cấu hình gas heater */
  heatr_conf.enable = BME68X_ENABLE;
  heatr_conf.heatr_temp = 320; // 320°C
  heatr_conf.heatr_dur = 150;  // 150ms

  rslt = bme68x_set_heatr_conf(BME68X_FORCED_MODE, &heatr_conf, &gas_sensor);
  if (rslt != BME68X_OK) {
    ESP_LOGE(TAG, "Failed to set heater configuration: %d", rslt);
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "BME680 sensor configured:");
  ESP_LOGI(TAG, "  - Temp Oversampling: x8");
  ESP_LOGI(TAG, "  - Pressure Oversampling: x4");
  ESP_LOGI(TAG, "  - Humidity Oversampling: x2");
  ESP_LOGI(TAG, "  - Heater Temp: 320C, Duration: 150ms");

  return ESP_OK;
}

/**
 * @brief Đọc dữ liệu từ BME680
 */
static esp_err_t bme680_read_data(struct bme68x_data *data) {
  int8_t rslt;
  uint8_t n_fields;

  /* Đặt sensor vào forced mode */
  rslt = bme68x_set_op_mode(BME68X_FORCED_MODE, &gas_sensor);
  if (rslt != BME68X_OK) {
    ESP_LOGE(TAG, "Failed to set sensor mode: %d", rslt);
    return ESP_FAIL;
  }

  /* Tính thời gian delay cần thiết */
  uint32_t del_period =
      bme68x_get_meas_dur(BME68X_FORCED_MODE, &conf, &gas_sensor) +
      (heatr_conf.heatr_dur * 1000);

  /* Delay cho phép sensor hoàn thành đo */
  gas_sensor.delay_us(del_period, gas_sensor.intf_ptr);

  /* Đọc dữ liệu */
  rslt = bme68x_get_data(BME68X_FORCED_MODE, data, &n_fields, &gas_sensor);
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

/* ======================= Buzzer Functions =============================== */

/**
 * @brief Khởi tạo GPIO cho Buzzer
 */
static esp_err_t buzzer_init(void) {
  gpio_config_t io_conf = {
      .pin_bit_mask = (1ULL << BUZZER_GPIO),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };

  esp_err_t ret = gpio_config(&io_conf);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to configure buzzer GPIO: %s", esp_err_to_name(ret));
    return ret;
  }

  /* Đảm bảo buzzer tắt khi khởi động */
  gpio_set_level(BUZZER_GPIO, 0);

  ESP_LOGI(TAG, "Buzzer initialized on GPIO%d", BUZZER_GPIO);
  return ESP_OK;
}

/**
 * @brief Bật buzzer
 */
static void buzzer_on(void) { gpio_set_level(BUZZER_GPIO, 1); }

/**
 * @brief Tắt buzzer
 */
static void buzzer_off(void) { gpio_set_level(BUZZER_GPIO, 0); }

/**
 * @brief Task điều khiển buzzer cảnh báo
 */
static void buzzer_task(void *pvParameters) {
  ESP_LOGI(TAG, "Buzzer task started");

  while (1) {
    if (g_buzzer_active) {
      buzzer_on();
      ESP_LOGW(TAG, "[BUZZER] ON - Temperature ALERT!");
      vTaskDelay(pdMS_TO_TICKS(BUZZER_ON_TIME_MS));

      buzzer_off();
      vTaskDelay(pdMS_TO_TICKS(BUZZER_OFF_TIME_MS));
    } else {
      buzzer_off();
      vTaskDelay(pdMS_TO_TICKS(500));
    }
  }
}

/* ======================= Sensor Task ==================================== */

/**
 * @brief In đường kẻ phân cách
 */
static void print_separator(void) {
  ESP_LOGI(TAG, "╔══════════════════════════════════════════════════════════╗");
}

static void print_separator_end(void) {
  ESP_LOGI(TAG, "╚══════════════════════════════════════════════════════════╝");
}

/**
 * @brief Task đọc dữ liệu sensor và log ra terminal
 */
static void sensor_read_task(void *pvParameters) {
  ESP_LOGI(TAG, "Sensor reading task started - Interval: %d ms",
           SENSOR_READ_INTERVAL_MS);

  while (1) {
    struct bme68x_data data;

    if (bme680_read_data(&data) == ESP_OK) {
      /* Cập nhật dữ liệu global thread-safe */
      if (xSemaphoreTake(g_sensor_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_sensor_data.temperature = data.temperature;
        g_sensor_data.humidity = data.humidity;
        g_sensor_data.pressure = data.pressure;
        g_sensor_data.gas_resistance = (float)data.gas_resistance;
        g_sensor_data.gas_valid =
            (data.status & BME68X_GASM_VALID_MSK) ? true : false;
        g_sensor_data.data_valid = true;
        g_sensor_data.read_count++;
        xSemaphoreGive(g_sensor_mutex);
      }

      /* Log dữ liệu chi tiết ra terminal */
      print_separator();
      ESP_LOGI(TAG, "║  BME680 SENSOR DATA - Reading #%lu",
               g_sensor_data.read_count);
      ESP_LOGI(TAG,
               "╠══════════════════════════════════════════════════════════╣");
      ESP_LOGI(TAG, "║  🌡️  Temperature : %8.2f °C                          ║",
               data.temperature);
      ESP_LOGI(TAG, "║  💧 Humidity    : %8.2f %%                           ║",
               data.humidity);
      ESP_LOGI(TAG, "║  📊 Pressure    : %8.2f hPa                          ║",
               data.pressure / 100.0f);

      if (data.status & BME68X_GASM_VALID_MSK) {
        ESP_LOGI(TAG,
                 "║  💨 Gas Resist. : %8.0f Ohms (Valid)                 ║",
                 (float)data.gas_resistance);
      } else {
        ESP_LOGW(
            TAG,
            "║  💨 Gas Resist. : N/A (Invalid reading)                  ║");
      }

      /* Hiển thị trạng thái cảnh báo */
      if (data.temperature > TEMP_THRESHOLD) {
        ESP_LOGW(
            TAG,
            "╠══════════════════════════════════════════════════════════╣");
        ESP_LOGW(TAG,
                 "║  🚨 ALERT: Temperature exceeds %.1f°C threshold!       ║",
                 TEMP_THRESHOLD);
        if (!g_buzzer_active) {
          g_buzzer_active = true;
        }
      } else if (data.temperature > (TEMP_THRESHOLD - 20)) {
        ESP_LOGW(
            TAG,
            "╠══════════════════════════════════════════════════════════╣");
        ESP_LOGW(
            TAG,
            "║  ⚠️  WARNING: Temperature approaching threshold!          ║");
        g_buzzer_active = false;
      } else {
        ESP_LOGI(
            TAG,
            "╠══════════════════════════════════════════════════════════╣");
        ESP_LOGI(
            TAG,
            "║  ✅ Status: Normal                                        ║");
        g_buzzer_active = false;
      }

      print_separator_end();
      ESP_LOGI(TAG, ""); // Empty line for readability

    } else {
      ESP_LOGE(TAG, "╔═══════════════════════════════════════╗");
      ESP_LOGE(TAG, "║  ❌ Failed to read sensor data!       ║");
      ESP_LOGE(TAG, "╚═══════════════════════════════════════╝");
    }

    vTaskDelay(pdMS_TO_TICKS(SENSOR_READ_INTERVAL_MS));
  }
}

/* ========================= Main Application ============================= */

/**
 * @brief Main application entry point
 */
void app_main(void) {
  esp_err_t ret;

  /* Banner khởi động */
  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "╔══════════════════════════════════════════════════════════╗");
  ESP_LOGI(TAG, "║                                                          ║");
  ESP_LOGI(TAG, "║     BME680 Environmental Sensor - Terminal Logger        ║");
  ESP_LOGI(TAG, "║                                                          ║");
  ESP_LOGI(TAG, "║     Version: 2.0 (No WebServer)                          ║");
  ESP_LOGI(TAG, "║     ESP-IDF: %s                                     ║",
           esp_get_idf_version());
  ESP_LOGI(TAG, "║                                                          ║");
  ESP_LOGI(TAG, "╚══════════════════════════════════════════════════════════╝");
  ESP_LOGI(TAG, "");

  /* Khởi tạo NVS */
  ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);
  ESP_LOGI(TAG, "[OK] NVS Flash initialized");

  /* Tạo mutex cho sensor data */
  g_sensor_mutex = xSemaphoreCreateMutex();
  if (g_sensor_mutex == NULL) {
    ESP_LOGE(TAG, "[FAIL] Failed to create sensor mutex");
    return;
  }
  ESP_LOGI(TAG, "[OK] Sensor mutex created");

  /* Khởi tạo Buzzer GPIO */
  ret = buzzer_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "[FAIL] Failed to initialize buzzer");
    return;
  }

  /* Chạy I2C Scanner để phát hiện thiết bị */
  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "=== Running I2C Scanner ===");
  i2c_scanner_init();
  i2c_scanner_scan();
  vTaskDelay(pdMS_TO_TICKS(1000));

  /* Xóa I2C driver trước khi khởi tạo lại */
  i2c_driver_delete(I2C_MASTER_NUM);

  /* Khởi tạo I2C master */
  ret = i2c_master_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "[FAIL] Failed to initialize I2C");
    return;
  }

  /* Khởi tạo BME680 sensor */
  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "=== Initializing BME680 Sensor ===");
  ret = bme680_init_sensor();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "[FAIL] Failed to initialize BME680 sensor");
    ESP_LOGE(TAG, "Please check:");
    ESP_LOGE(TAG, "  - BME680 wiring (SDA=GPIO%d, SCL=GPIO%d)",
             I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO);
    ESP_LOGE(TAG, "  - BME680 I2C address (0x%02X)", BME680_I2C_ADDR);
    return;
  }

  /* Tạo task đọc sensor */
  xTaskCreate(sensor_read_task, "sensor_task", 4096, NULL, 5, NULL);

  /* Tạo task điều khiển buzzer */
  xTaskCreate(buzzer_task, "buzzer_task", 2048, NULL, 4, NULL);

  /* Thông báo hoàn tất khởi tạo */
  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "╔══════════════════════════════════════════════════════════╗");
  ESP_LOGI(TAG, "║  System initialized successfully!                        ║");
  ESP_LOGI(TAG, "╠══════════════════════════════════════════════════════════╣");
  ESP_LOGI(TAG,
           "║  📍 I2C:                                                  ║");
  ESP_LOGI(TAG, "║     - SDA: GPIO%d                                         ║",
           I2C_MASTER_SDA_IO);
  ESP_LOGI(TAG, "║     - SCL: GPIO%d                                         ║",
           I2C_MASTER_SCL_IO);
  ESP_LOGI(TAG, "║     - Frequency: %d Hz                               ║",
           I2C_MASTER_FREQ_HZ);
  ESP_LOGI(TAG,
           "║  📍 Sensor: BME680 @ 0x%02X                                ║",
           BME680_I2C_ADDR);
  ESP_LOGI(TAG,
           "║  📍 Buzzer: GPIO%d                                         ║",
           BUZZER_GPIO);
  ESP_LOGI(TAG, "║  📍 Temp Threshold: %.1f°C                               ║",
           TEMP_THRESHOLD);
  ESP_LOGI(TAG, "║  📍 Read Interval: %d ms                               ║",
           SENSOR_READ_INTERVAL_MS);
  ESP_LOGI(TAG, "╚══════════════════════════════════════════════════════════╝");
  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "Starting sensor readings...");
  ESP_LOGI(TAG, "");
}
