idf_component_register(
    SRCS "bme680_app.c"
    INCLUDE_DIRS "."
    REQUIRES bme680 i2c_config driver log freertos esp_rom
)
