/**
 * @file bme680_app.h
 */

#ifndef BME680_APP_H
#define BME680_APP_H

#include "bme68x.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* BME680 Configuration */
#define BME680_I2C_ADDR BME68X_I2C_ADDR_HIGH // 0x77
#define TEMP_THRESHOLD 100.0f                // Temperature alert threshold (°C)

/**
 * @brief Sensor data structure
 */
typedef struct {
  float temperature;    // Temperature in °C
  float humidity;       // Humidity in %
  float pressure;       // Pressure in Pa
  float gas_resistance; // Gas resistance in Ohms
  bool gas_valid;       // Gas reading validity
  bool data_valid;      // Overall data validity
  uint32_t read_count;  // Number of readings
} bme680_sensor_data_t;

/**
 * @brief Initialize BME680 sensor
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t bme680_app_init(void);

/**
 * @brief Read data from BME680 sensor
 * @param data Pointer to bme68x_data structure to store raw data
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t bme680_app_read(struct bme68x_data *data);

/**
 * @brief Get last sensor reading (thread-safe)
 * @param data Pointer to bme680_sensor_data_t to store data
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t bme680_app_get_data(bme680_sensor_data_t *data);

/**
 * @brief Update global sensor data (thread-safe)
 * @param raw_data Pointer to raw BME68x data
 */
void bme680_app_update_data(const struct bme68x_data *raw_data);

/**
 * @brief Get temperature threshold for alerts
 * @return Temperature threshold in °C
 */
float bme680_app_get_threshold(void);

/**
 * @brief Get BME680 I2C address
 * @return I2C address
 */
uint8_t bme680_app_get_address(void);

/**
 * @brief Create sensor data mutex
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t bme680_app_create_mutex(void);

#ifdef __cplusplus
}
#endif

#endif // BME680_APP_H
        