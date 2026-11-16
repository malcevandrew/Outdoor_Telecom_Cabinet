#ifndef RC522_H
#define RC522_H

#include "spi.h"
#include "gpio.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RC522_UID_MAX_LEN 4U

typedef struct {
    SPI_HandleTypeDef *hspi;
    GPIO_TypeDef *cs_port;
    uint16_t cs_pin;
    GPIO_TypeDef *rst_port;
    uint16_t rst_pin;
    bool is_valid;
    uint8_t error_count;
} rc522_t;

bool RC522_Init(rc522_t *dev, SPI_HandleTypeDef *hspi,
                GPIO_TypeDef *cs_port, uint16_t cs_pin,
                GPIO_TypeDef *rst_port, uint16_t rst_pin);

bool RC522_PollCard(rc522_t *dev, uint8_t uid_out[RC522_UID_MAX_LEN]);

#ifdef __cplusplus
}
#endif

#endif /* RC522_H */