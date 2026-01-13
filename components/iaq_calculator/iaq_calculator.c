/**
 * @file iaq_calculator.c
 * @brief Indoor Air Quality (IAQ) Calculator implementation
 *
 * This implementation uses a software-based algorithm to calculate
 * IAQ score and CO2/VOC equivalents based on BME680 sensor data.
 *
 * The algorithm is based on research papers and empirical data,
 * providing similar functionality to Bosch BSEC library.
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

/* ========================== Algorithm Constants =========================== */

// Gas resistance baseline for clean air (calibrated at 25°C, 40% humidity)
#define GAS_BASELINE_DEFAULT 250000.0f // Ohms

// Reference values for gas resistance
#define GAS_EXCELLENT_THRESHOLD 500000.0f // Very clean air
#define GAS_GOOD_THRESHOLD 200000.0f      // Good air
#define GAS_MODERATE_THRESHOLD 100000.0f  // Moderate
#define GAS_POOR_THRESHOLD 50000.0f       // Poor
#define GAS_VERY_POOR_THRESHOLD 20000.0f  // Very poor

// Temperature compensation coefficients
#define TEMP_COMP_COEFF 0.003f // 0.3% per °C deviation from 25°C

// Humidity compensation coefficients
#define HUM_COMP_COEFF 0.015f // 1.5% per %RH deviation from 40%

// CO2 estimation parameters (based on empirical research)
#define CO2_BASE 400.0f // Baseline CO2 in ppm (outdoor)
#define CO2_MAX 2000.0f // Maximum estimated CO2
#define CO2_SLOPE 5.0f  // Scaling factor

// VOC estimation parameters
#define VOC_BASE 0.0f    // Baseline VOC in ppm
#define VOC_MAX 10.0f    // Maximum estimated VOC
#define VOC_SLOPE 0.015f // Scaling factor

// Burn-in and calibration
#define DEFAULT_BURN_IN_SAMPLES 50
#define CALIBRATION_RATE_DEFAULT 0.001f // Slow adaptation rate
#define GAS_HISTORY_SIZE 10             // Moving average window

// NVS keys
#define NVS_NAMESPACE "iaq_state"
#define NVS_KEY_BASELINE "gas_base"
#define NVS_KEY_SAMPLES "samples"

/* ========================== Internal State ================================ */

static struct {
  iaq_config_t config;
  iaq_result_t last_result;
  SemaphoreHandle_t mutex;

  // Calibration state
  float gas_baseline;
  float gas_history[GAS_HISTORY_SIZE];
  uint8_t gas_history_idx;
  uint32_t samples_count;
  bool initialized;

  // Running statistics
  float gas_sum;
  float gas_min;
  float gas_max;
} g_iaq_state = {0};

/* ========================== Internal Functions ============================ */

/**
 * @brief Apply temperature and humidity compensation to gas resistance
 */
static float compensate_gas_resistance(float gas_resistance, float temperature,
                                       float humidity) {
  // Compensation for temperature deviation from reference (25°C)
  float temp_factor = 1.0f + TEMP_COMP_COEFF * (temperature - 25.0f);

  // Compensation for humidity deviation from reference (40% RH)
  float hum_factor = 1.0f + HUM_COMP_COEFF * (humidity - 40.0f);

  // Apply compensation (higher humidity = lower apparent resistance)
  float comp_resistance = gas_resistance * temp_factor / hum_factor;

  return comp_resistance;
}

/**
 * @brief Update gas baseline using exponential moving average
 */
