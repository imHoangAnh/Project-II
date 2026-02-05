/**
 * @file mqtt_client_app.c
 * @brief MQTT Client Application Implementation
 *
 * Implements WiFi connectivity and MQTT publishing for BME680 sensor data.
 */

#include "mqtt_client_app.h"

#include "cJSON.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "nvs_flash.h"
#include <string.h>
#include <time.h>

/* ========================== Private Definitions ========================== */

#define TAG "MQTT_APP"

/* WiFi Event Group bits */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

/* ========================== Private Variables ============================ */

/* WiFi */
static EventGroupHandle_t s_wifi_event_group = NULL;
static int s_retry_num = 0;
static bool s_wifi_connected = false;

/* MQTT */
static esp_mqtt_client_handle_t s_mqtt_client = NULL;
static mqtt_status_t s_mqtt_status = MQTT_STATUS_DISCONNECTED;
static SemaphoreHandle_t s_mqtt_mutex = NULL;

/* ========================== Private Functions ============================ */

/**
 * @brief WiFi event handler
 */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    s_wifi_connected = false;
    if (s_retry_num < WIFI_MAXIMUM_RETRY) {
      esp_wifi_connect();
      s_retry_num++;
      ESP_LOGI(TAG, "Retrying WiFi connection... (%d/%d)", s_retry_num,
               WIFI_MAXIMUM_RETRY);
    } else {
      xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
      ESP_LOGE(TAG, "WiFi connection failed after %d attempts",
               WIFI_MAXIMUM_RETRY);
    }
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "Connected! IP: " IPSTR, IP2STR(&event->ip_info.ip));
    s_retry_num = 0;
    s_wifi_connected = true;
    xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
  }
}

/**
 * @brief MQTT event handler
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data) {
  esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

  switch ((esp_mqtt_event_id_t)event_id) {
  case MQTT_EVENT_CONNECTED:
    ESP_LOGI(TAG, "MQTT Connected to broker");
    s_mqtt_status = MQTT_STATUS_CONNECTED;

#if !MQTT_USE_THINGSBOARD
    /* Publish online status (ThingsBoard uses last activity for status) */
    mqtt_publish_status("online");
#endif
    break;

  case MQTT_EVENT_DISCONNECTED:
    ESP_LOGW(TAG, "MQTT Disconnected from broker");
    s_mqtt_status = MQTT_STATUS_DISCONNECTED;
    break;

  case MQTT_EVENT_SUBSCRIBED:
    ESP_LOGI(TAG, "MQTT Subscribed, msg_id=%d", event->msg_id);
    break;

  case MQTT_EVENT_UNSUBSCRIBED:
    ESP_LOGI(TAG, "MQTT Unsubscribed, msg_id=%d", event->msg_id);
    break;

  case MQTT_EVENT_PUBLISHED:
    ESP_LOGD(TAG, "MQTT Message published, msg_id=%d", event->msg_id);
    break;

  case MQTT_EVENT_DATA:
    ESP_LOGI(TAG, "MQTT Data received");
    ESP_LOGI(TAG, "  Topic: %.*s", event->topic_len, event->topic);
    ESP_LOGI(TAG, "  Data: %.*s", event->data_len, event->data);
    break;

  case MQTT_EVENT_ERROR:
    ESP_LOGE(TAG, "MQTT Error occurred");
    s_mqtt_status = MQTT_STATUS_ERROR;
    if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
      ESP_LOGE(TAG, "TCP transport error: %s",
               strerror(event->error_handle->esp_transport_sock_errno));
    }
    break;

  default:
    ESP_LOGD(TAG, "MQTT Event: %d", event->event_id);
    break;
  }
}

/**
 * @brief Create JSON string from sensor data
 */
static char *create_sensor_json(const mqtt_sensor_data_t *data) {
  cJSON *root = cJSON_CreateObject();
  if (root == NULL) {
    return NULL;
  }

  cJSON_AddNumberToObject(root, "temperature", data->temperature);
  cJSON_AddNumberToObject(root, "humidity", data->humidity);
  cJSON_AddNumberToObject(root, "pressure", data->pressure);
  cJSON_AddNumberToObject(root, "gas_resistance", data->gas_resistance);
  cJSON_AddBoolToObject(root, "gas_valid", data->gas_valid);
  cJSON_AddNumberToObject(root, "timestamp", (double)time(NULL));

  char *json_str = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);

  return json_str;
}

