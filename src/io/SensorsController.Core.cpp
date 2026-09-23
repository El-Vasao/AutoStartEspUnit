/**
 * @file SensorsController.Core.cpp
 * @brief Реализация контроллера сенсоров (DS18B20 + ADC напряжение).
 *
 * Память/устойчивость:
 * - Горячий цикл `update()` не должен аллоцировать heap.
 * - OneWire/Dallas живут в этом TU (публичный заголовок их не тянет).
 * - Чтение DS18B20 асинхронное (конверсия занимает время) — не блокируем loop.
 *
 * Запрещено:
 * - Делать `String` операции внутри циклов опроса.
 * - Увеличивать статические буферы без явной необходимости.
 */
#include "io/SensorsController.h"
#include "config/Config.h"
#include "core/Core.h"
#include "core/ErrorManager.h"
#include "common/Logger.h"
#include "common/Constants.h"
#include "common/ErrorCodes.h"

#include <OneWire.h>
#include <DallasTemperature.h>

namespace {

constexpr uint8_t kOwTimeoutFailStreak = 3;
constexpr uint8_t kAdcSatFailStreak = 8;

struct SensorsOwBackend {
    OneWire bus;
    DallasTemperature dallas;
    SensorsOwBackend() : bus(Pin::ONEWIRE), dallas(&bus) {}
};

// Single-controller product: Ow stack is TU-local (keeps include/io/SensorsController.h thin).
SensorsOwBackend& owBackend() {
    static SensorsOwBackend ow;
    return ow;
}

} // namespace

SensorsController::SensorsController()
    : _sensorCount(0)
    , _conversionInProgress(false)
    , _conversionStartTime(0)
    , _lastTemperatureRequest(0)
    , _voltageData{0, 0, false}
    , _voltageIndex(0)
    , _lastVoltageRead(0)
{
    memset(_voltageBuffer, 0, sizeof(_voltageBuffer));
    memset(_foundAddresses, 0, sizeof(_foundAddresses));
    memset(_sensorData, 0, sizeof(_sensorData));
}

void SensorsController::noteOneWireBusError_() {
    if (_owTimeoutStreak < 255) _owTimeoutStreak++;
    if (_owTimeoutStreak == kOwTimeoutFailStreak) {
        core.getErrorManager().set(ErrorCode::ONEWIRE_BUS_ERR);
    }
}

void SensorsController::noteAdcReadFail_() {
    if (_adcSatStreak < 255) _adcSatStreak++;
    if (_adcSatStreak == kAdcSatFailStreak) {
        core.getErrorManager().set(ErrorCode::ADC_READ_FAIL);
    }
}

void SensorsController::clearSensorErrorIf_(ErrorCode code) {
    if (core.getErrorManager().get() == code) {
        core.getErrorManager().clear();
    }
}

void SensorsController::begin() {
    logger.log("[SensorsController] begin\n");
    pinMode(Pin::ONEWIRE, INPUT);

    // Battery divider outputs ~0–1 V into ADC; 0 dB atten ≈ 0–1.1 V full-scale on ESP32-C3.
    analogSetAttenuation(ADC_0db);
    analogReadResolution(12);

    auto& ow = owBackend();
    ow.dallas.begin();
    _sensorCount = discoverSensors();

    logger.log("[SensorsController] Found %u DS18B20 sensors\n", _sensorCount);
    printAllAddresses();

    for (uint8_t i = 0; i < _sensorCount; i++) {
        ow.dallas.setResolution(_foundAddresses[i], DS18B20::RESOLUTION);
    }

    requestTemperatures();

    logger.log("[SensorsController] VoltageMonitor OK\n");
    updateVoltage();
    if (_voltageData.valid) {
        logger.log("[SensorsController] Initial voltage: %.2fV\n", _voltageData.voltage);
    }

    // Выводим конфиг датчиков (ROM+имя) из конфига
    for (uint8_t i = 0; i < HardwareLimits::SENSORS; i++) {
        const char* rom = config.getBase().sensors[i].rom;
        const char* name = config.getBase().sensors[i].name;
        if ((rom && rom[0]) || (name && name[0])) {
            logger.log("[SensorsController] Config sensor %d: rom=%s name=%s coeff=%.2f\n",
                       i,
                       (rom && rom[0]) ? rom : "—",
                       (name && name[0]) ? name : "—",
                       config.getBase().sensors[i].coeff);
        }
    }
}

void SensorsController::update() {
    updateTemperatures();
    updateVoltage();
}

