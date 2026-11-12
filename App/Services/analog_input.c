#include "analog_input.h"
#include "adc.h"

/* Количество выборок для усреднения по каждому каналу */
#define ADC_SAMPLES_PER_CHANNEL  32U
#define ADC_CHANNELS_COUNT       2U
#define ADC_DMA_BUFFER_SIZE      (ADC_SAMPLES_PER_CHANNEL * ADC_CHANNELS_COUNT)

/* Параметры АЦП */
#define ADC_VREF_VOLTS           3.30f
#define ADC_MAX_COUNTS           4095.0f

/* Калибровочные границы аналогового канала температуры поверхности */
#define SURF_TEMP_MIN_V          0.05f
#define SURF_TEMP_MAX_V          3.25f
#define SURF_TEMP_MIN_C          (-20.0f)
#define SURF_TEMP_MAX_C          40.0f

/* Диапазон регулировки порога включения вентилятора (°C) */
#define FAN_SETPOINT_MIN_C       20.0f
#define FAN_SETPOINT_MAX_C       45.0f

/* Кольцевой буфер для DMA: [TEMP_0, SETPOINT_0, TEMP_1, SETPOINT_1, ...] */
static volatile uint16_t adc_dma_buffer[ADC_DMA_BUFFER_SIZE];

static analog_data_t current_data = {0};

void AnalogInput_Init(void)
{
    if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_dma_buffer, ADC_DMA_BUFFER_SIZE) != HAL_OK) {
        current_data.is_valid = false;
    } else {
        current_data.is_valid = true;
    }
}

void AnalogInput_Process(void)
{
    uint32_t sum_temp     = 0;
    uint32_t sum_setpoint = 0;

    /* Читаем данные из кольцевого буфера и усредняем */
    for (uint32_t i = 0; i < ADC_SAMPLES_PER_CHANNEL; i++) {
        sum_temp     += adc_dma_buffer[i * ADC_CHANNELS_COUNT + 0U];
        sum_setpoint += adc_dma_buffer[i * ADC_CHANNELS_COUNT + 1U];
    }

    uint16_t avg_temp     = (uint16_t)(sum_temp / ADC_SAMPLES_PER_CHANNEL);
    uint16_t avg_setpoint = (uint16_t)(sum_setpoint / ADC_SAMPLES_PER_CHANNEL);

    current_data.raw_temp_ch     = avg_temp;
    current_data.raw_setpoint_ch = avg_setpoint;

    /* Расчёт напряжения на канале температуры поверхности стенки шкафа */
    float voltage_temp = ((float)avg_temp / ADC_MAX_COUNTS) * ADC_VREF_VOLTS;
    current_data.surface_voltage_v = voltage_temp;

    /* Контроль достоверности сигнала (защита от обрыва и КЗ линии датчика) */
    if (voltage_temp < SURF_TEMP_MIN_V || voltage_temp > SURF_TEMP_MAX_V) {
        current_data.is_valid = false;
    } else {
        current_data.is_valid = true;
        float ratio = (voltage_temp - SURF_TEMP_MIN_V) / (SURF_TEMP_MAX_V - SURF_TEMP_MIN_V);           // Пропорция между напряжением и температурой
        current_data.surface_temp_c = SURF_TEMP_MIN_C + ratio * (SURF_TEMP_MAX_C - SURF_TEMP_MIN_C);
    }

    /* Масштабирование канала уставки в диапазон [20.0 .. 45.0 °C] */
    float setpoint_ratio = (float)avg_setpoint / ADC_MAX_COUNTS;                    // Один шаг
    current_data.fan_setpoint_c = FAN_SETPOINT_MIN_C + setpoint_ratio * (FAN_SETPOINT_MAX_C - FAN_SETPOINT_MIN_C);
}

analog_data_t AnalogInput_GetData(void)
{
    return current_data;
}