/**
 * @brief Create JSON string from IAQ data
 */
static char *create_iaq_json(const mqtt_iaq_data_t *data) {
  cJSON *root = cJSON_CreateObject();
  if (root == NULL) {
    return NULL;
  }

  cJSON_AddNumberToObject(root, "iaq_score", data->iaq_score);
  cJSON_AddNumberToObject(root, "iaq_level", data->iaq_level);
  cJSON_AddStringToObject(root, "iaq_text",
                          data->iaq_text ? data->iaq_text : "Unknown");
  cJSON_AddNumberToObject(root, "accuracy", data->accuracy);
  cJSON_AddNumberToObject(root, "co2_equivalent", data->co2_equivalent);
  cJSON_AddNumberToObject(root, "voc_equivalent", data->voc_equivalent);
  cJSON_AddBoolToObject(root, "is_calibrated", data->is_calibrated);
  cJSON_AddNumberToObject(root, "timestamp", (double)time(NULL));

  char *json_str = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);

  return json_str;
}

#if MQTT_USE_THINGSBOARD
/**
 * @brief Create JSON for ThingsBoard telemetry (sensor + IAQ in one payload)
 */
static char *create_thingsboard_telemetry_json(const mqtt_sensor_data_t *sensor,
                                                const mqtt_iaq_data_t *iaq) {
  cJSON *root = cJSON_CreateObject();
  if (root == NULL) {
    return NULL;
  }

  cJSON_AddNumberToObject(root, "temperature", sensor->temperature);
  cJSON_AddNumberToObject(root, "humidity", sensor->humidity);
  cJSON_AddNumberToObject(root, "pressure", sensor->pressure);
  cJSON_AddNumberToObject(root, "gas_resistance", sensor->gas_resistance);
  cJSON_AddBoolToObject(root, "gas_valid", sensor->gas_valid);

  if (iaq != NULL) {
    cJSON_AddNumberToObject(root, "iaq_score", iaq->iaq_score);
    cJSON_AddNumberToObject(root, "iaq_level", iaq->iaq_level);
    cJSON_AddNumberToObject(root, "co2_equivalent", iaq->co2_equivalent);
    cJSON_AddNumberToObject(root, "voc_equivalent", iaq->voc_equivalent);
    cJSON_AddBoolToObject(root, "is_calibrated", iaq->is_calibrated);
    cJSON_AddNumberToObject(root, "accuracy", iaq->accuracy);
    if (iaq->iaq_text != NULL) {
      cJSON_AddStringToObject(root, "iaq_text", iaq->iaq_text);
    }
  }

  /* Optional: client timestamp in ms (ThingsBoard accepts ts in payload) */
  cJSON_AddNumberToObject(root, "ts", (double)(time(NULL) * 1000));

  char *json_str = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  return json_str;
}
#endif

/* ========================== Public Functions ============================= */

esp_err_t wifi_init_sta(void) {
  ESP_LOGI(TAG, "Initializing WiFi Station mode...");

  /* Create event group */
  s_wifi_event_group = xEventGroupCreate();
  if (s_wifi_event_group == NULL) {
    ESP_LOGE(TAG, "Failed to create WiFi event group");
    return ESP_ERR_NO_MEM;
  }

  /* Initialize TCP/IP stack */
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();

  /* Initialize WiFi with default config */
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  /* Register event handlers */
  esp_event_handler_instance_t instance_any_id;
  esp_event_handler_instance_t instance_got_ip;

  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL,
      &instance_any_id));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL,
      &instance_got_ip));

  /* Configure WiFi */
  wifi_config_t wifi_config = {
      .sta =
          {
              .ssid = WIFI_SSID,
              .password = WIFI_PASSWORD,
              .threshold.authmode = WIFI_AUTH_WPA2_PSK,
              .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
          },
  };

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_LOGI(TAG, "WiFi initialization complete, connecting to SSID: %s",
           WIFI_SSID);

  /* Wait for connection result */
  EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                         WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                         pdFALSE, pdFALSE, portMAX_DELAY);

  if (bits & WIFI_CONNECTED_BIT) {
    ESP_LOGI(TAG, "WiFi connected successfully");
    return ESP_OK;
  } else if (bits & WIFI_FAIL_BIT) {
    ESP_LOGE(TAG, "WiFi connection failed");
    return ESP_FAIL;
  }

  return ESP_ERR_TIMEOUT;
}

