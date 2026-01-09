/**
 * @file main.c
 * @brief BME680 Environmental Sensor - Terminal Logger
 *
 * Simple application that reads BME680 sensor data and logs to terminal.
 */

#include "bme680_app.h"
#include "buzzer.h"
#include "i2c_config.h"

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

/* ========================== Configuration ================================ */

#define TAG "MAIN"
#define SENSOR_READ_INTERVAL_MS 3000 // Read sensor every 3 seconds

/* ========================== Sensor Task ================================== */

/**
 * @brief Print formatted separator lines
 */
static void print_header(void) {
  ESP_LOGI(TAG, "╔══════════════════════════════════════════════════════════╗");
}

static void print_footer(void) {
  ESP_LOGI(TAG, "╚══════════════════════════════════════════════════════════╝");
}

static void print_divider(void) {
  ESP_LOGI(TAG, "╠══════════════════════════════════════════════════════════╣");
}

/**
 * @brief Sensor reading task
 */
static void sensor_task(void *pvParameters) {
  (void)pvParameters; // Unused parameter
  ESP_LOGI(TAG, "Sensor task started - Interval: %d ms",
           SENSOR_READ_INTERVAL_MS);

  while (1) {
    struct bme68x_data raw_data;
    bme680_sensor_data_t sensor_data;

    if (bme680_app_read(&raw_data) == ESP_OK) {
      /* Update global data */
      bme680_app_update_data(&raw_data);
      bme680_app_get_data(&sensor_data);

      /* Log data to terminal */
      print_header();
      ESP_LOGI(TAG, "║  BME680 SENSOR DATA - Reading #%lu",
               sensor_data.read_count);
      print_divider();
      ESP_LOGI(TAG, "║  Temperature : %8.2f °C", raw_data.temperature);
      ESP_LOGI(TAG, "║  Humidity    : %8.2f %%", raw_data.humidity);
      ESP_LOGI(TAG, "║  Pressure    : %8.2f hPa", raw_data.pressure / 100.0f);

      if (raw_data.status & BME68X_GASM_VALID_MSK) {
        ESP_LOGI(TAG, "║  Gas Resist. : %8.0f Ohms",
                 (float)raw_data.gas_resistance);
      } else {
        ESP_LOGW(TAG, "║  Gas Resist. : N/A (Invalid)");
      }

      /* Check temperature threshold */
      float threshold = bme680_app_get_threshold();
      print_divider();

      if (raw_data.temperature > threshold) {
        ESP_LOGW(TAG, "║  ALERT: Temperature exceeds %.1f°C!", threshold);
        buzzer_set_active(true);
      } else if (raw_data.temperature > (threshold - 20)) {
        ESP_LOGW(TAG, "║  WARNING: Temperature approaching threshold!");
        buzzer_set_active(false);
      } else {
        ESP_LOGI(TAG, "║  Status: Normal");
        buzzer_set_active(false);
      }

      print_footer();
      ESP_LOGI(TAG, "");

    } else {
      ESP_LOGE(TAG, "Failed to read sensor data!");
    }

    vTaskDelay(pdMS_TO_TICKS(SENSOR_READ_INTERVAL_MS));
  }
}

/* ========================== Main Application ============================= */

/**
 * @brief Print startup banner
 */
static void print_banner(void) {
  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "╔══════════════════════════════════════════════════════════╗");
  ESP_LOGI(TAG, "║                                                          ║");
  ESP_LOGI(TAG, "║     BME680 Environmental Sensor - Terminal Logger        ║");
  ESP_LOGI(TAG, "║                                                          ║");
  ESP_LOGI(TAG, "║     ESP-IDF: %s                                     ║",
           esp_get_idf_version());
  ESP_LOGI(TAG, "║                                                          ║");
  ESP_LOGI(TAG, "╚══════════════════════════════════════════════════════════╝");
  ESP_LOGI(TAG, "");
}

/**
 * @brief Print system info after initialization
 */
static void print_system_info(void) {
  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "╔══════════════════════════════════════════════════════════╗");
  ESP_LOGI(TAG, "║  System initialized successfully!                        ║");
  ESP_LOGI(TAG, "╠══════════════════════════════════════════════════════════╣");
  ESP_LOGI(TAG, "║  I2C: SDA=GPIO%d, SCL=GPIO%d, Freq=%dHz          ║",
           I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO, I2C_MASTER_FREQ_HZ);
  ESP_LOGI(TAG, "║  BME680: Address=0x%02X                                   ║",
           bme680_app_get_address());
  ESP_LOGI(TAG, "║  Buzzer: GPIO%d                                           ║",
           buzzer_get_gpio());
  ESP_LOGI(TAG, "║  Temp Threshold: %.1f°C                                  ║",
           bme680_app_get_threshold());
  ESP_LOGI(TAG, "║  Read Interval: %d ms                                  ║",
           SENSOR_READ_INTERVAL_MS);
  ESP_LOGI(TAG, "╚══════════════════════════════════════════════════════════╝");
  ESP_LOGI(TAG, "");
}

/**
 * @brief Main application entry point
 */
void app_main(void) {
  esp_err_t ret;

  /* Print startup banner */
  print_banner();

  /* Initialize NVS */
  ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);
  ESP_LOGI(TAG, "[OK] NVS Flash initialized");

  /* Create sensor mutex */
  ret = bme680_app_create_mutex();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "[FAIL] Failed to create sensor mutex");
    return;
  }

  /* Initialize Buzzer */
  ret = buzzer_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "[FAIL] Failed to initialize buzzer");
    return;
  }

  /* Initialize I2C */
  ret = i2c_master_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "[FAIL] Failed to initialize I2C");
    return;
  }

  /* Initialize BME680 sensor */
  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "=== Initializing BME680 Sensor ===");
  ret = bme680_app_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "[FAIL] Failed to initialize BME680 sensor");
    ESP_LOGE(TAG, "Check wiring: SDA=GPIO%d, SCL=GPIO%d, Addr=0x%02X",
             I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO, bme680_app_get_address());
    return;
  }

  /* Start tasks */
  xTaskCreate(sensor_task, "sensor_task", 4096, NULL, 5, NULL);
  buzzer_start_task();

  /* Print system info */
  print_system_info();
  ESP_LOGI(TAG, "Starting sensor readings...");
  ESP_LOGI(TAG, "");
}
