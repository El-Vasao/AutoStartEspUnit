/**
 * @file SensorsController.h
 * @brief Публичный интерфейс контроллера сенсоров (DS18B20 + ADC).
 *
 * Инварианты по памяти:
 * - Публичный заголовок не должен тянуть лишние зависимости (особенно `Config.h` и тяжёлые JSON-DOM библиотеки),
 *   чтобы не раздувать include-граф и время компиляции.
 * - Горячие методы (`update`) не должны требовать динамических аллокаций.
 *
 * Запрещено:
 * - Добавлять сюда инклюды “для удобства” (только то, что реально нужно по типам/полям).
 */
// include/io/SensorsController.h
#pragma once

#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "common/Pins.h"
#include "common/Constants.h"

// Структура для хранения данных одного температурного датчика
struct TemperatureSensorData {
    float temperature = -127.0f;    ///< последнее измеренное значение
    uint32_t lastReadTime = 0;       ///< время последнего чтения (мс)
    bool valid = false;              ///< флаг валидности последнего значения
    DeviceAddress address = {0};     ///< уникальный адрес датчика
};

/**
 * @brief Контроллер сенсоров (температура и напряжение АКБ).
 *
 * Температура:
 * - DS18B20 читаются асинхронно: `requestTemperatures()` запускает конверсию, а чтение происходит,
 *   когда конверсия завершена (`isConversionComplete`) или по таймауту.
 *
 * Напряжение:
 * - чтение АЦП с фильтрацией (скользящее среднее) для подавления шумов.
 * - коэффициент калибровки задаётся в конфиге (`vehicle.adc_voltage_coeff`).
 */
class SensorsController {
public:
    SensorsController();

    // Инициализация, поиск датчиков температуры, первый запрос
    void begin();

    // Обновление показаний (вызывается в loop)
    void update();

    /// When true, use longer ADC/DS18B20 intervals (no program / no UI sessions).
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

    // Вывести адрес датчика (отладка)
    void printAddress(uint8_t index) const;

    // Вывести все адреса (отладка)
    void printAllAddresses() const;

    // ---- Напряжение АКБ ----

    // Последнее измеренное напряжение
    float getVoltage() const { return _voltageData.voltage; }

    // Валидно ли последнее измерение напряжения
    bool isVoltageValid() const { return _voltageData.valid; }

    // Время последнего измерения напряжения
    uint32_t getLastVoltageTime() const { return _voltageData.lastReadTime; }

private:
    // Температура
    OneWire _oneWire;                              ///< объект OneWire
    DallasTemperature _sensors;                    ///< объект DallasTemperature

    TemperatureSensorData _sensorData[HardwareLimits::SENSORS]; ///< данные датчиков
    DeviceAddress _foundAddresses[HardwareLimits::SENSORS];     ///< адреса найденных датчиков
    uint8_t _sensorCount;                           ///< количество найденных датчиков

    bool _conversionInProgress;                     ///< флаг ожидания преобразования
    uint32_t _conversionStartTime;                   ///< время запуска преобразования (мс)
    uint32_t _lastTemperatureRequest;                ///< время последнего запроса (мс)
    bool _pollIdle{false};                           ///< растянутые интервалы опроса

    // Обновление температуры (опрос датчиков)
    void updateTemperatures();

    // Поиск датчиков на шине
    uint8_t discoverSensors();

    // Напряжение
    struct VoltageData {
        float voltage;          ///< последнее измеренное напряжение
        uint32_t lastReadTime;  ///< время последнего измерения (мс)
        bool valid;             ///< флаг валидности
    } _voltageData;

    uint16_t _voltageBuffer[ADC::SAMPLES];  ///< буфер для скользящего среднего
    uint8_t _voltageIndex;                   ///< текущий индекс в буфере
    uint32_t _lastVoltageRead;                ///< время последнего чтения АЦП (мс)

    // Обновление напряжения (чтение АЦП, скользящее среднее)
    void updateVoltage();

    // Расчёт напряжения из сырого значения АЦП
    float calculateVoltage(uint16_t raw) const;
};

