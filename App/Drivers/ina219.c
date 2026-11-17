#include "ina219.h"

#define INA219_I2C_ADDR  (0x40U << 1)   // 7 битный адрес датчика(сразу смещен влево на 1 бит, для добавления бита чтения/записи функцией HAL)

#define INA219_REG_CONFIG       0x00U   // Адрес регистра конфигурации
#define INA219_REG_SHUNT_V      0x01U   // Адрес регистра, содержащий значение напряжения на шунтирующем резисторе
#define INA219_REG_BUS_V        0x02U   // Адрес регистра, содержащий значение напряжения на шине
#define INA219_REG_POWER        0x03U   // Адрес регистра, содержащий значение мощности
#define INA219_REG_CURRENT      0x04U   // Адрес регистра, содержащий значение така, идущего через шунтирующий резистор
#define INA219_REG_CALIBRATION  0x05U   // Адрес регистра, содержащий значения калибровки

#define INA219_CONFIG_RESET     0x8000U // Значение, записываемое в регистр конфигурации для сброса: 1000 0000 0000 0000 
#define INA219_CONFIG_DEFAULT   0x399FU // Значение, записываемое в регистр конфигурации при инициализации. Устанавливаем параметры по умолчанию(32В, 12 бит АЦП): 0011 1001 1001 1111 
#define INA219_CALIB_32V_2A     4096U   // Значение, записываемое в регистр калибровки

#define INA219_I2C_TIMEOUT_MS   20U     // Тайматут для функций записи и чтения
#define INA219_MAX_ERRORS       3U      // Количество неудачных попыток связи до перехода в состояние ошибки


// Запись в 16-битный регистр INA219(для I2C это запись двух байт)
static bool INA219_WriteReg16(ina219_t *dev, uint8_t reg, uint16_t val)
{
    uint8_t buf[2] = {(uint8_t)(val >> 8), (uint8_t)(val & 0xFFU)};     // buf[0] - старший байт у val, buf[1] - младший байт у val
    return (HAL_I2C_Mem_Write(dev->hi2c, dev->dev_addr, reg, I2C_MEMADD_SIZE_8BIT, buf, 2, INA219_I2C_TIMEOUT_MS) == HAL_OK);
}

// Чтение из 16-битного регистра в INA219(для I2C это чтение двух байт)
static bool INA219_ReadReg16(ina219_t *dev, uint8_t reg, uint16_t *val)
{
    uint8_t buf[2] = {0};
    if (HAL_I2C_Mem_Read(dev->hi2c, dev->dev_addr, reg, I2C_MEMADD_SIZE_8BIT, buf, 2, INA219_I2C_TIMEOUT_MS) != HAL_OK) {
        return false;
    }
    *val = (uint16_t)(((uint16_t)buf[0] << 8) | buf[1]);    // Первые 8 бит у val это buf[0], вторые 8 бит это buf[1]
    return true;
}

// Счетчик неудачных попыток связи. Если превышается установленный лимит попыток, то переводим в ошибку
static void INA219_HandleError(ina219_t *dev)
{
    dev->error_count++;
    if (dev->error_count >= INA219_MAX_ERRORS) {
        dev->is_valid = false;
    }
}

// Инициализация датчика
bool INA219_Init(ina219_t *dev, I2C_HandleTypeDef *hi2c)
{
    if (dev == NULL || hi2c == NULL) {
        return false;
    }

    dev->hi2c = hi2c;
    dev->dev_addr = INA219_I2C_ADDR;
    dev->is_valid = false;
    dev->error_count = 0;

    // Проверка готовности датчика
    if (HAL_I2C_IsDeviceReady(dev->hi2c, dev->dev_addr, 3, INA219_I2C_TIMEOUT_MS) != HAL_OK) {
        return false;
    }

    // Программный сброс перед настройкой
    if (!INA219_WriteReg16(dev, INA219_REG_CONFIG, INA219_CONFIG_RESET)) {
        return false;
    }
    HAL_Delay(2);

    // Для измерения токов до 3.2 А с разрешением 2^15 (3.2/2^15 = 0.1 мА (100 мкА на бит)) и шунтом 0.1 Ом (R100 на плате INA219)
    // в регистр калибровки записывается значение, расчитаное по формуле из даташита: 0.04096/(0.1 мА * 0.1 Ом) = 4096
    // Т.о. одна единица (младший бит) в регистре значения тока = 0.1 мА (Current_LSB),
    // а одна единица (младший бит) в регистре значения мощности = 2 мВт (Power_LSB) (т.к. по формуле из даташита Power_LSB = 20 * Current_LSB)
    if (!INA219_WriteReg16(dev, INA219_REG_CALIBRATION, INA219_CALIB_32V_2A)) {
        return false;
    }

    // Записываем в регистр конфигурации параметры по умолчанию
    if (!INA219_WriteReg16(dev, INA219_REG_CONFIG, INA219_CONFIG_DEFAULT)) {
        return false;
    }

    return true;
}

// Процесс измерения
void INA219_Process(ina219_t *dev)
{
    if (dev == NULL || dev->hi2c == NULL) {
        return;
    }

    // Автовосстановление при обрыве связи
    if (!dev->is_valid && dev->error_count >= INA219_MAX_ERRORS) {
        if (!INA219_Init(dev, dev->hi2c)) {
            return;
        }
    }

    uint16_t raw_bus = 0;
    uint16_t raw_current = 0;
    uint16_t raw_power = 0;

    // Читаем значения регистров, содержащих значения напряжения на шине, тока и мощности 
    if (!INA219_ReadReg16(dev, INA219_REG_BUS_V,   &raw_bus)      || 
        !INA219_ReadReg16(dev, INA219_REG_CURRENT, &raw_current)  || 
        !INA219_ReadReg16(dev, INA219_REG_POWER,   &raw_power)) {
        
        INA219_HandleError(dev);
        return;
    }

    // Для получения реального значения напряжение шины значение из регистра(13 бит (с 15 по 3)) умножаем на шаг 4 мВ (32В/2^13)
    dev->bus_voltage_v = (float)(raw_bus >> 3) * 0.004f;

    // Для получения реального значения тока умножаем значение из регистра на 0.1 мА (Current_LSB) 
    dev->current_ma = (float)((int16_t)raw_current) * 0.1f;

    // Для получения реального значения мощности умножаем значение из регистра на 2 мВт (Power_LSB)
    dev->power_mw = (float)raw_power * 2.0f;

    // Небольшая фильтрация шума нуля (если ток < 0.5 мА без нагрузки) 
    if (dev->current_ma > -0.5f && dev->current_ma < 0.5f) {
        dev->current_ma = 0.0f;
        dev->power_mw = 0.0f;
    }

    dev->error_count = 0;
    dev->is_valid = true;
}