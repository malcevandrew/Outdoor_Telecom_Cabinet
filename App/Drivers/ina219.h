#ifndef INA219_H
#define INA219_H

#include "i2c.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    I2C_HandleTypeDef *hi2c;
    uint8_t dev_addr;
    float bus_voltage_v;   // Напряжение шины нагрузки (В) 
    float current_ma;      // Ток нагрузки (мА) 
    float power_mw;        // Мощность нагрузки (мВт) 
    bool is_valid;
    uint8_t error_count;
} ina219_t;

bool INA219_Init(ina219_t *dev, I2C_HandleTypeDef *hi2c);
void INA219_Process(ina219_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* INA219_H */