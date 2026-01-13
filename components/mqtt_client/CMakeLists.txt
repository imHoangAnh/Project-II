idf_component_register(SRCS "mqtt_client_app.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_wifi esp_event esp_netif mqtt nvs_flash json esp_http_client)
