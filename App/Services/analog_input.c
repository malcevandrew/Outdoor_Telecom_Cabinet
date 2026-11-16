#include "analog_input.h"
#include "adc.h"

// Количество выборок для усреднения по каждому каналу
#define ADC_SAMPLES_PER_CHANNEL  32U
#define ADC_CHANNELS_COUNT       2U     // Используем 2 канала АЦП.
                                        // 1 - измерение с потенциометра, имитирующего датчик температуры на стенке шкафа. 
                                        // 2 - измерение со второго потенциометра, регулирующего температурный порог включения кулера для охлаждения

#define ADC_DMA_BUFFER_SIZE      (ADC_SAMPLES_PER_CHANNEL * ADC_CHANNELS_COUNT)

// Параметры АЦП 
#define ADC_VREF_VOLTS           3.30f          // Опорное напряжение
#define ADC_MAX_COUNTS           4095.0f        // Разрядность АЦП 12бит (т.е. цена деления АЦП равна 2^12 - 1 = 4095)

// Калибровочные границы аналогового канала температуры поверхности  
#define SURF_TEMP_MIN_V          0.05f
#define SURF_TEMP_MAX_V          3.25f
#define SURF_TEMP_MIN_C          (-20.0f)
#define SURF_TEMP_MAX_C          40.0f

// Диапазон регулировки порога включения кулера (°C)  
#define FAN_SETPOINT_MIN_C       10.0f
#define FAN_SETPOINT_MAX_C       35.0f

// Кольцевой буфер для DMA: [TEMP_0, SETPOINT_0, TEMP_1, SETPOINT_1, ...]  
static volatile uint16_t adc_dma_buffer[ADC_DMA_BUFFER_SIZE];

// Инициализация
void AnalogInput_Init(analog_data_t *current_data)
{
    current_data->surface_temp_c    = 0;       
    current_data->surface_voltage_v = 0;    
    current_data->fan_setpoint_c    = 0;       
    current_data->raw_temp_ch       = 0;       
    current_data->raw_setpoint_ch   = 0;        

    if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_dma_buffer, ADC_DMA_BUFFER_SIZE) != HAL_OK) {
        current_data->is_valid = false;
    } else {
        current_data->is_valid = true;
    }
}

// Определение значения температуры на стенке шкафа (для проведения тестов датчик имитируем потенциометром) 
void AnalogInput_Process(analog_data_t* current_data)
{
    uint32_t sum_temp     = 0;
    uint32_t sum_setpoint = 0;

    // Читаем данные из кольцевого буфера 
    for (uint32_t i = 0; i < ADC_SAMPLES_PER_CHANNEL; i++) {
        sum_temp     += adc_dma_buffer[i * ADC_CHANNELS_COUNT + 0U];
        sum_setpoint += adc_dma_buffer[i * ADC_CHANNELS_COUNT + 1U];
    }

    // Усредняем эти данные
    uint16_t avg_temp     = (uint16_t)(sum_temp / ADC_SAMPLES_PER_CHANNEL);
    uint16_t avg_setpoint = (uint16_t)(sum_setpoint / ADC_SAMPLES_PER_CHANNEL);

    current_data->raw_temp_ch     = avg_temp;
    current_data->raw_setpoint_ch = avg_setpoint;

    // Расчёт напряжения на канале температуры поверхности стенки шкафа  
    float voltage_temp = ((float)avg_temp / ADC_MAX_COUNTS) * ADC_VREF_VOLTS;
    current_data->surface_voltage_v = voltage_temp;

    // Пропорциональный перерасчет напряжения в температуру
    if (voltage_temp < SURF_TEMP_MIN_V || voltage_temp > SURF_TEMP_MAX_V) {
        current_data->is_valid = false;
    } else {
        current_data->is_valid = true;
        float ratio = (voltage_temp - SURF_TEMP_MIN_V) / (SURF_TEMP_MAX_V - SURF_TEMP_MIN_V);           
        current_data->surface_temp_c = SURF_TEMP_MIN_C + ratio * (SURF_TEMP_MAX_C - SURF_TEMP_MIN_C);
    }

     // Пропорциональный расчет на канале регулировки порога включения кулера в диапазоне от FAN_SETPOINT_MIN_C до FAN_SETPOINT_MAX_C 
    float setpoint_ratio = (float)avg_setpoint / ADC_MAX_COUNTS;                    // Один шаг потенциометра
    current_data->fan_setpoint_c = FAN_SETPOINT_MIN_C + setpoint_ratio * (FAN_SETPOINT_MAX_C - FAN_SETPOINT_MIN_C);
}
