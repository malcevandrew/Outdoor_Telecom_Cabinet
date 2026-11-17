#include "bmp280.h"

#define BMP280_I2C_ADDR  (0x77U << 1)   // 7 битный адрес датчика(сразу смещен влево на 1 бит, для добавления бита чтения/записи функцией HAL)

#define BMP280_REG_ID           0xD0U   // Адрес регистра с ID чипа  
#define BMP280_REG_RESET        0xE0U   // Адрес регистра для аппаратной перезагрузки датчика
#define BMP280_REG_CTRL_MEAS    0xF4U   // Адрес регистра для настройки передискретизации (oversampling) и режима измерения(power mode)
#define BMP280_REG_CONFIG       0xF5U   // Адрес регистра для настройки параметров фильтрации и периода ожидания между измерениями
#define BMP280_REG_PRESS_MSB    0xF7U   // Адрес первого из 5 регистров, содержащих значение давления и температуры (0xF7..0xF9) (первые 20 бит давление, вторые 20 температура)
#define BMP280_REG_CALIB_START  0x88U   // Адрес первого из 24 регистров содержащих калибровочные значения (0x88..0xA1)

#define BMP280_CHIP_ID          0x58U   // ID чипа по даташиту
#define BMP280_RESET_VALUE      0xB6U   // Значение команды для выполнения аппаратной перезагрузки датчика 
#define BMP280_I2C_TIMEOUT_MS   20U     // Тайматут для функций записи и чтения
#define BMP280_MAX_ERRORS       3U      // Количество неудачных попыток связи до перехода в состояние ошибки


// Счетчик неудачных попыток связи. Если превышается установленный лимит попыток, то переводим в ошибку
static void BMP280_HandleError(bmp280_t *dev)
{
    dev->error_count++;
    if (dev->error_count >= BMP280_MAX_ERRORS) {
        dev->is_valid = false;
    }
}

// Вычисление значения давления по алгоритму из даташита Bosch
static void BMP280_Pressure_Calculation_from_Bosch(bmp280_t *dev,  int32_t adc_P, int32_t adc_T)
{
    // Формула компенсации температуры Bosch (нужна для расчёта t_fine)
    int32_t var1_t = ((((adc_T >> 3) - ((int32_t)dev->calib.dig_T1 << 1))) * ((int32_t)dev->calib.dig_T2)) >> 11;
    int32_t var2_t = (((((adc_T >> 4) - ((int32_t)dev->calib.dig_T1)) * ((adc_T >> 4) - ((int32_t)dev->calib.dig_T1))) >> 12) *
                     ((int32_t)dev->calib.dig_T3)) >> 14;
    int32_t t_fine = var1_t + var2_t;
    dev->temperature_c = (float)((t_fine * 5 + 128) >> 8) / 100.0f;

    // 64-битная формула компенсации давления Bosch (высокая точность) 
    int64_t var1_p = ((int64_t)t_fine) - 128000;
    int64_t var2_p = var1_p * var1_p * (int64_t)dev->calib.dig_P6;
    var2_p = var2_p + ((var1_p * (int64_t)dev->calib.dig_P5) << 17);
    var2_p = var2_p + (((int64_t)dev->calib.dig_P4) << 35);
    var1_p = ((var1_p * var1_p * (int64_t)dev->calib.dig_P3) >> 8) + ((var1_p * (int64_t)dev->calib.dig_P2) << 12);
    var1_p = (((((int64_t)1) << 47) + var1_p)) * ((int64_t)dev->calib.dig_P1) >> 33;

    // Предотвращение деления на 0
    if (var1_p == 0) {
        BMP280_HandleError(dev);
        return;
    }

    int64_t p = 1048576 - adc_P;
    p = (((p << 31) - var2_p) * 3125) / var1_p;
    var1_p = (((int64_t)dev->calib.dig_P9) * (p >> 13) * (p >> 13)) >> 25;
    var2_p = (((int64_t)dev->calib.dig_P8) * p) >> 19;
    p = ((p + var1_p + var2_p) >> 8) + (((int64_t)dev->calib.dig_P7) << 4);

    float pressure_pa = (float)p / 256.0f;
    dev->pressure_hpa  = pressure_pa / 100.0f;
    dev->pressure_mmhg = dev->pressure_hpa * 0.750062f;
}

