idf_component_register(
    SRCS "iaq_calculator.c"
    INCLUDE_DIRS "."
    REQUIRES freertos nvs_flash log esp_system
)
