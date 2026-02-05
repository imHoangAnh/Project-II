/**
 * @file iaq_calculator.c
 * @brief Indoor Air Quality (IAQ) Calculator implementation
 */

#include "iaq_calculator.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <inttypes.h>
#include <math.h>
#include <string.h>

static const char *TAG = "IAQ_CALC";

#define GAS_BASELINE_DEFAULT 250000.0f
#define GAS_EXCELLENT_THRESHOLD 500000.0f
#define GAS_GOOD_THRESHOLD 200000.0f
#define GAS_MODERATE_THRESHOLD 100000.0f
#define GAS_POOR_THRESHOLD 50000.0f
#define GAS_VERY_POOR_THRESHOLD 20000.0f
#define TEMP_COMP_COEFF 0.003f
#define HUM_COMP_COEFF 0.015f
#define CO2_BASE 400.0f
#define CO2_MAX 2000.0f
#define CO2_SLOPE 5.0f
#define VOC_BASE 0.0f
#define VOC_MAX 10.0f
#define VOC_SLOPE 0.015f
#define DEFAULT_BURN_IN_SAMPLES 50
#define CALIBRATION_RATE_DEFAULT 0.001f
#define GAS_HISTORY_SIZE 10
#define NVS_NAMESPACE "iaq_state"
#define NVS_KEY_BASELINE "gas_base"
#define NVS_KEY_SAMPLES "samples"

static struct {
  iaq_config_t config;
  iaq_result_t last_result;
  SemaphoreHandle_t mutex;
  float gas_baseline;
  float gas_history[GAS_HISTORY_SIZE];
  uint8_t gas_history_idx;
  uint32_t samples_count;
  bool initialized;
  float gas_sum;
  float gas_min;
  float gas_max;
} g_iaq_state = {0};

/**
 * @brief Apply temperature and humidity compensation to gas resistance
 */
static float compensate_gas_resistance(float gas_resistance, float temperature,
                                       float humidity) {
  float temp_factor = 1.0f + TEMP_COMP_COEFF * (temperature - 25.0f);
  float hum_factor = 1.0f + HUM_COMP_COEFF * (humidity - 40.0f);
  float comp_resistance = gas_resistance * temp_factor / hum_factor;
  return comp_resistance;
}

/**
 * @brief Update gas baseline using exponential moving average
 */
static void update_gas_baseline(float gas_resistance) {
  g_iaq_state.gas_history[g_iaq_state.gas_history_idx] = gas_resistance;
  g_iaq_state.gas_history_idx =
      (g_iaq_state.gas_history_idx + 1) % GAS_HISTORY_SIZE;
  g_iaq_state.gas_sum += gas_resistance;
  if (gas_resistance > g_iaq_state.gas_max) {
    g_iaq_state.gas_max = gas_resistance;
  }
  if (gas_resistance < g_iaq_state.gas_min || g_iaq_state.gas_min == 0) {
    g_iaq_state.gas_min = gas_resistance;
  }
  g_iaq_state.samples_count++;

  if (g_iaq_state.samples_count <= DEFAULT_BURN_IN_SAMPLES) {
    g_iaq_state.gas_baseline = g_iaq_state.gas_sum / g_iaq_state.samples_count;
  } else {
    if (gas_resistance > g_iaq_state.gas_baseline) {
      float rate = g_iaq_state.config.gas_recalibration_rate;
      g_iaq_state.gas_baseline =
          g_iaq_state.gas_baseline * (1.0f - rate) + gas_resistance * rate;
    }
  }
}

/**
 * @brief Calculate IAQ score from compensated gas resistance
 */