// Инициализация датчика атмосферного давления
bool BMP280_Init(bmp280_t *dev, I2C_HandleTypeDef *hi2c)
{
    if (dev == NULL || hi2c == NULL) {
        return false;
    }

    dev->hi2c = hi2c;
    dev->dev_addr = BMP280_I2C_ADDR;
    dev->is_valid = false;
    dev->error_count = 0;

    // Проверка ID чипа (должен быть 0x58)
    uint8_t chip_id = 0;
    if (HAL_I2C_Mem_Read(dev->hi2c, dev->dev_addr, BMP280_REG_ID, I2C_MEMADD_SIZE_8BIT, &chip_id, 1, BMP280_I2C_TIMEOUT_MS) != HAL_OK) {
        return false;
    }
    if (chip_id != BMP280_CHIP_ID) {
        return false;
    }

    // Чтение 24 байт калибровочных коэффициентов необходимых для вычисления давления
    uint8_t cal_buf[24] = {0};
    if (HAL_I2C_Mem_Read(dev->hi2c, dev->dev_addr, BMP280_REG_CALIB_START, I2C_MEMADD_SIZE_8BIT, cal_buf, 24, BMP280_I2C_TIMEOUT_MS) != HAL_OK) {
        return false;
    }

    dev->calib.dig_T1 = (uint16_t)((cal_buf[1] << 8) | cal_buf[0]);
    dev->calib.dig_T2 = (int16_t)((cal_buf[3] << 8)  | cal_buf[2]);
    dev->calib.dig_T3 = (int16_t)((cal_buf[5] << 8)  | cal_buf[4]);
    dev->calib.dig_P1 = (uint16_t)((cal_buf[7] << 8) | cal_buf[6]);
    dev->calib.dig_P2 = (int16_t)((cal_buf[9] << 8)  | cal_buf[8]);
    dev->calib.dig_P3 = (int16_t)((cal_buf[11] << 8) | cal_buf[10]);
    dev->calib.dig_P4 = (int16_t)((cal_buf[13] << 8) | cal_buf[12]);
    dev->calib.dig_P5 = (int16_t)((cal_buf[15] << 8) | cal_buf[14]);
    dev->calib.dig_P6 = (int16_t)((cal_buf[17] << 8) | cal_buf[16]);
    dev->calib.dig_P7 = (int16_t)((cal_buf[19] << 8) | cal_buf[18]);
    dev->calib.dig_P8 = (int16_t)((cal_buf[21] << 8) | cal_buf[20]);
    dev->calib.dig_P9 = (int16_t)((cal_buf[23] << 8) | cal_buf[22]);

    // Настройка фильтрации и периода ожидания между измерениями: t_sb = 250 мс (0x03), IIR filter = x4 (0x02) 
    uint8_t config_reg = (0x03U << 5) | (0x02U << 2);
    if (HAL_I2C_Mem_Write(dev->hi2c, dev->dev_addr, BMP280_REG_CONFIG, I2C_MEMADD_SIZE_8BIT, &config_reg, 1, BMP280_I2C_TIMEOUT_MS) != HAL_OK) {
        return false;
    }

    // Настройка передискретизации (oversampling): для температуры osrs_t = x2 (0x02), для давления osrs_p = x8 (0x04) и режим mode = Normal (0x03) 
    uint8_t ctrl_meas = (0x02U << 5) | (0x04U << 2) | 0x03U;
    if (HAL_I2C_Mem_Write(dev->hi2c, dev->dev_addr, BMP280_REG_CTRL_MEAS, I2C_MEMADD_SIZE_8BIT, &ctrl_meas, 1, BMP280_I2C_TIMEOUT_MS) != HAL_OK) {
        return false;
    }

    return true;
}

// Получение сырых значений с датчика и конвертация в готовые физические величины по алгоритму из даташита 
void BMP280_Process(bmp280_t *dev)
{
    if (dev == NULL || dev->hi2c == NULL) {
        return;
    }

    // Если датчик в ошибке, пробуем переинициализировать его
    if (!dev->is_valid && dev->error_count >= BMP280_MAX_ERRORS) {
        if (!BMP280_Init(dev, dev->hi2c)) {
            return;
        }
    }

    // Пакетное чтение 6 байт (давление 0xF7..0xF9 и температура 0xFA..0xFC)
    uint8_t raw[6] = {0};
    if (HAL_I2C_Mem_Read(dev->hi2c, dev->dev_addr, BMP280_REG_PRESS_MSB, I2C_MEMADD_SIZE_8BIT, raw, 6, BMP280_I2C_TIMEOUT_MS) != HAL_OK) {
        BMP280_HandleError(dev);
        return;
    }

    // Собираем сырые 20 битные занчения температуры и давления
    int32_t adc_P = (int32_t)(((uint32_t)raw[0] << 12) | ((uint32_t)raw[1] << 4) | ((uint32_t)raw[2] >> 4));
    int32_t adc_T = (int32_t)(((uint32_t)raw[3] << 12) | ((uint32_t)raw[4] << 4) | ((uint32_t)raw[5] >> 4));

    // Вычисляем значение давления по алгоритму из даташита
    BMP280_Pressure_Calculation_from_Bosch(dev, adc_P, adc_T);

    dev->error_count = 0;
    dev->is_valid = true;
}