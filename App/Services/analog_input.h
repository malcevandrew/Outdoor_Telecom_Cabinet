#ifndef ANALOG_INPUT_H
#define ANALOG_INPUT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float surface_temp_c;    // Температура стенки шкафа (°C)
    float surface_voltage_v; // Напряжение на входе канала температуры (В) 
    float fan_setpoint_c;    // Заданный порог включения вентиляции (°C) 
    uint16_t raw_temp_ch;    // Усреднённое сырое значение ADC канала температуры 
    uint16_t raw_setpoint_ch;// Усреднённое сырое значение ADC канала задания порога 
    bool is_valid;           // Флаг достоверности канала температуры 
} analog_data_t;

void AnalogInput_Init(void);
void AnalogInput_Process(void);
analog_data_t AnalogInput_GetData(void);

#ifdef __cplusplus
}
#endif

#endif /* ANALOG_INPUT_H */