static float calculate_iaq_from_gas(float comp_gas_resistance) {
  float baseline = g_iaq_state.gas_baseline;

  if (baseline <= 0) {
    baseline = GAS_BASELINE_DEFAULT;
  }

  float gas_ratio = comp_gas_resistance / baseline;
  float iaq;

  if (gas_ratio >= 1.0f) {
    iaq = 50.0f * (2.0f - fminf(gas_ratio, 2.0f));
  } else if (gas_ratio >= 0.5f) {
    iaq = 50.0f + 100.0f * (1.0f - gas_ratio) * 2.0f;
  } else if (gas_ratio >= 0.2f) {
    iaq = 150.0f + 100.0f * ((0.5f - gas_ratio) / 0.3f);
  } else if (gas_ratio >= 0.1f) {
    iaq = 250.0f + 100.0f * ((0.2f - gas_ratio) / 0.1f);
  } else {
    iaq = 350.0f + 150.0f * fminf((0.1f - gas_ratio) / 0.1f, 1.0f);
  }

  if (iaq < 0)
    iaq = 0;
  if (iaq > 500)
    iaq = 500;

  return iaq;
}

/**
 * @brief Estimate CO2 equivalent from IAQ score
 */
static float estimate_co2(float iaq_score) {
  float co2 = CO2_BASE + (iaq_score * CO2_SLOPE);

  if (co2 < CO2_BASE)
    co2 = CO2_BASE;
  if (co2 > CO2_MAX)
    co2 = CO2_MAX;

  return co2;
}

/**
 * @brief Estimate VOC equivalent from gas resistance
 */
static float estimate_voc(float gas_resistance, float baseline) {
  if (gas_resistance <= 0 || baseline <= 0) {
    return 0;
  }

  float ratio = baseline / gas_resistance;
  float voc = VOC_BASE + (ratio - 1.0f) * VOC_SLOPE * 100.0f;

  if (voc < VOC_BASE)
    voc = VOC_BASE;
  if (voc > VOC_MAX)
    voc = VOC_MAX;

  return voc;
}

/**
 * @brief Classify IAQ score into level
 */
static iaq_level_t classify_iaq(float iaq_score) {
  if (iaq_score <= 50)
    return IAQ_LEVEL_EXCELLENT;
  if (iaq_score <= 100)
    return IAQ_LEVEL_GOOD;
  if (iaq_score <= 150)
    return IAQ_LEVEL_LIGHTLY_POLLUTED;
  if (iaq_score <= 200)
    return IAQ_LEVEL_MODERATELY_POLLUTED;
  if (iaq_score <= 300)
    return IAQ_LEVEL_HEAVILY_POLLUTED;
  return IAQ_LEVEL_SEVERELY_POLLUTED;
}

/**
 * @brief Determine accuracy based on calibration progress
 */
static iaq_accuracy_t determine_accuracy(void) {
  uint32_t samples = g_iaq_state.samples_count;
  uint32_t burn_in = g_iaq_state.config.burn_in_samples;

  if (samples < burn_in / 4) {
    return IAQ_ACCURACY_UNRELIABLE;
  } else if (samples < burn_in / 2) {
    return IAQ_ACCURACY_LOW;
  } else if (samples < burn_in) {
    return IAQ_ACCURACY_MEDIUM;
  }
  return IAQ_ACCURACY_HIGH;
}

esp_err_t iaq_init(void) {
  iaq_config_t default_config = {.temp_offset = 0.0f,
                                 .humidity_offset = 0.0f,
                                 .burn_in_samples = DEFAULT_BURN_IN_SAMPLES,
                                 .gas_recalibration_rate =
                                     CALIBRATION_RATE_DEFAULT};
  return iaq_init_with_config(&default_config);
}

