/**
 * @file mqtt_client_app.h
 * @brief MQTT Client Application for ESP32
 *
 * This component provides MQTT connectivity to publish
 * BME680 sensor data and IAQ results to a broker.
 */

#ifndef MQTT_CLIENT_APP_H
#define MQTT_CLIENT_APP_H

#include "esp_err.h"
#include "sdkconfig.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================== Configuration ================================ */

/**
 * @brief WiFi Configuration (from Kconfig or defaults)
 */
#ifdef CONFIG_WIFI_SSID
#define WIFI_SSID CONFIG_WIFI_SSID
#else
#define WIFI_SSID "Hoanganhh"
#endif

#ifdef CONFIG_WIFI_PASSWORD
#define WIFI_PASSWORD CONFIG_WIFI_PASSWORD
#else
#define WIFI_PASSWORD "250303hanh"
#endif

#ifdef CONFIG_WIFI_MAXIMUM_RETRY
#define WIFI_MAXIMUM_RETRY CONFIG_WIFI_MAXIMUM_RETRY
#else
#define WIFI_MAXIMUM_RETRY 5
#endif

/**
 * @brief MQTT Broker Configuration (from Kconfig or defaults)
 */
#ifdef CONFIG_MQTT_BROKER_URI
#define MQTT_BROKER_URI CONFIG_MQTT_BROKER_URI
#else
#define MQTT_BROKER_URI "mqtt://10.143.203.27:1883"
#endif

#ifdef CONFIG_MQTT_CLIENT_ID
#define MQTT_CLIENT_ID CONFIG_MQTT_CLIENT_ID
#else
#define MQTT_CLIENT_ID "esp32_bme680_sensor"
#endif
/**
 * @brief ThingsBoard Configuration
 */
// #ifdef CONFIG_MQTT_USE_THINGSBOARD
// #define MQTT_USE_THINGSBOARD 1
// #define MQTT_TOPIC_TELEMETRY "v1/devices/me/telemetry"
// #ifdef CONFIG_MQTT_THINGSBOARD_ACCESS_TOKEN
// #define MQTT_ACCESS_TOKEN CONFIG_MQTT_THINGSBOARD_ACCESS_TOKEN
// #else
// #define MQTT_ACCESS_TOKEN "3x50jua1ah34f5r3kfrx"
// #endif
/* ThingsBoard Configuration - Always enabled */
#define MQTT_USE_THINGSBOARD 1
#define MQTT_TOPIC_TELEMETRY "v1/devices/me/telemetry"
#define MQTT_ACCESS_TOKEN "3x50jua1ah34f5r3kfrx"  // ← Access Token của bạn

/**
 * @brief MQTT Topics
 */
#define MQTT_TOPIC_SENSOR "sensor/bme680/data"
#define MQTT_TOPIC_IAQ "sensor/bme680/iaq"
#define MQTT_TOPIC_STATUS "sensor/bme680/status"
#define MQTT_TOPIC_ALERT "sensor/bme680/alert"

/* ========================== Data Structures ============================== */

/**
 * @brief Sensor data structure for MQTT publishing
 */
typedef struct {
  float temperature;    // Temperature in °C
  float humidity;       // Humidity in %
  float pressure;       // Pressure in hPa
  float gas_resistance; // Gas resistance in Ohms
  bool gas_valid;       // Gas reading validity
} mqtt_sensor_data_t;

/**
 * @brief IAQ data structure for MQTT publishing
 */
typedef struct {
  float iaq_score;      // IAQ index (0-500)
  int iaq_level;        // IAQ level enum
  const char *iaq_text; // IAQ level text description
  int accuracy;         // Accuracy status
  float co2_equivalent; // Estimated CO2 in ppm
  float voc_equivalent; // Estimated VOC in ppm
  bool is_calibrated;   // Calibration status
} mqtt_iaq_data_t;

/**
 * @brief MQTT Connection status
 */
typedef enum {
  MQTT_STATUS_DISCONNECTED = 0,
  MQTT_STATUS_CONNECTING,
  MQTT_STATUS_CONNECTED,
  MQTT_STATUS_ERROR
} mqtt_status_t;

/* ========================== Public Functions ============================= */

/**
 * @brief Initialize WiFi in station mode
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t wifi_init_sta(void);

/**
 * @brief Initialize MQTT client
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mqtt_app_init(void);

/**
 * @brief Start MQTT client connection
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mqtt_app_start(void);

/**
 * @brief Stop MQTT client connection
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mqtt_app_stop(void);

/**
 * @brief Publish sensor data to MQTT broker
 * @param data Pointer to sensor data structure
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mqtt_publish_sensor_data(const mqtt_sensor_data_t *data);

/**
 * @brief Publish IAQ data to MQTT broker
 * @param data Pointer to IAQ data structure
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mqtt_publish_iaq_data(const mqtt_iaq_data_t *data);

/**
 * @brief Publish status message to MQTT broker
 * @param status Status string to publish
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mqtt_publish_status(const char *status);

/**
 * @brief Publish alert message to MQTT broker
 * @param alert_type Type of alert (e.g., "IAQ_HIGH", "TEMP_HIGH")
 * @param message Alert message
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mqtt_publish_alert(const char *alert_type, const char *message);

#if MQTT_USE_THINGSBOARD
/**
 * @brief Publish combined sensor + IAQ telemetry to ThingsBoard
 * (v1/devices/me/telemetry)
 * @param sensor Pointer to sensor data (required)
 * @param iaq Pointer to IAQ data (optional; pass NULL if not available)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mqtt_publish_thingsboard_telemetry(const mqtt_sensor_data_t *sensor,
                                             const mqtt_iaq_data_t *iaq);
#endif

/**
 * @brief Get current MQTT connection status
 * @return Current MQTT status
 */
mqtt_status_t mqtt_get_status(void);

/**
 * @brief Check if MQTT client is connected
 * @return true if connected, false otherwise
 */
bool mqtt_is_connected(void);

/**
 * @brief Get WiFi connection status
 * @return true if WiFi is connected, false otherwise
 */
bool wifi_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif // MQTT_CLIENT_APP_H