void SensorsController::updateTemperatures() {
    uint32_t now = millis();
    const uint32_t tempInterval =
        _pollIdle ? Timing::TEMPERATURE_READ_INTERVAL_IDLE_MS : Timing::TEMPERATURE_READ_INTERVAL_MS;

    auto& dallas = owBackend().dallas;

    if (!_conversionInProgress &&
        (now - _lastTemperatureRequest >= tempInterval)) {
        // DS18B20 конвертирует температуру не мгновенно. Запускаем конверсию и вернёмся за результатом позже.
        requestTemperatures();
        _lastTemperatureRequest = now;
    }

    if (_conversionInProgress && dallas.isConversionComplete()) {
        for (uint8_t i = 0; i < _sensorCount; i++) {
            float temp = dallas.getTempC(_foundAddresses[i]);
            if (temp != DS18B20::DISCONNECTED) {
                // Apply per-sensor calibration by ROM match (not by slot index).
                float coeff = 0.0f;
                char romStr[TextBytes::Sensors::ADDR_STRING];
                snprintf(romStr, sizeof(romStr), "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
                         _foundAddresses[i][0], _foundAddresses[i][1], _foundAddresses[i][2], _foundAddresses[i][3],
                         _foundAddresses[i][4], _foundAddresses[i][5], _foundAddresses[i][6], _foundAddresses[i][7]);
                for (uint8_t j = 0; j < HardwareLimits::SENSORS; j++) {
                    const char* cfgRom = config.getBase().sensors[j].rom;
                    if (cfgRom[0] == '\0') continue;
                    if (strcmp(cfgRom, romStr) == 0) {
                        coeff = config.getBase().sensors[j].coeff;
                        break;
                    }
                }
                _sensorData[i].temperature = temp + coeff;
                _sensorData[i].valid = true;
            } else {
                _sensorData[i].valid = false;
            }
            _sensorData[i].lastReadTime = now;
            // Short yield between sensors so SoftAP/lwIP stay responsive.
            yield();
        }
        _conversionInProgress = false;
        _owTimeoutStreak = 0;
        clearSensorErrorIf_(ErrorCode::ONEWIRE_BUS_ERR);
    }

    if (_conversionInProgress && (now - _conversionStartTime > DS18B20::CONVERSION_TIMEOUT_MS)) {
        logger.log("[SensorsController] Temperature conversion timeout\n");
        _conversionInProgress = false;
        if (_sensorCount > 0) {
            noteOneWireBusError_();
        }
    }
}

void SensorsController::requestTemperatures() {
    if (_sensorCount == 0) return;
    owBackend().dallas.requestTemperatures();
    _conversionInProgress = true;
    _conversionStartTime = millis();
}

uint8_t SensorsController::discoverSensors() {
    uint8_t count = 0;
    DeviceAddress addr;
    auto& ow = owBackend();

    ow.bus.reset_search();
    while (ow.bus.search(addr) && count < HardwareLimits::SENSORS) {
        core.feedWatchdog();
        core.cooperate();
        if (OneWire::crc8(addr, 7) == addr[7]) {
            memcpy(_foundAddresses[count], addr, 8);
            memcpy(_sensorData[count].address, addr, 8);
            count++;
        }
    }
    return count;
}

float SensorsController::getTemperature(uint8_t index) const {
    if (index >= HardwareLimits::SENSORS) return DS18B20::DISCONNECTED;
    return _sensorData[index].temperature;
}

int8_t SensorsController::findSensorIndexByRom(const char* rom) const {
    if (!rom || !rom[0]) return -1;
    for (uint8_t i = 0; i < _sensorCount; i++) {
        char romStr[TextBytes::Sensors::ADDR_STRING];
        snprintf(romStr, sizeof(romStr), "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
                 _foundAddresses[i][0], _foundAddresses[i][1], _foundAddresses[i][2], _foundAddresses[i][3],
                 _foundAddresses[i][4], _foundAddresses[i][5], _foundAddresses[i][6], _foundAddresses[i][7]);
        if (strcmp(rom, romStr) == 0) {
            return (int8_t)i;
        }
        yield();
    }
    return -1;
}

bool SensorsController::isTemperatureValid(uint8_t index) const {
    if (index >= HardwareLimits::SENSORS) return false;
    return _sensorData[index].valid;
}

uint32_t SensorsController::getLastTemperatureTime(uint8_t index) const {
    if (index >= HardwareLimits::SENSORS) return 0;
    return _sensorData[index].lastReadTime;
}

void SensorsController::printAddress(uint8_t index) const {
    (void)index;
    // Адреса выводятся через logger в printAllAddresses().
}

void SensorsController::printAllAddresses() const {
    for (uint8_t i = 0; i < _sensorCount; i++) {
        char addrStr[TextBytes::Sensors::ADDR_STRING];
        snprintf(addrStr, sizeof(addrStr), "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
                 _foundAddresses[i][0], _foundAddresses[i][1], _foundAddresses[i][2], _foundAddresses[i][3],
                 _foundAddresses[i][4], _foundAddresses[i][5], _foundAddresses[i][6], _foundAddresses[i][7]);
        logger.log("[SensorsController]   Sensor %u: %s\n", i, addrStr);
    }
}

void SensorsController::updateVoltage() {
    uint32_t now = millis();
    const uint32_t interval =
        _pollIdle ? Timing::VOLTAGE_READ_INTERVAL_IDLE_MS : Timing::VOLTAGE_READ_INTERVAL_MS;
    if (now - _lastVoltageRead < interval) return;
    _lastVoltageRead = now;

    uint16_t raw = analogRead(Pin::VBAT);

    _voltageBuffer[_voltageIndex] = raw;
    _voltageIndex = (_voltageIndex + 1) % ADC::SAMPLES;
    if (_adcFillCount < ADC::SAMPLES) _adcFillCount++;

    uint32_t sum = 0;
    for (uint8_t i = 0; i < ADC::SAMPLES; i++) {
        sum += _voltageBuffer[i];
    }
    uint16_t avg = sum / ADC::SAMPLES;

    _voltageData.voltage = calculateVoltage(avg);
    _voltageData.valid = true;
    _voltageData.lastReadTime = now;

    // Sustained rail/open ADC after the filter window is full — sticky diagnostic only.
    if (_adcFillCount >= ADC::SAMPLES) {
        const bool saturated = (raw == 0) || (raw >= (uint16_t)ADC::MAX_RAW);
        if (saturated) {
            noteAdcReadFail_();
        } else {
            _adcSatStreak = 0;
            clearSensorErrorIf_(ErrorCode::ADC_READ_FAIL);
        }
    }
}

float SensorsController::calculateVoltage(uint16_t raw) const {
    float coeff = config.getBase().vehicle.adc_voltage_coeff;
    return raw * (ADC::VREF / ADC::MAX_RAW) * ADC::DIVIDER_RATIO * coeff;
}
