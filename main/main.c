/**
 * @file main.c
 * @brief BME680 Environmental Sensor with IAQ - Terminal Logger
 *
 * Application that reads BME680 sensor data, calculates Indoor Air Quality,
 * and logs to terminal with temperature alerts.
 */

#include "bme680_app.h"
#include "buzzer.h"
#include "i2c_config.h"
#include "iaq_calculator.h"

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include <inttypes.h>

/* ========================== Configuration ================================ */

#define TAG "MAIN"
#define SENSOR_READ_INTERVAL_MS 10000 // Read sensor every 10 seconds
#define IAQ_SAVE_INTERVAL 20          // Save IAQ state every N readings

/* ========================== Sensor Task ================================== */
/**
 * @brief Sensor reading task with IAQ calculation
 */
static void sensor_task(void *pvParameters) {
  (void)pvParameters; // Unused parameter
  ESP_LOGI(TAG, "Sensor task started - Interval: %d ms",
           SENSOR_READ_INTERVAL_MS);

  uint32_t save_counter = 0;

  while (1) {
    struct bme68x_data raw_data;
    bme680_sensor_data_t sensor_data;

    if (bme680_app_read(&raw_data) == ESP_OK) {
      /* Update global data */
      bme680_app_update_data(&raw_data);
      bme680_app_get_data(&sensor_data);

      /* Calculate IAQ */
      iaq_raw_data_t iaq_input = {
          .temperature = raw_data.temperature,
          .humidity = raw_data.humidity,
          .pressure = raw_data.pressure,
          .gas_resistance = (float)raw_data.gas_resistance,
          .gas_valid =
              (raw_data.status & BME68X_GASM_VALID_MSK) ? true : false};

      iaq_result_t iaq_result;
      esp_err_t iaq_ret = iaq_calculate(&iaq_input, &iaq_result);

      /* Log data to terminal */
      ESP_LOGI(TAG, "----BME680 SENSOR DATA----");

      /* Basic sensor readings */
      ESP_LOGI(TAG, "Temperature : %8.2f °C ", raw_data.temperature);
      ESP_LOGI(TAG, "Humidity    : %8.2f %% ", raw_data.humidity);
      ESP_LOGI(TAG, "Pressure    : %8.2f hPa ", raw_data.pressure / 100.0f);

      if (raw_data.status & BME68X_GASM_VALID_MSK) {
        ESP_LOGI(TAG, "Gas Resist. : %8.0f Ohms ",
                 (float)raw_data.gas_resistance);
      } else {
        ESP_LOGW(TAG, "Gas Resist. :  Invalid");
      }

      /* IAQ Section */
      ESP_LOGI(TAG, "----INDOOR AIR QUALITY (IAQ)----");

      if (iaq_ret == ESP_OK) {
        /* IAQ Score with color indicator */
        const char *level_str = iaq_level_to_string(iaq_result.iaq_level);
        const char *acc_str = iaq_accuracy_to_string(iaq_result.accuracy);

        if (iaq_result.iaq_score <= 50) {
          ESP_LOGI(TAG, "IAQ Score   : %8.1f  [%s]", iaq_result.iaq_score,
                   level_str);
        } else if (iaq_result.iaq_score <= 150) {
          ESP_LOGW(TAG, "IAQ Score   : %8.1f  [%s]", iaq_result.iaq_score,
                   level_str);
        } else {
          ESP_LOGE(TAG, "IAQ Score   : %8.1f  [%s]", iaq_result.iaq_score,
                   level_str);
        }

        ESP_LOGI(TAG, "CO2 Equiv.  : %8.0f ppm", iaq_result.co2_equivalent);
        ESP_LOGI(TAG, "VOC Equiv.  : %8.2f ppm", iaq_result.voc_equivalent);
        ESP_LOGI(TAG, "Accuracy    : %s", acc_str);

        /* Calibration progress */
        if (!iaq_result.is_calibrated) {
          uint8_t progress = iaq_get_calibration_progress();
          ESP_LOGW(TAG, "Calibrating : %d%% complete", progress);
        }

        /* Periodically save IAQ state */
        save_counter++;
        if (save_counter >= IAQ_SAVE_INTERVAL && iaq_result.is_calibrated) {
          iaq_save_state();
          save_counter = 0;
        }
      } else {
        ESP_LOGW(TAG, "IAQ         : Waiting for valid gas data...");
      }

      /* Check IAQ level for buzzer alert */
      if (iaq_ret == ESP_OK && iaq_result.is_calibrated) {
        if (iaq_result.iaq_level >= IAQ_LEVEL_MODERATELY_POLLUTED) {
          /* Buzzer ON: Moderately, Heavily, or Severely Polluted (IAQ >= 151)
           */
          ESP_LOGE(TAG, "ALERT: %s! IAQ=%.0f - Buzzer ON",
                   iaq_level_to_string(iaq_result.iaq_level),
                   iaq_result.iaq_score);
          buzzer_set_active(true);
        } else if (iaq_result.iaq_level == IAQ_LEVEL_LIGHTLY_POLLUTED) {
          /* Warning: Lightly Polluted (IAQ 101-150) */
          ESP_LOGW(TAG, "WARNING: Lightly Polluted Air! IAQ=%.0f",
                   iaq_result.iaq_score);
          buzzer_set_active(false);
        } else {
          /* Normal: Excellent or Good (IAQ 0-100) */
          ESP_LOGI(TAG, "NORMAL: Air Quality Status: %s",
                   iaq_level_to_string(iaq_result.iaq_level));
          buzzer_set_active(false);
        }
      } else {
        ESP_LOGI(TAG, "Status: Calibrating IAQ sensor...");
        buzzer_set_active(false);
      }
      ESP_LOGI(TAG, "");

    } else {
      ESP_LOGE(TAG, "Failed to read sensor data!");
    }

    vTaskDelay(pdMS_TO_TICKS(SENSOR_READ_INTERVAL_MS));
  }
}

