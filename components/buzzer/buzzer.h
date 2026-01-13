/**
 * @file buzzer.h
 */

#ifndef BUZZER_H
#define BUZZER_H

#include "driver/gpio.h"
#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Buzzer Configuration */
#define BUZZER_GPIO GPIO_NUM_5  // GPIO5   cho Buzzer
#define BUZZER_ON_TIME_MS 3000  // Thời gian buzzer kêu (ms)
#define BUZZER_OFF_TIME_MS 2000 // Thời gian buzzer tắt (ms)

/**
 * @brief Initialize buzzer GPIO
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t buzzer_init(void);

/**
 * @brief Turn buzzer on
 */
void buzzer_on(void);

/**
 * @brief Turn buzzer off
 */
void buzzer_off(void);

/**
 * @brief Set buzzer alert state
 * @param active true to activate buzzer alert, false to deactivate
 */
void buzzer_set_active(bool active);

/**
 * @brief Get buzzer alert state
 * @return true if buzzer alert is active
 */
bool buzzer_is_active(void);

/**
 * @brief Start buzzer alert task
 * @note Creates a FreeRTOS task to handle buzzer beeping
 */
void buzzer_start_task(void);

/**
 * @brief Get buzzer GPIO number
 * @return GPIO number used for buzzer
 */
gpio_num_t buzzer_get_gpio(void);

#ifdef __cplusplus
}
#endif

#endif // BUZZER_H
