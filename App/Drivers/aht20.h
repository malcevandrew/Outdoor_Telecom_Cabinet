#ifndef AHT20_H
#define AHT20_H

#include "i2c.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AHT20_STATE_IDLE = 0,       // Состояние ожидания
    AHT20_STATE_MEASURING,      // Состояние измерения
    AHT20_STATE_ERROR           // Состояние ошибки
} aht20_state_t;

typedef struct {
    I2C_HandleTypeDef *hi2c;
    uint8_t dev_addr;
    aht20_state_t state;
    uint32_t meas_start_ms;
    float temperature_c;
    float humidity_rh;
    bool is_valid;
    uint8_t error_count;
} aht20_t;

bool AHT20_Init(aht20_t *dev, I2C_HandleTypeDef *hi2c);
void AHT20_Process(aht20_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* AHT20_H */
