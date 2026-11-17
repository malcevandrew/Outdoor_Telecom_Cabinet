#ifndef UI_CTRL_H
#define UI_CTRL_H

#include "app_main.h"

#include "lcd1602.h"
#include "aht20.h"
#include "bmp280.h"
#include "ina219.h"
#include "analog_input.h"
#include "dew_point.h"
#include "climate_ctrl.h"
#include "access_ctrl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif


void UICtrl_Init(void);
void UICtrl_Update(lcd1602_t *lcd, const system_telemetry_t *sys);

#ifdef __cplusplus
}
#endif

#endif /* UI_CTRL_H */