/**
 * @file SensorsController.h
 * @brief Публичный интерфейс контроллера сенсоров (DS18B20 + ADC).
 *
 * Инварианты по памяти:
 * - Публичный заголовок не тянет OneWire/DallasTemperature (чтобы не раздувать компиляцию).
 * - Горячие методы (`update`) не должны требовать динамических аллокаций.
 *
 * Запрещено:
 * - Добавлять сюда инклюды “для удобства” (только то, что реально нужно по типам/полям).
 */
#pragma once

#include <Arduino.h>
#include "common/Pins.h"
#include "common/Constants.h"
#include "common/ErrorCodes.h"

/// Адрес ROM / 1-Wire (8 байт). Сырые байты, чтобы публичные заголовки не тянули типы Dallas.
using SensorRomAddress = uint8_t[8];

// Структура для хранения данных одного температурного датчика
struct TemperatureSensorData {
    float temperature = DS18B20::DISCONNECTED;    ///< последнее измеренное значение
    uint32_t lastReadTime = 0;       ///< время последнего чтения (мс)
    bool valid = false;              ///< флаг валидности последнего значения
    SensorRomAddress address = {0};  ///< уникальный адрес датчика
};

/**
 * @brief Контроллер сенсоров (температура и напряжение АКБ).
 *
 * Температура:
 * - DS18B20 читаются асинхронно: `requestTemperatures()` запускает конверсию, а чтение происходит,
 *   когда конверсия завершена (`isConversionComplete`) или по таймауту.
 *
 * Напряжение:
 * - oversample + медиана + EMA evidence, затем deadband/commit и асимметричный slew.
 * - `voltage = raw × vehicle.adc_voltage_coeff` (калибровка по измеренному напряжению).
 */
class SensorsController {
public:
    SensorsController();

    // Инициализация, поиск датчиков температуры, первый запрос
    void begin();

    // Обновление показаний (вызывается в loop)
    void update();

    /// Если истина — более длинные интервалы ADC/DS18B20 (нет программы и нет сессий UI).
    void setPollIdle(bool idle) { _pollIdle = idle; }
    bool isPollIdle() const { return _pollIdle; }

    // ---- Температура ----

    // Запустить преобразование температуры (асинхронно)
    void requestTemperatures();

    // Получить последнее значение температуры для датчика (по индексу)
    float getTemperature(uint8_t index) const;

    // Проверить, валидно ли последнее значение для датчика
    bool isTemperatureValid(uint8_t index) const;

    // Время последнего чтения для датчика
    uint32_t getLastTemperatureTime(uint8_t index) const;

    // Количество обнаруженных датчиков
    uint8_t getSensorCount() const { return _sensorCount; }

    // ROM-адрес датчика по индексу (8 байт) или nullptr, если индекс вне диапазона найденных.
    const uint8_t* getSensorAddress(uint8_t index) const {
        return (index < _sensorCount) ? _foundAddresses[index] : nullptr;
    }

    // Найти индекс датчика по ROM-строке ("AA:BB:...") среди найденных. Возвращает -1, если не найден.
    int8_t findSensorIndexByRom(const char* rom) const;

    // Вывести все адреса (отладка)
    void printAllAddresses() const;

    // ---- Напряжение АКБ ----

    // Последнее измеренное напряжение
    float getVoltage() const { return _voltageData.voltage; }

    // Валидно ли последнее измерение напряжения
    bool isVoltageValid() const { return _voltageData.valid; }

    // Время последнего измерения напряжения
    uint32_t getLastVoltageTime() const { return _voltageData.lastReadTime; }

    /// Сбросить набранные выборки (например просадку при прокрутке); среднее начинается со следующего чтения.
    void resetVoltageWindow();

    /// Истина, когда в окне Adc::SAMPLES чтений после последнего сброса.
    bool isVoltageWindowFull() const { return _adcFillCount >= Adc::SAMPLES; }

#ifdef UNIT_TEST
    void testSetVoltage(float volts, bool valid) {
        _voltageData.voltage = volts;
        _voltageData.valid = valid;
    }
    void testSetWindowFull(bool full) { _adcFillCount = full ? Adc::SAMPLES : 0; }
#endif

    /// Вывести `adc_voltage_coeff` из показания мультиметра. Берёт только закрытое сырое значение;
    /// false, если фильтр не готов, сырое слишком мало или measuredVolts ≤ 0.
    bool computeAdcVoltageCoeff(float measuredVolts, float& outCoeff) const;

private:
    TemperatureSensorData _sensorData[HardwareLimits::SENSORS]; ///< данные датчиков
    SensorRomAddress _foundAddresses[HardwareLimits::SENSORS];  ///< адреса найденных датчиков
    uint8_t _sensorCount;                           ///< количество найденных датчиков

    bool _conversionInProgress;                     ///< флаг ожидания преобразования
    uint32_t _conversionStartTime;                   ///< время запуска преобразования (мс)
    uint32_t _lastTemperatureRequest;                ///< время последнего запроса (мс)
    bool _pollIdle{false};                           ///< растянутые интервалы опроса
    uint8_t _owTimeoutStreak{0};                     ///< подряд таймауты конверсии DS18B20
    uint8_t _adcSatStreak{0};                        ///< подряд насыщенные или нулевые выборки АЦП
    bool _tempHave[HardwareLimits::SENSORS]{};      ///< температура датчика уже была в журнале
    float _loggedTemp[HardwareLimits::SENSORS]{};   ///< последняя записанная температура
    bool _voltageHave{false};                       ///< напряжение уже было в журнале
    float _loggedVoltage{0.f};                      ///< последнее записанное напряжение

    // Обновление температуры (опрос датчиков)
    void updateTemperatures();

    /// Сначала MATCH по ROM из конфига (только живые), затем SEARCH на оставшиеся слоты.
    uint8_t seedConfiguredRoms();
    uint8_t discoverSensors();
    uint8_t bindSensors();
    void forgetSensors_();

    // Напряжение
    struct VoltageData {
        float voltage;          ///< последнее измеренное напряжение
        uint32_t lastReadTime;  ///< время последнего измерения (мс)
        bool valid;             ///< флаг валидности
    } _voltageData;

    uint16_t _voltageBuffer[Adc::SAMPLES];  ///< буфер короткой медианы
    uint8_t _voltageIndex;                   ///< текущий индекс в буфере
    uint32_t _lastVoltageRead;                ///< время последнего чтения АЦП (мс)
    uint8_t _adcFillCount{0};                 ///< выборки, набранные к полному окну фильтра
    uint16_t _lastAdcAvg{0};                  ///< последнее отфильтрованное сырое для калибровки
    float _evidence{0.f};                     ///< EMA медианы (вход deadband/commit)
    float _filtVolts{0.f};                    ///< стабилизированное напряжение (deadband + slew)
    bool _filtHave{false};
    uint8_t _voltCommitStreak{0};             ///< тики с |Δ| ≥ COMMIT_V (или 1 при fast-track)
    uint8_t _owInvalidStreak{0};              ///< подряд циклы, где ни один DS18B20 не ответил

    // Обновление напряжения (oversample → медиана → evidence → deadband/slew)
    void updateVoltage();

    // Расчёт напряжения из сырого значения АЦП
    float calculateVoltage(uint16_t raw) const;

    void noteOneWireBusError_();
    void noteAdcReadFail_();
    void clearSensorErrorIf_(ErrorCode code);
    void noteTemperature_(uint8_t index, float celsius);
    void noteTemperatureLost_(uint8_t index);
    void noteVoltage_(float volts);
    void noteVoltageLost_();
};