esp_err_t iaq_init_with_config(const iaq_config_t *config) {
  if (config == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  // Create mutex
  g_iaq_state.mutex = xSemaphoreCreateMutex();
  if (g_iaq_state.mutex == NULL) {
    ESP_LOGE(TAG, "Failed to create IAQ mutex");
    return ESP_FAIL;
  }

  // Copy configuration
  memcpy(&g_iaq_state.config, config, sizeof(iaq_config_t));

  // Initialize state
  g_iaq_state.gas_baseline = GAS_BASELINE_DEFAULT;
  g_iaq_state.gas_history_idx = 0;
  g_iaq_state.samples_count = 0;
  g_iaq_state.gas_sum = 0;
  g_iaq_state.gas_min = 0;
  g_iaq_state.gas_max = 0;
  g_iaq_state.initialized = true;

  memset(g_iaq_state.gas_history, 0, sizeof(g_iaq_state.gas_history));
  memset(&g_iaq_state.last_result, 0, sizeof(iaq_result_t));

  if (iaq_load_state() == ESP_OK) {
    ESP_LOGI(TAG, "Loaded previous calibration state");
    ESP_LOGI(TAG, "  - Gas Baseline: %.0f Ohms", g_iaq_state.gas_baseline);
    ESP_LOGI(TAG, "  - Samples: %" PRIu32, g_iaq_state.samples_count);
  } else {
    ESP_LOGI(TAG, "Starting fresh calibration");
  }

  ESP_LOGI(TAG, "IAQ Calculator initialized");
  ESP_LOGI(TAG, "  - Burn-in samples: %" PRIu32, config->burn_in_samples);
  ESP_LOGI(TAG, "  - Recalibration rate: %.4f", config->gas_recalibration_rate);

  return ESP_OK;
}

esp_err_t iaq_calculate(const iaq_raw_data_t *raw_data, iaq_result_t *result) {
  if (!g_iaq_state.initialized) {
    ESP_LOGE(TAG, "IAQ not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  if (raw_data == NULL || result == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  if (!raw_data->gas_valid || raw_data->gas_resistance <= 0) {
    ESP_LOGW(TAG, "Invalid gas reading, skipping calculation");
    result->iaq_score = 0;
    result->iaq_level = IAQ_LEVEL_UNKNOWN;
    result->accuracy = IAQ_ACCURACY_UNRELIABLE;
    return ESP_ERR_INVALID_ARG;
  }

  if (xSemaphoreTake(g_iaq_state.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  float comp_gas = compensate_gas_resistance(
      raw_data->gas_resistance, raw_data->temperature, raw_data->humidity);
  update_gas_baseline(comp_gas);
  float iaq_score = calculate_iaq_from_gas(comp_gas);

  result->iaq_score = iaq_score;
  result->iaq_level = classify_iaq(iaq_score);
  result->accuracy = determine_accuracy();
  result->co2_equivalent = estimate_co2(iaq_score);
  result->voc_equivalent = estimate_voc(comp_gas, g_iaq_state.gas_baseline);
  result->static_iaq = iaq_score;
  result->comp_temperature =
      raw_data->temperature + g_iaq_state.config.temp_offset;
  result->comp_humidity =
      raw_data->humidity + g_iaq_state.config.humidity_offset;
  result->gas_baseline = g_iaq_state.gas_baseline;
  result->samples_count = g_iaq_state.samples_count;
  result->is_calibrated =
      (g_iaq_state.samples_count >= g_iaq_state.config.burn_in_samples);
  memcpy(&g_iaq_state.last_result, result, sizeof(iaq_result_t));

  xSemaphoreGive(g_iaq_state.mutex);

  return ESP_OK;
}

esp_err_t iaq_get_result(iaq_result_t *result) {
  if (result == NULL || !g_iaq_state.initialized) {
    return ESP_ERR_INVALID_ARG;
  }

  if (xSemaphoreTake(g_iaq_state.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    memcpy(result, &g_iaq_state.last_result, sizeof(iaq_result_t));
    xSemaphoreGive(g_iaq_state.mutex);
    return ESP_OK;
  }
  return ESP_ERR_TIMEOUT;
}

void iaq_reset(void) {
  if (!g_iaq_state.initialized)
    return;

  if (xSemaphoreTake(g_iaq_state.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    g_iaq_state.gas_baseline = GAS_BASELINE_DEFAULT;
    g_iaq_state.gas_history_idx = 0;
    g_iaq_state.samples_count = 0;
    g_iaq_state.gas_sum = 0;
    g_iaq_state.gas_min = 0;
    g_iaq_state.gas_max = 0;
    memset(g_iaq_state.gas_history, 0, sizeof(g_iaq_state.gas_history));
    xSemaphoreGive(g_iaq_state.mutex);
    ESP_LOGI(TAG, "IAQ algorithm reset");
  }
}

const char *iaq_level_to_string(iaq_level_t level) {
  switch (level) {
  case IAQ_LEVEL_EXCELLENT:
    return "Excellent";
  case IAQ_LEVEL_GOOD:
    return "Good";
  case IAQ_LEVEL_LIGHTLY_POLLUTED:
    return "Lightly Polluted";
  case IAQ_LEVEL_MODERATELY_POLLUTED:
    return "Moderately Polluted";
  case IAQ_LEVEL_HEAVILY_POLLUTED:
    return "Heavily Polluted";
  case IAQ_LEVEL_SEVERELY_POLLUTED:
    return "Severely Polluted";
  default:
    return "Unknown";
  }
}

const char *iaq_accuracy_to_string(iaq_accuracy_t accuracy) {
  switch (accuracy) {
  case IAQ_ACCURACY_UNRELIABLE:
    return "Unreliable (Stabilizing)";
  case IAQ_ACCURACY_LOW:
    return "Low (Calibrating)";
  case IAQ_ACCURACY_MEDIUM:
    return "Medium";
  case IAQ_ACCURACY_HIGH:
    return "High (Calibrated)";
  default:
    return "Unknown";
  }
}

uint32_t iaq_level_to_color(iaq_level_t level) {
  switch (level) {
  case IAQ_LEVEL_EXCELLENT:
    return 0x00E400; // Green
  case IAQ_LEVEL_GOOD:
    return 0x92D050; // Light Green
  case IAQ_LEVEL_LIGHTLY_POLLUTED:
    return 0xFFFF00; // Yellow
  case IAQ_LEVEL_MODERATELY_POLLUTED:
    return 0xFF8000; // Orange
  case IAQ_LEVEL_HEAVILY_POLLUTED:
    return 0xFF0000; // Red
  case IAQ_LEVEL_SEVERELY_POLLUTED:
    return 0x800080; // Purple
  default:
    return 0x808080; // Gray
  }
}

bool iaq_is_calibrated(void) {
  return g_iaq_state.samples_count >= g_iaq_state.config.burn_in_samples;
}

uint8_t iaq_get_calibration_progress(void) {
  if (g_iaq_state.config.burn_in_samples == 0)
    return 100;

  uint32_t progress =
      (g_iaq_state.samples_count * 100) / g_iaq_state.config.burn_in_samples;
  if (progress > 100)
    progress = 100;
  return (uint8_t)progress;
}

esp_err_t iaq_save_state(void) {
  nvs_handle_t nvs_handle;
  esp_err_t err;

  err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
    return err;
  }

  uint32_t baseline_int = (uint32_t)(g_iaq_state.gas_baseline);
  err = nvs_set_u32(nvs_handle, NVS_KEY_BASELINE, baseline_int);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to save baseline: %s", esp_err_to_name(err));
    nvs_close(nvs_handle);
    return err;
  }

  err = nvs_set_u32(nvs_handle, NVS_KEY_SAMPLES, g_iaq_state.samples_count);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to save samples count: %s", esp_err_to_name(err));
    nvs_close(nvs_handle);
    return err;
  }

  err = nvs_commit(nvs_handle);
  nvs_close(nvs_handle);

  if (err == ESP_OK) {
    ESP_LOGI(TAG, "IAQ state saved (baseline: %.0f, samples: %" PRIu32 ")",
             g_iaq_state.gas_baseline, g_iaq_state.samples_count);
  }

  return err;
}

esp_err_t iaq_load_state(void) {
  nvs_handle_t nvs_handle;
  esp_err_t err;

  err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
  if (err != ESP_OK) {
    return err;
  }

  uint32_t baseline_int = 0;
  err = nvs_get_u32(nvs_handle, NVS_KEY_BASELINE, &baseline_int);
  if (err == ESP_OK) {
    g_iaq_state.gas_baseline = (float)baseline_int;
  }

  err = nvs_get_u32(nvs_handle, NVS_KEY_SAMPLES, &g_iaq_state.samples_count);

  nvs_close(nvs_handle);

  return err;
}
