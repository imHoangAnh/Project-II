/**
 * @file i2c_config.h
 * @brief I2C Configuration and initialization for ESP32
 */

#ifndef I2C_CONFIG_H
#define I2C_CONFIG_H

#include "driver/i2c.h"
#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* I2C Configuration */
#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_SCL_IO 7       // GPIO7 cho SCL
#define I2C_MASTER_SDA_IO 6       // GPIO6 cho SDA
#define I2C_MASTER_FREQ_HZ 100000 // 100kHz
#define I2C_MASTER_TIMEOUT_MS 1000

/**
 * @brief Initialize I2C master
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t i2c_master_init(void);

/**
 * @brief Delete I2C driver
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t i2c_master_deinit(void);

/**
 * @brief Get I2C port number
 * @return I2C port number
 */
i2c_port_t i2c_get_port(void);

/**
 * @brief Get I2C timeout in ticks
 * @return Timeout in ticks
 */
TickType_t i2c_get_timeout_ticks(void);

#ifdef __cplusplus
}
#endif

#endif // I2C_CONFIG_H