esp_err_t mqtt_app_init(void) {
  ESP_LOGI(TAG, "Initializing MQTT client...");

  /* Create mutex */
  s_mqtt_mutex = xSemaphoreCreateMutex();
  if (s_mqtt_mutex == NULL) {
    ESP_LOGE(TAG, "Failed to create MQTT mutex");
    return ESP_ERR_NO_MEM;
  }

  /* Configure MQTT client */
  esp_mqtt_client_config_t mqtt_cfg = {
      .broker.address.uri = MQTT_BROKER_URI,
      .credentials.client_id = MQTT_CLIENT_ID,
      .session.keepalive = 60,
      .network.reconnect_timeout_ms = 5000,
  };
#if MQTT_USE_THINGSBOARD
  if (strlen(MQTT_ACCESS_TOKEN) > 0) {
    mqtt_cfg.credentials.username = MQTT_ACCESS_TOKEN;
    mqtt_cfg.credentials.authentication.password = "";
  }
#endif

  /* Create MQTT client */
  s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
  if (s_mqtt_client == NULL) {
    ESP_LOGE(TAG, "Failed to create MQTT client");
    return ESP_FAIL;
  }

  /* Register event handler */
  ESP_ERROR_CHECK(esp_mqtt_client_register_event(
      s_mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL));

  ESP_LOGI(TAG, "MQTT client initialized");
  ESP_LOGI(TAG, "  Broker: %s", MQTT_BROKER_URI);
  ESP_LOGI(TAG, "  Client ID: %s", MQTT_CLIENT_ID);

  return ESP_OK;
}

