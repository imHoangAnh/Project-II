/**
 * @file main.c
 * @brief BME680 Environmental Sensor with IAQ - Terminal Logger
 */

#include "bme680_app.h"
#include "buzzer.h"
#include "i2c_config.h"
#include "iaq_calculator.h"
#include "mqtt_client_app.h"

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include <inttypes.h>
#include <stdio.h>

#define TAG "MAIN"
#define SENSOR_READ_INTERVAL_MS 10000
#define IAQ_SAVE_INTERVAL 20
#define MQTT_ENABLED 1

/**
 * @brief Sensor reading task with IAQ calculation
 */
static void sensor_task(void *pvParameters)
{
  (void)pvParameters;
  ESP_LOGI(TAG, "Sensor task started - Interval: %d ms",
           SENSOR_READ_INTERVAL_MS);

  uint32_t save_counter = 0;

  while (1)
  {
    struct bme68x_data raw_data;
    bme680_sensor_data_t sensor_data;

    if (bme680_app_read(&raw_data) == ESP_OK)
    {
      bme680_app_update_data(&raw_data);
      bme680_app_get_data(&sensor_data);

      iaq_raw_data_t iaq_input = {
          .temperature = raw_data.temperature,
          .humidity = raw_data.humidity,
          .pressure = raw_data.pressure,
          .gas_resistance = (float)raw_data.gas_resistance,
          .gas_valid =
              (raw_data.status & BME68X_GASM_VALID_MSK) ? true : false};

      iaq_result_t iaq_result;
      esp_err_t iaq_ret = iaq_calculate(&iaq_input, &iaq_result);

      ESP_LOGI(TAG, "----BME680 SENSOR DATA----");
      ESP_LOGI(TAG, "Temperature : %8.2f °C ", raw_data.temperature);
      ESP_LOGI(TAG, "Humidity    : %8.2f %% ", raw_data.humidity);
      ESP_LOGI(TAG, "Pressure    : %8.2f hPa ", raw_data.pressure / 100.0f);

      if (raw_data.status & BME68X_GASM_VALID_MSK)
      {
        ESP_LOGI(TAG, "Gas Resist. : %8.0f Ohms ",
                 (float)raw_data.gas_resistance);
      }
      else
      {
        ESP_LOGW(TAG, "Gas Resist. :  Invalid");
      }

      ESP_LOGI(TAG, "----INDOOR AIR QUALITY (IAQ)----");

      if (iaq_ret == ESP_OK)
      {
        const char *level_str = iaq_level_to_string(iaq_result.iaq_level);
        const char *acc_str = iaq_accuracy_to_string(iaq_result.accuracy);

        if (iaq_result.iaq_score <= 50)
        {
          ESP_LOGI(TAG, "IAQ Score   : %8.1f  [%s]", iaq_result.iaq_score,
                   level_str);
        }
        else if (iaq_result.iaq_score <= 150)
        {
          ESP_LOGW(TAG, "IAQ Score   : %8.1f  [%s]", iaq_result.iaq_score,
                   level_str);
        }
        else
        {
          ESP_LOGE(TAG, "IAQ Score   : %8.1f  [%s]", iaq_result.iaq_score,
                   level_str);
        }

        ESP_LOGI(TAG, "CO2 Equiv.  : %8.0f ppm", iaq_result.co2_equivalent);
        ESP_LOGI(TAG, "VOC Equiv.  : %8.2f ppm", iaq_result.voc_equivalent);
        ESP_LOGI(TAG, "Accuracy    : %s", acc_str);

        if (!iaq_result.is_calibrated)
        {
          uint8_t progress = iaq_get_calibration_progress();
          ESP_LOGW(TAG, "Calibrating : %d%% complete", progress);
        }

        save_counter++;
        if (save_counter >= IAQ_SAVE_INTERVAL && iaq_result.is_calibrated)
        {
          iaq_save_state();
          save_counter = 0;
        }
      }
      else
      {
        ESP_LOGW(TAG, "IAQ         : Waiting for valid gas data...");
      }

      if (iaq_ret == ESP_OK && iaq_result.is_calibrated)
      {
        if (iaq_result.iaq_level >= IAQ_LEVEL_MODERATELY_POLLUTED)
        {
          ESP_LOGE(TAG, "ALERT: %s! IAQ=%.0f - Buzzer ON",
                   iaq_level_to_string(iaq_result.iaq_level),
                   iaq_result.iaq_score);
          buzzer_set_active(true);
        }
        else if (iaq_result.iaq_level == IAQ_LEVEL_LIGHTLY_POLLUTED)
        {
          ESP_LOGW(TAG, "WARNING: Lightly Polluted Air! IAQ=%.0f",
                   iaq_result.iaq_score);
          buzzer_set_active(false);
        }
        else
        {
          ESP_LOGI(TAG, "NORMAL: Air Quality Status: %s",
                   iaq_level_to_string(iaq_result.iaq_level));
          buzzer_set_active(false);
        }
      }
      else
      {
        ESP_LOGI(TAG, "Status: Calibrating IAQ sensor...");
        buzzer_set_active(false);
      }

#if MQTT_ENABLED
      /* Publish data to MQTT broker */
      if (mqtt_is_connected())
      {
#if MQTT_USE_THINGSBOARD
        mqtt_sensor_data_t mqtt_sensor = {
            .temperature = raw_data.temperature,
            .humidity = raw_data.humidity,
            .pressure = raw_data.pressure / 100.0f,
            .gas_resistance = (float)raw_data.gas_resistance,
            .gas_valid =
                (raw_data.status & BME68X_GASM_VALID_MSK) ? true : false};
        mqtt_iaq_data_t mqtt_iaq;
        mqtt_iaq_data_t *iaq_ptr = NULL;
        if (iaq_ret == ESP_OK)
        {
          mqtt_iaq = (mqtt_iaq_data_t){
              .iaq_score = iaq_result.iaq_score,
              .iaq_level = (int)iaq_result.iaq_level,
              .iaq_text = iaq_level_to_string(iaq_result.iaq_level),
              .accuracy = (int)iaq_result.accuracy,
              .co2_equivalent = iaq_result.co2_equivalent,
              .voc_equivalent = iaq_result.voc_equivalent,
              .is_calibrated = iaq_result.is_calibrated};
          iaq_ptr = &mqtt_iaq;
        }
        mqtt_publish_thingsboard_telemetry(&mqtt_sensor, iaq_ptr);
#else
        mqtt_sensor_data_t mqtt_sensor = {
            .temperature = raw_data.temperature,
            .humidity = raw_data.humidity,
            .pressure = raw_data.pressure / 100.0f,
            .gas_resistance = (float)raw_data.gas_resistance,
            .gas_valid =
                (raw_data.status & BME68X_GASM_VALID_MSK) ? true : false};
        mqtt_publish_sensor_data(&mqtt_sensor);

        if (iaq_ret == ESP_OK)
        {
          mqtt_iaq_data_t mqtt_iaq = {
              .iaq_score = iaq_result.iaq_score,
              .iaq_level = (int)iaq_result.iaq_level,
              .iaq_text = iaq_level_to_string(iaq_result.iaq_level),
              .accuracy = (int)iaq_result.accuracy,
              .co2_equivalent = iaq_result.co2_equivalent,
              .voc_equivalent = iaq_result.voc_equivalent,
              .is_calibrated = iaq_result.is_calibrated};
          mqtt_publish_iaq_data(&mqtt_iaq);

          if (iaq_result.is_calibrated &&
              iaq_result.iaq_level >= IAQ_LEVEL_MODERATELY_POLLUTED)
          {
            char alert_msg[128];
            snprintf(alert_msg, sizeof(alert_msg),
                     "Air quality is %s! IAQ Score: %.0f",
                     iaq_level_to_string(iaq_result.iaq_level),
                     iaq_result.iaq_score);
            mqtt_publish_alert("IAQ_ALERT", alert_msg);
          }
        }
#endif
        ESP_LOGI(TAG, "MQTT: Data published successfully");
      }
#endif
    }
    else
    {
      ESP_LOGE(TAG, "Failed to read sensor data!");
    }

    vTaskDelay(pdMS_TO_TICKS(SENSOR_READ_INTERVAL_MS));
  }
}

