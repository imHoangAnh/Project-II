/**
 * @file buzzer.c
 */

#include "buzzer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BUZZER";

static volatile bool g_buzzer_active = false;

esp_err_t buzzer_init(void) {
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

  gpio_set_level(BUZZER_GPIO, 0);

  ESP_LOGI(TAG, "Buzzer initialized on GPIO%d", BUZZER_GPIO);
  return ESP_OK;
}

void buzzer_on(void) { gpio_set_level(BUZZER_GPIO, 1); }

void buzzer_off(void) { gpio_set_level(BUZZER_GPIO, 0); }

void buzzer_set_active(bool active) { g_buzzer_active = active; }

bool buzzer_is_active(void) { return g_buzzer_active; }

gpio_num_t buzzer_get_gpio(void) { return BUZZER_GPIO; }

static void buzzer_task(void *pvParameters) {
  (void)pvParameters;
  ESP_LOGI(TAG, "Buzzer task started");

  while (1) {
    if (g_buzzer_active) {
      buzzer_on();
      ESP_LOGW(TAG, "[BUZZER] ON - Air Quality ALERT!");
      vTaskDelay(pdMS_TO_TICKS(BUZZER_ON_TIME_MS));

      buzzer_off();
      vTaskDelay(pdMS_TO_TICKS(BUZZER_OFF_TIME_MS));
    } else {
      buzzer_off();
      vTaskDelay(pdMS_TO_TICKS(500));
    }
  }
}

void buzzer_start_task(void) {
  xTaskCreate(buzzer_task, "buzzer_task", 2048, NULL, 4, NULL);
  ESP_LOGI(TAG, "Buzzer alert task created");
}