/* ========================== Main Application ============================= */

static void print_banner(void) {
  ESP_LOGI(TAG, "BME680 Environmental Sensor - Terminal Logger");
  ESP_LOGI(TAG, "ESP-IDF: %s", esp_get_idf_version());
  ESP_LOGI(TAG, "");
}

static void print_system_info(void) {
  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "System initialized successfully!");
  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "I2C: SDA=GPIO%d, SCL=GPIO%d, Freq=%dHz", I2C_MASTER_SDA_IO,
           I2C_MASTER_SCL_IO, I2C_MASTER_FREQ_HZ);
  ESP_LOGI(TAG, "BME680: Address=0x%02X", bme680_app_get_address());
  ESP_LOGI(TAG, "Buzzer: GPIO%d", buzzer_get_gpio());
  ESP_LOGI(TAG, "Temp Threshold: %.1f°C", bme680_app_get_threshold());
  ESP_LOGI(TAG, "Read Interval: %d ms", SENSOR_READ_INTERVAL_MS);
  ESP_LOGI(TAG, "IAQ Enabled: Yes (Software Algorithm)");
  ESP_LOGI(TAG, "");
}

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
  ESP_LOGI(TAG, "NVS Flash initialized");

  /* Create sensor mutex */
  ret = bme680_app_create_mutex();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create sensor mutex");
    return;
  }

  /* Initialize Buzzer */
  ret = buzzer_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize buzzer");
    return;
  }

  /* Initialize I2C */
  ret = i2c_master_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize I2C");
    return;
  }

  /* Initialize BME680 sensor */
  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "Initializing BME680 Sensor");
  ret = bme680_app_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize BME680 sensor");
    ESP_LOGE(TAG, "Check wiring: SDA=GPIO%d, SCL=GPIO%d, Addr=0x%02X",
             I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO, bme680_app_get_address());
    return;
  }

  /* Initialize IAQ Calculator */
  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "Initializing IAQ Calculator");
  ret = iaq_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize IAQ Calculator");
    return;
  }
  ESP_LOGI(TAG, "IAQ Calculator initialized");

  /* Start tasks */
  xTaskCreate(sensor_task, "sensor_task", 4096, NULL, 5, NULL);
  buzzer_start_task();

  /* Print system info */
  print_system_info();
  ESP_LOGI(TAG, "Starting sensor readings...");
  ESP_LOGI(TAG, "");
}