esp_err_t mqtt_app_start(void) {
  if (s_mqtt_client == NULL) {
    ESP_LOGE(TAG, "MQTT client not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  s_mqtt_status = MQTT_STATUS_CONNECTING;
  ESP_LOGI(TAG, "Starting MQTT client...");

  return esp_mqtt_client_start(s_mqtt_client);
}

esp_err_t mqtt_app_stop(void) {
  if (s_mqtt_client == NULL) {
    return ESP_ERR_INVALID_STATE;
  }

#if !MQTT_USE_THINGSBOARD
  /* Publish offline status before disconnecting */
  mqtt_publish_status("offline");
  vTaskDelay(pdMS_TO_TICKS(100));
#endif

  s_mqtt_status = MQTT_STATUS_DISCONNECTED;
  return esp_mqtt_client_stop(s_mqtt_client);
}

esp_err_t mqtt_publish_sensor_data(const mqtt_sensor_data_t *data) {
  if (data == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  if (s_mqtt_status != MQTT_STATUS_CONNECTED) {
    ESP_LOGW(TAG, "MQTT not connected, skipping sensor data publish");
    return ESP_ERR_INVALID_STATE;
  }

  /* Create JSON payload */
  char *json_str = create_sensor_json(data);
  if (json_str == NULL) {
    ESP_LOGE(TAG, "Failed to create sensor JSON");
    return ESP_ERR_NO_MEM;
  }

  /* Publish to broker */
  int msg_id = esp_mqtt_client_publish(s_mqtt_client, MQTT_TOPIC_SENSOR,
                                       json_str, strlen(json_str), 1, 0);

  free(json_str);

  if (msg_id < 0) {
    ESP_LOGE(TAG, "Failed to publish sensor data");
    return ESP_FAIL;
  }

  ESP_LOGD(TAG, "Sensor data published, msg_id=%d", msg_id);
  return ESP_OK;
}

esp_err_t mqtt_publish_iaq_data(const mqtt_iaq_data_t *data) {
  if (data == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  if (s_mqtt_status != MQTT_STATUS_CONNECTED) {
    ESP_LOGW(TAG, "MQTT not connected, skipping IAQ data publish");
    return ESP_ERR_INVALID_STATE;
  }

  /* Create JSON payload */
  char *json_str = create_iaq_json(data);
  if (json_str == NULL) {
    ESP_LOGE(TAG, "Failed to create IAQ JSON");
    return ESP_ERR_NO_MEM;
  }

  /* Publish to broker */
  int msg_id = esp_mqtt_client_publish(s_mqtt_client, MQTT_TOPIC_IAQ, json_str,
                                       strlen(json_str), 1, 0);

  free(json_str);

  if (msg_id < 0) {
    ESP_LOGE(TAG, "Failed to publish IAQ data");
    return ESP_FAIL;
  }

  ESP_LOGD(TAG, "IAQ data published, msg_id=%d", msg_id);
  return ESP_OK;
}

esp_err_t mqtt_publish_status(const char *status) {
  if (status == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  if (s_mqtt_client == NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  /* Create JSON payload */
  cJSON *root = cJSON_CreateObject();
  if (root == NULL) {
    return ESP_ERR_NO_MEM;
  }

  cJSON_AddStringToObject(root, "status", status);
  cJSON_AddStringToObject(root, "client_id", MQTT_CLIENT_ID);
  cJSON_AddNumberToObject(root, "timestamp", (double)time(NULL));

  char *json_str = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);

  if (json_str == NULL) {
    return ESP_ERR_NO_MEM;
  }

  /* Publish with retain flag for status */
  int msg_id = esp_mqtt_client_publish(s_mqtt_client, MQTT_TOPIC_STATUS,
                                       json_str, strlen(json_str), 1, 1);

  free(json_str);

  if (msg_id < 0) {
    ESP_LOGE(TAG, "Failed to publish status");
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Status published: %s", status);
  return ESP_OK;
}

esp_err_t mqtt_publish_alert(const char *alert_type, const char *message) {
  if (alert_type == NULL || message == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  if (s_mqtt_status != MQTT_STATUS_CONNECTED) {
    return ESP_ERR_INVALID_STATE;
  }

  /* Create JSON payload */
  cJSON *root = cJSON_CreateObject();
  if (root == NULL) {
    return ESP_ERR_NO_MEM;
  }

  cJSON_AddStringToObject(root, "type", alert_type);
  cJSON_AddStringToObject(root, "message", message);
  cJSON_AddStringToObject(root, "client_id", MQTT_CLIENT_ID);
  cJSON_AddNumberToObject(root, "timestamp", (double)time(NULL));

  char *json_str = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);

  if (json_str == NULL) {
    return ESP_ERR_NO_MEM;
  }

  /* Publish alert */
  int msg_id = esp_mqtt_client_publish(s_mqtt_client, MQTT_TOPIC_ALERT,
                                       json_str, strlen(json_str), 1, 0);

  free(json_str);

  if (msg_id < 0) {
    ESP_LOGE(TAG, "Failed to publish alert");
    return ESP_FAIL;
  }

  ESP_LOGW(TAG, "Alert published: [%s] %s", alert_type, message);
  return ESP_OK;
}

#if MQTT_USE_THINGSBOARD
esp_err_t mqtt_publish_thingsboard_telemetry(const mqtt_sensor_data_t *sensor,
                                              const mqtt_iaq_data_t *iaq) {
  if (sensor == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  if (s_mqtt_status != MQTT_STATUS_CONNECTED) {
    ESP_LOGW(TAG, "MQTT not connected, skipping ThingsBoard telemetry");
    return ESP_ERR_INVALID_STATE;
  }

  char *json_str = create_thingsboard_telemetry_json(sensor, iaq);
  if (json_str == NULL) {
    ESP_LOGE(TAG, "Failed to create ThingsBoard telemetry JSON");
    return ESP_ERR_NO_MEM;
  }

  int msg_id = esp_mqtt_client_publish(s_mqtt_client, MQTT_TOPIC_TELEMETRY,
                                       json_str, strlen(json_str), 1, 0);
  free(json_str);

  if (msg_id < 0) {
    ESP_LOGE(TAG, "Failed to publish ThingsBoard telemetry");
    return ESP_FAIL;
  }
  ESP_LOGD(TAG, "ThingsBoard telemetry published, msg_id=%d", msg_id);
  return ESP_OK;
}
#endif

mqtt_status_t mqtt_get_status(void) { return s_mqtt_status; }

bool mqtt_is_connected(void) { return s_mqtt_status == MQTT_STATUS_CONNECTED; }

bool wifi_is_connected(void) { return s_wifi_connected; }
