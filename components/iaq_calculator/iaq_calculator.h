/**
 * @file iaq_calculator.h
 * @brief Indoor Air Quality (IAQ) Calculator based on BME680 sensor data
 */

#ifndef IAQ_CALCULATOR_H
#define IAQ_CALCULATOR_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief IAQ Level enumeration for easy classification
 */
typedef enum {
  IAQ_LEVEL_EXCELLENT = 0,
  IAQ_LEVEL_GOOD = 1,
  IAQ_LEVEL_LIGHTLY_POLLUTED = 2,
  IAQ_LEVEL_MODERATELY_POLLUTED = 3,
  IAQ_LEVEL_HEAVILY_POLLUTED = 4,
  IAQ_LEVEL_SEVERELY_POLLUTED = 5,
  IAQ_LEVEL_UNKNOWN = 6
} iaq_level_t;

/**
 * @brief IAQ Accuracy status
 */
typedef enum {
  IAQ_ACCURACY_UNRELIABLE = 0,
  IAQ_ACCURACY_LOW = 1,
  IAQ_ACCURACY_MEDIUM = 2,
  IAQ_ACCURACY_HIGH = 3
} iaq_accuracy_t;

/**
 * @brief Raw sensor data input for IAQ calculation
 */
typedef struct {
  float temperature;
  float humidity;
  float pressure;
  float gas_resistance;
  bool gas_valid;
} iaq_raw_data_t;

/**
 * @brief Complete IAQ calculation result
 */
typedef struct {
  float iaq_score;
  iaq_level_t iaq_level;
  iaq_accuracy_t accuracy;
  float co2_equivalent;
  float voc_equivalent;
  float static_iaq;
  float comp_temperature;
  float comp_humidity;
  float gas_baseline;
  uint32_t samples_count;
  bool is_calibrated;
} iaq_result_t;

/**
 * @brief IAQ Algorithm configuration
 */
typedef struct {
  float temp_offset;
  float humidity_offset;
  uint32_t burn_in_samples;
  float gas_recalibration_rate;
} iaq_config_t;

/**
 * @brief Initialize IAQ calculator with default configuration
 * @return ESP_OK on success
 */
esp_err_t iaq_init(void);

/**
 * @brief Initialize IAQ calculator with custom configuration
 * @param config Pointer to configuration structure
 * @return ESP_OK on success
 */
esp_err_t iaq_init_with_config(const iaq_config_t *config);

/**
 * @brief Process raw sensor data and calculate IAQ
 * @param raw_data Pointer to raw sensor data
 * @param result Pointer to store calculation result
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG on invalid input
 */
esp_err_t iaq_calculate(const iaq_raw_data_t *raw_data, iaq_result_t *result);

/**
 * @brief Get the current IAQ result (thread-safe)
 * @param result Pointer to store latest IAQ result
 * @return ESP_OK on success
 */
esp_err_t iaq_get_result(iaq_result_t *result);

/**
 * @brief Reset IAQ algorithm state (restart calibration)
 */
void iaq_reset(void);

/**
 * @brief Get string description for IAQ level
 * @param level IAQ level enum value
 * @return Pointer to static string description
 */
const char *iaq_level_to_string(iaq_level_t level);

/**
 * @brief Get string description for IAQ accuracy
 * @param accuracy IAQ accuracy enum value
 * @return Pointer to static string description
 */
const char *iaq_accuracy_to_string(iaq_accuracy_t accuracy);

/**
 * @brief Get color code for IAQ level (for display purposes)
 * @param level IAQ level enum value
 * @return Hex color code (e.g., 0x00FF00 for green)
 */
uint32_t iaq_level_to_color(iaq_level_t level);

/**
 * @brief Check if IAQ algorithm is calibrated
 * @return true if calibrated, false otherwise
 */
bool iaq_is_calibrated(void);

/**
 * @brief Get calibration progress percentage
 * @return Progress percentage (0-100)
 */
uint8_t iaq_get_calibration_progress(void);

/**
 * @brief Save current calibration state to NVS
 * @return ESP_OK on success
 */
esp_err_t iaq_save_state(void);

/**
 * @brief Load calibration state from NVS
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if no saved state
 */
esp_err_t iaq_load_state(void);

#ifdef __cplusplus
}
#endif

#endif // IAQ_CALCULATOR_H
