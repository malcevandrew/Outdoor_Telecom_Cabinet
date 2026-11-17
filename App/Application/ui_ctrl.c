#include "ui_ctrl.h"
#include "gpio.h"
#include <stdio.h>

// Кнопка смены показаний на дисплее
#define UI_BTN_GPIO_PORT    GPIOB        
#define UI_BTN_GPIO_PIN     GPIO_PIN_13

#define SCREEN_COUNT        4U          
#define BTN_DEBOUNCE_MS     50U         

static uint8_t current_screen = 0;
static bool last_btn_pressed = false;
static uint32_t btn_debounce_ms = 0;


// Инициализация
void UICtrl_Init(void)
{
    current_screen = 0;
}

// Проверка нажатия на кнопку для переключения информации на дисплее
static void HandleScreenButton(uint32_t now)
{
    bool raw_pressed = (HAL_GPIO_ReadPin(UI_BTN_GPIO_PORT, UI_BTN_GPIO_PIN) == GPIO_PIN_RESET);

    if (raw_pressed != last_btn_pressed) {
        if ((now - btn_debounce_ms) >= BTN_DEBOUNCE_MS) {   // Программный антидребезг
            btn_debounce_ms = now;
            last_btn_pressed = raw_pressed;

            if (raw_pressed) {
                current_screen = (uint8_t)((current_screen + 1U) % SCREEN_COUNT);   // Текущий номер экрана (0 -> 1 -> 2 -> .. -> SCREEN_COUNT-1 -> 0)
            }
        }
    }
}

// Формирование текста для дисплея и включение сирены при внештатных ситуациях
void UICtrl_Update(lcd1602_t *lcd, const system_telemetry_t *sys)
{
    if (lcd == NULL || sys == NULL) {
        return;
    }

    char line0[17] = {0};
    char line1[17] = {0};

    uint32_t now = HAL_GetTick();

    // Проверка нажатия кнопки для переключения информации на дисплее 
    HandleScreenButton(now);        

    // Приоритетный вывод критических предупреждений поверх экранов
    if (sys->access_alarm) {
        snprintf(line0, sizeof(line0), "! ACCESS ALARM !");
        snprintf(line1, sizeof(line1), "%s",
                 sys->access->last_card_rejected ? "UNKNOWN RFID TAG" : "AUTH TIMEOUT!");
        LCD1602_SetLine(lcd, 0, line0);
        LCD1602_SetLine(lcd, 1, line1);
        return;
    }

    if (sys->dew_critical) {
        snprintf(line0, sizeof(line0), "! DEW CRITICAL !");
        snprintf(line1, sizeof(line1), "dT:%.1fC HEAT:ON", sys->dew->temp_margin_c);
        LCD1602_SetLine(lcd, 0, line0);
        LCD1602_SetLine(lcd, 1, line1);
        return;
    }

    // Штатные 4 экрана
    switch (current_screen) {
        case 0: // Экран 1. Микроклимат 
            if (sys->aht20->is_valid) {
                snprintf(line0, sizeof(line0), "1.Air T: %.1f C", sys->aht20->temperature_c);
                snprintf(line1, sizeof(line1), "  Humid: %.1f %%", sys->aht20->humidity_rh);
            } else {
                snprintf(line0, sizeof(line0), "1.Air T: ERR");
                snprintf(line1, sizeof(line1), "  Humid: ERR");
            }
            break;

        case 1: // Экран 2. Давление и конденсация 
            if (sys->bmp280->is_valid) {
                snprintf(line0, sizeof(line0), "2.P:%.0fmm D:%.1fC",
                         sys->bmp280->pressure_mmhg,
                         sys->dew->is_valid ? sys->dew->dew_point_c : 0.0f);
            } else {
                snprintf(line0, sizeof(line0), "2.P:ERR  D:--.-C");
            }

            if (sys->analog->is_valid && sys->dew->is_valid) {
                snprintf(line1, sizeof(line1), "S:%.1fC dT:%.1fC",
                         sys->analog->surface_temp_c, sys->dew->temp_margin_c);
            } else {
                snprintf(line1, sizeof(line1), "S:ERR   dT:ERR");
            }
            break;

        case 2: // Экран 3. Энергопотребление 
            if (sys->ina219->is_valid) {
                snprintf(line0, sizeof(line0), "3.U:%.2fV I:%.0fmA",
                         sys->ina219->bus_voltage_v, sys->ina219->current_ma);
                snprintf(line1, sizeof(line1), "  Power: %.0f mW", sys->ina219->power_mw);
            } else {
                snprintf(line0, sizeof(line0), "3.Power Meter");
                snprintf(line1, sizeof(line1), "  SENSOR ERROR");
            }
            break;

        case 3: // Экран 4. Состояние системы 
        default: {
            uint16_t fan_pct = sys->climate->fan_pwm_duty / 10U;
            const char *door_str = "CLS";

            if (sys->access->state == ACCESS_STATE_WAIT_AUTH) door_str = "OPN";
            else if (sys->access->state == ACCESS_STATE_AUTHORIZED) door_str = "AUT";

            snprintf(line0, sizeof(line0), "4.Fan:%u%% Ht:%s",
                     fan_pct, sys->climate->heater_relay_on ? "ON" : "OFF");
            snprintf(line1, sizeof(line1), "Dr:%s Set:%.0fC%s",
                     door_str, sys->analog->fan_setpoint_c, sys->sensor_fault ? " !" : "");
            break;
        }
    }

    LCD1602_SetLine(lcd, 0, line0);
    LCD1602_SetLine(lcd, 1, line1);
}