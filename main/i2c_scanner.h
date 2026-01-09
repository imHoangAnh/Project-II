/**
 * @file i2c_scanner.h
 * @brief I2C Scanner header
 */

#ifndef I2C_SCANNER_H
#define I2C_SCANNER_H

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief Initialize I2C for scanning
     */
    void i2c_scanner_init(void);

    /**
     * @brief Scan I2C bus for devices
     */
    void i2c_scanner_scan(void);

#ifdef __cplusplus
}
#endif

#endif // I2C_SCANNER_H

