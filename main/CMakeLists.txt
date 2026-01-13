idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES bme680_app buzzer i2c_config iaq_calculator mqtt_client nvs_flash log freertos
)