static void print_banner(void)
{
  ESP_LOGI(TAG, "BME680 Environmental Sensor - Terminal Logger");
  ESP_LOGI(TAG, "ESP-IDF: %s", esp_get_idf_version());
  ESP_LOGI(TAG, "");
}

static void print_system_info(void)
{
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
#if MQTT_ENABLED
  ESP_LOGI(TAG, "MQTT: %s", mqtt_is_connected() ? "Connected" : "Disconnected");
#if MQTT_USE_THINGSBOARD
  ESP_LOGI(TAG, "MQTT backend: ThingsBoard (v1/devices/me/telemetry)");
#endif
#endif
  ESP_LOGI(TAG, "");
}

void app_main(void)
{
  esp_err_t ret;

  print_banner();

  ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
  {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);
  ESP_LOGI(TAG, "NVS Flash initialized");

  ret = bme680_app_create_mutex();
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to create sensor mutex");
    return;
  }

  ret = buzzer_init();
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to initialize buzzer");
    return;
  }

  ret = i2c_master_init();
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to initialize I2C");
    return;
  }

  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "Initializing BME680 Sensor");
  ret = bme680_app_init();
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to initialize BME680 sensor");
    ESP_LOGE(TAG, "Check wiring: SDA=GPIO%d, SCL=GPIO%d, Addr=0x%02X",
             I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO, bme680_app_get_address());
    return;
  }

  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "Initializing IAQ Calculator");
  ret = iaq_init();
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to initialize IAQ Calculator");
    return;
  }
  ESP_LOGI(TAG, "IAQ Calculator initialized");

#if MQTT_ENABLED
  /* Initialize WiFi */
  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "Initializing WiFi...");
  ret = wifi_init_sta();
  if (ret != ESP_OK)
  {
    ESP_LOGW(TAG, "WiFi connection failed - MQTT disabled");
  }
  else
  {
    ESP_LOGI(TAG, "Initializing MQTT client...");
    ret = mqtt_app_init();
    if (ret == ESP_OK)
    {
      ret = mqtt_app_start();
      if (ret != ESP_OK)
      {
        ESP_LOGW(TAG, "Failed to start MQTT client");
      }
    }
    else
    {
      ESP_LOGW(TAG, "Failed to initialize MQTT client");
    }
  }
#endif

  xTaskCreate(sensor_task, "sensor_task", 8192, NULL, 5, NULL);
  buzzer_start_task();

  print_system_info();
  ESP_LOGI(TAG, "Starting sensor readings...");
  ESP_LOGI(TAG, "");
}