static void update_gas_baseline(float gas_resistance) {
  // Add to history buffer
  g_iaq_state.gas_history[g_iaq_state.gas_history_idx] = gas_resistance;
  g_iaq_state.gas_history_idx =
      (g_iaq_state.gas_history_idx + 1) % GAS_HISTORY_SIZE;

  // Update running statistics
  g_iaq_state.gas_sum += gas_resistance;
  if (gas_resistance > g_iaq_state.gas_max) {
    g_iaq_state.gas_max = gas_resistance;
  }
  if (gas_resistance < g_iaq_state.gas_min || g_iaq_state.gas_min == 0) {
    g_iaq_state.gas_min = gas_resistance;
  }

  g_iaq_state.samples_count++;

  // Update baseline with slow adaptation
  if (g_iaq_state.samples_count <= DEFAULT_BURN_IN_SAMPLES) {
    // During burn-in, use simple average
    g_iaq_state.gas_baseline = g_iaq_state.gas_sum / g_iaq_state.samples_count;
  } else {
    // After burn-in, use exponential moving average with slow adaptation
    // Only update if current reading suggests cleaner air
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

  // Calculate gas ratio (higher is better)
  float gas_ratio = comp_gas_resistance / baseline;

  // Convert to IAQ score (0-500 scale, lower is better)
  float iaq;

  if (gas_ratio >= 1.0f) {
    // Clean air (ratio >= 1.0)
    // IAQ = 0-50 for very clean, 50-100 for clean
    iaq = 50.0f * (2.0f - fminf(gas_ratio, 2.0f));
  } else if (gas_ratio >= 0.5f) {
    // Slightly polluted (0.5 <= ratio < 1.0)
    // IAQ = 50-150
    iaq = 50.0f + 100.0f * (1.0f - gas_ratio) * 2.0f;
  } else if (gas_ratio >= 0.2f) {
    // Moderately polluted (0.2 <= ratio < 0.5)
    // IAQ = 150-250
    iaq = 150.0f + 100.0f * ((0.5f - gas_ratio) / 0.3f);
  } else if (gas_ratio >= 0.1f) {
    // Heavily polluted (0.1 <= ratio < 0.2)
    // IAQ = 250-350
    iaq = 250.0f + 100.0f * ((0.2f - gas_ratio) / 0.1f);
  } else {
    // Severely polluted (ratio < 0.1)
    // IAQ = 350-500
    iaq = 350.0f + 150.0f * fminf((0.1f - gas_ratio) / 0.1f, 1.0f);
  }

  // Clamp to valid range
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
  // CO2 estimation based on IAQ score
  // IAQ 0 -> ~400 ppm (outdoor baseline)
  // IAQ 500 -> ~2000+ ppm (very poor indoor)

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
  // VOC estimation - inversely proportional to gas resistance
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

/* ========================== Public Functions ============================== */

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

  // Try to load previous calibration state
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

  // Apply temperature/humidity compensation
  float comp_gas = compensate_gas_resistance(
      raw_data->gas_resistance, raw_data->temperature, raw_data->humidity);

  // Update baseline calibration
  update_gas_baseline(comp_gas);

  // Calculate IAQ score
  float iaq_score = calculate_iaq_from_gas(comp_gas);

  // Fill result structure
  result->iaq_score = iaq_score;
  result->iaq_level = classify_iaq(iaq_score);
  result->accuracy = determine_accuracy();

  // Calculate secondary outputs
  result->co2_equivalent = estimate_co2(iaq_score);
  result->voc_equivalent = estimate_voc(comp_gas, g_iaq_state.gas_baseline);
  result->static_iaq = iaq_score; // For this implementation, static = dynamic

  // Compensated values
  result->comp_temperature =
      raw_data->temperature + g_iaq_state.config.temp_offset;
  result->comp_humidity =
      raw_data->humidity + g_iaq_state.config.humidity_offset;

  // Algorithm state
  result->gas_baseline = g_iaq_state.gas_baseline;
  result->samples_count = g_iaq_state.samples_count;
  result->is_calibrated =
      (g_iaq_state.samples_count >= g_iaq_state.config.burn_in_samples);

  // Store last result
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

  // Save gas baseline as integer (x100 for precision)
  uint32_t baseline_int = (uint32_t)(g_iaq_state.gas_baseline);
  err = nvs_set_u32(nvs_handle, NVS_KEY_BASELINE, baseline_int);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to save baseline: %s", esp_err_to_name(err));
    nvs_close(nvs_handle);
    return err;
  }

  // Save samples count
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
