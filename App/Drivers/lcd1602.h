#ifndef LCD1602_H
#define LCD1602_H

#include "i2c.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    I2C_HandleTypeDef *hi2c;
    uint8_t dev_addr;
    char target_lines[2][17];
    char current_lines[2][17];
    uint8_t cursor_row;
    uint8_t cursor_col;
    bool backlight;
    bool is_valid;
    uint8_t error_count;
} lcd1602_t;

bool LCD1602_Init(lcd1602_t *dev, I2C_HandleTypeDef *hi2c);
void LCD1602_SetLine(lcd1602_t *dev, uint8_t row, const char *text);
void LCD1602_SetBacklight(lcd1602_t *dev, bool state);
void LCD1602_Process(lcd1602_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* LCD1602_H */