#ifndef BMP280_H
#define BMP280_H

#include "i2c.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t dig_T1;
    int16_t  dig_T2;
    int16_t  dig_T3;
    uint16_t dig_P1;
    int16_t  dig_P2;
    int16_t  dig_P3;
    int16_t  dig_P4;
    int16_t  dig_P5;
    int16_t  dig_P6;
    int16_t  dig_P7;
    int16_t  dig_P8;
    int16_t  dig_P9;
} bmp280_calib_t;

typedef struct {
    I2C_HandleTypeDef *hi2c;
    uint8_t dev_addr;
    bmp280_calib_t calib;
    float pressure_hpa;
    float pressure_mmhg;
    float temperature_c;
    bool is_valid;
    uint8_t error_count;
} bmp280_t;

bool BMP280_Init(bmp280_t *dev, I2C_HandleTypeDef *hi2c);
void BMP280_Process(bmp280_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* BMP280_H */