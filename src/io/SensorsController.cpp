/**
 * @file SensorsController.cpp
 * @brief Реализация контроллера сенсоров (DS18B20 + ADC напряжение).
 *
 * Память/устойчивость:
 * - Горячий цикл `update()` не должен аллоцировать heap.
 * - OneWire/Dallas живут в этом файле (публичный заголовок их не тянет).
 * - Чтение DS18B20 асинхронное (конверсия занимает время) — не блокируем loop.
 *
 * Запрещено:
 * - Делать `String` операции внутри циклов опроса.
 * - Увеличивать статические буферы без явной необходимости.
 */
#include "io/SensorsController.h"
#include "config/Config.h"
#include "config/SensorRomResolve.h"
#include "core/Core.h"
#include "core/ErrorManager.h"
#include "common/Logger.h"
#include "common/Constants.h"
#include "io/RpmEstimator.h"
#include "common/ErrorCodes.h"

#include <OneWire.h>
#include <DallasTemperature.h>

namespace {

struct SensorsOwBackend {
    OneWire bus;
    DallasTemperature dallas;
    SensorsOwBackend() : bus(Pin::ONEWIRE), dallas(&bus) {}
};

// Изделие с одним контроллером: стек Ow локален для этого файла (заголовок include/io/SensorsController.h остаётся тонким).
SensorsOwBackend& owBackend() {
    static SensorsOwBackend ow;
    return ow;
}

/// Медиана первых n элементов (n ≥ 1). Копия на стеке — Adc::SAMPLES мало.
uint16_t medianU16_(const uint16_t* src, uint8_t n) {
    uint16_t tmp[Adc::SAMPLES];
    if (n == 0 || n > Adc::SAMPLES) return 0;
    memcpy(tmp, src, sizeof(uint16_t) * n);
    for (uint8_t i = 1; i < n; i++) {
        const uint16_t key = tmp[i];
        int8_t j = static_cast<int8_t>(i) - 1;
        while (j >= 0 && tmp[j] > key) {
            tmp[j + 1] = tmp[j];
            j--;
        }
        tmp[j + 1] = key;
    }
    return tmp[n / 2];
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
    if (_owTimeoutStreak == Sensors::OW_TIMEOUT_FAIL_STREAK) {
        core.getErrorManager().set(ErrorCode::ONEWIRE_BUS_ERR);
    }
}

void SensorsController::noteAdcReadFail_() {
    if (_adcSatStreak < 255) _adcSatStreak++;
    if (_adcSatStreak == Sensors::ADC_SAT_FAIL_STREAK) {
        core.getErrorManager().set(ErrorCode::ADC_READ_FAIL);
    }
}

void SensorsController::clearSensorErrorIf_(ErrorCode code) {
    core.getErrorManager().clear(code);
}

void SensorsController::noteTemperature_(uint8_t index, float celsius) {
    if (!valueLogDue(_loggedTemp[index], celsius, DS18B20::LOG_BAND_C, _tempHave[index])) return;
    _tempHave[index] = true;
    _loggedTemp[index] = celsius;
    logger.log(LogModule::SensorsController, "sensor %u %.1f C\n", (unsigned)index, celsius);
}

void SensorsController::noteTemperatureLost_(uint8_t index) {
    if (!_tempHave[index]) return;
    _tempHave[index] = false;
    logger.log(LogModule::SensorsController, "sensor %u invalid\n", (unsigned)index);
}

void SensorsController::noteVoltage_(float volts) {
    if (!valueLogDue(_loggedVoltage, volts, Adc::LOG_BAND_V, _voltageHave)) return;
    _voltageHave = true;
    _loggedVoltage = volts;
    logger.log(LogModule::SensorsController, "voltage %.2f V\n", volts);
}

void SensorsController::noteVoltageLost_() {
    if (!_voltageHave) return;
    _voltageHave = false;
    logger.log(LogModule::SensorsController, "voltage invalid\n");
}

void SensorsController::forgetSensors_() {
    _sensorCount = 0;
    _conversionInProgress = false;
    _owInvalidStreak = 0;
    _owTimeoutStreak = 0;
    for (uint8_t i = 0; i < HardwareLimits::SENSORS; i++) {
        _sensorData[i].valid = false;
        _sensorData[i].temperature = DS18B20::DISCONNECTED;
        _tempHave[i] = false;
        memset(_foundAddresses[i], 0, 8);
        memset(_sensorData[i].address, 0, 8);
    }
}

/// MATCH + READ SCRATCHPAD: ROM из конфига считается живым только если CRC scratchpad ок.
bool probeRomPresent_(OneWire& bus, const uint8_t addr[8]) {
    if (!bus.reset()) return false;
    bus.select(addr);
    bus.write(0xBE);
    uint8_t sp[9];
    for (uint8_t i = 0; i < 9; i++) sp[i] = bus.read();
    return OneWire::crc8(sp, 8) == sp[8];
}

uint8_t SensorsController::seedConfiguredRoms() {
    // Только ROM, которые реально отвечают на шине (не «слепая» подстановка из JSON).
    uint8_t count = 0;
    auto& bus = owBackend().bus;
    for (uint8_t i = 0; i < HardwareLimits::SENSORS; i++) {
        const char* rom = config.getBase().sensors[i].rom;
        uint8_t addr[8];
        if (!parseSensorRomString(rom, addr)) continue;
        if (OneWire::crc8(addr, 7) != addr[7]) {
            logger.log(LogModule::SensorsController, "config ROM CRC bad slot %u\n", (unsigned)i);
            continue;
        }
        if (!probeRomPresent_(bus, addr)) continue;
        memcpy(_foundAddresses[count], addr, 8);
        memcpy(_sensorData[count].address, addr, 8);
        count++;
        core.feedWatchdog();
        core.cooperate();
    }
    return count;
}

uint8_t SensorsController::bindSensors() {
    // 1) ROM из конфига, если мэтчатся на шине.
    // 2) Иначе (и для незанятых слотов) — SEARCH.
    uint8_t count = seedConfiguredRoms();
    if (count >= HardwareLimits::SENSORS) return count;

    DeviceAddress addr;
    auto& ow = owBackend();
    ow.bus.reset_search();
    while (count < HardwareLimits::SENSORS && ow.bus.search(addr)) {
        core.feedWatchdog();
        core.cooperate();
        if (OneWire::crc8(addr, 7) != addr[7]) continue;
        bool have = false;
        for (uint8_t j = 0; j < count; j++) {
            if (memcmp(_foundAddresses[j], addr, 8) == 0) { have = true; break; }
        }
        if (have) continue;
        memcpy(_foundAddresses[count], addr, 8);
        memcpy(_sensorData[count].address, addr, 8);
        count++;
    }
    return count;
}

void SensorsController::begin() {
    logger.log(LogModule::SensorsController, "start\n");
    // Подтяжка на стороне мастера: у эмулятора свой pull-up слабый (GPIO0).
    pinMode(Pin::ONEWIRE, INPUT_PULLUP);

    // Делитель АКБ даёт на АЦП ~0–1 В; ослабление 0 дБ ≈ полная шкала 0–1.1 В на ESP32-C3.
    analogSetAttenuation(ADC_0db);
    analogReadResolution(Adc::RESOLUTION_BITS);

    auto& ow = owBackend();
    ow.dallas.begin();
    _sensorCount = bindSensors();

    logger.log(LogModule::SensorsController, "DS18B20 sensors bound: %u\n", _sensorCount);
    printAllAddresses();

    for (uint8_t i = 0; i < _sensorCount; i++) {
        ow.dallas.setResolution(_foundAddresses[i], DS18B20::RESOLUTION);
    }

    requestTemperatures();

    logger.log(LogModule::SensorsController, "VoltageMonitor ok\n");
    updateVoltage();

    // Выводим конфиг датчиков (ROM+имя) из конфига
    for (uint8_t i = 0; i < HardwareLimits::SENSORS; i++) {
        const char* rom = config.getBase().sensors[i].rom;
        const char* name = config.getBase().sensors[i].name;
        if ((rom && rom[0]) || (name && name[0])) {
            logger.log(LogModule::SensorsController, "config sensor %d: rom=%s name=%s coef=%.2f\n",
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

    // Повторная привязка, если датчиков ещё нет (эмулятор подняли позже) или шина «умерла».
    if (_sensorCount == 0) {
        if (_lastTemperatureRequest != 0 && (now - _lastTemperatureRequest < tempInterval)) return;
        _lastTemperatureRequest = now;
        _sensorCount = bindSensors();
        if (_sensorCount == 0) return;
        for (uint8_t i = 0; i < HardwareLimits::SENSORS; i++) _tempHave[i] = false;
        logger.log(LogModule::SensorsController, "DS18B20 sensors bound: %u\n", _sensorCount);
        printAllAddresses();
        auto& ow = owBackend();
        for (uint8_t i = 0; i < _sensorCount; i++) {
            ow.dallas.setResolution(_foundAddresses[i], DS18B20::RESOLUTION);
        }
    }

    auto& dallas = owBackend().dallas;

    if (!_conversionInProgress &&
        (now - _lastTemperatureRequest >= tempInterval)) {
        // DS18B20 конвертирует температуру не мгновенно. Запускаем конверсию и вернёмся за результатом позже.
        requestTemperatures();
        _lastTemperatureRequest = now;
    }

    if (_conversionInProgress && dallas.isConversionComplete()) {
        uint8_t ok = 0;
        for (uint8_t i = 0; i < _sensorCount; i++) {
            float temp = dallas.getTempC(_foundAddresses[i]);
            if (temp != DS18B20::DISCONNECTED) {
                ok++;
                // Калибровка датчика по совпадению ROM (не по индексу слота).
                float coeff = 0.0f;
                char romStr[TextBytes::Sensors::ADDR_STRING];
                formatSensorRomString(_foundAddresses[i], romStr, sizeof(romStr));
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
                noteTemperature_(i, _sensorData[i].temperature);
            } else {
                _sensorData[i].valid = false;
                noteTemperatureLost_(i);
            }
            _sensorData[i].lastReadTime = now;
        }
        // Уступка после полного прохода 1-Wire — не между MATCH/READ.
        yield();
        _conversionInProgress = false;
        if (ok > 0) {
            _owTimeoutStreak = 0;
            _owInvalidStreak = 0;
            clearSensorErrorIf_(ErrorCode::ONEWIRE_BUS_ERR);
        } else {
            if (_owInvalidStreak < 255) _owInvalidStreak++;
            // Несколько пустых циклов подряд → сброс и повторная привязка (часто после сбоя SEARCH).
            if (_owInvalidStreak >= Sensors::OW_TIMEOUT_FAIL_STREAK) {
                logger.log(LogModule::SensorsController, "DS18B20 all invalid, rebind\n");
                core.getErrorManager().set(ErrorCode::ONEWIRE_BUS_ERR);
                forgetSensors_();
            }
        }
    }

    if (_conversionInProgress && (now - _conversionStartTime > DS18B20::CONVERSION_TIMEOUT_MS)) {
        logger.log(LogModule::SensorsController, "temperature conversion timeout\n");
        _conversionInProgress = false;
        for (uint8_t i = 0; i < _sensorCount; i++) {
            _sensorData[i].valid = false;
            noteTemperatureLost_(i);
        }
        if (_sensorCount > 0) {
            noteOneWireBusError_();
            if (_owTimeoutStreak >= Sensors::OW_TIMEOUT_FAIL_STREAK) {
                logger.log(LogModule::SensorsController, "DS18B20 bus timeout, rebind\n");
                forgetSensors_();
            }
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
        formatSensorRomString(_foundAddresses[i], romStr, sizeof(romStr));
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

void SensorsController::printAllAddresses() const {
    for (uint8_t i = 0; i < _sensorCount; i++) {
        char addrStr[TextBytes::Sensors::ADDR_STRING];
        formatSensorRomString(_foundAddresses[i], addrStr, sizeof(addrStr));
        logger.log(LogModule::SensorsController, "  sensor %u: %s\n", i, addrStr);
    }
}

void SensorsController::updateVoltage() {
    uint32_t now = millis();
    const uint32_t interval =
        _pollIdle ? Timing::VOLTAGE_READ_INTERVAL_IDLE_MS : Timing::VOLTAGE_READ_INTERVAL_MS;
    if (now - _lastVoltageRead < interval) return;
    _lastVoltageRead = now;

    // Пачка чтений за тик: остаточная пульсация ШИМ эмулятора / наводка на IO0.
    uint32_t burstSum = 0;
    for (uint8_t i = 0; i < Adc::OVERSAMPLES; i++) {
        burstSum += analogRead(Pin::VBAT);
    }
    const uint16_t raw = static_cast<uint16_t>(burstSum / Adc::OVERSAMPLES);

    _voltageBuffer[_voltageIndex] = raw;
    _voltageIndex = (_voltageIndex + 1) % Adc::SAMPLES;
    if (_adcFillCount < Adc::SAMPLES) _adcFillCount++;

    // Медиана заполненной части окна — выбросы не тянут среднее.
    const uint8_t n = _adcFillCount;
    uint16_t window[Adc::SAMPLES];
    if (n >= Adc::SAMPLES) {
        memcpy(window, _voltageBuffer, sizeof(_voltageBuffer));
    } else {
        // Ещё не полный круг: первые n записей с индекса 0…n-1 после сброса,
        // либо компактно собрать последние n из кольца.
        for (uint8_t i = 0; i < n; i++) {
            const uint8_t idx = static_cast<uint8_t>((_voltageIndex + Adc::SAMPLES - n + i) % Adc::SAMPLES);
            window[i] = _voltageBuffer[idx];
        }
    }
    const uint16_t med = (n > 0) ? medianU16_(window, n) : 0;
    const bool rail = (n == 0) || (med == 0) || (med >= (uint16_t)Adc::MAX_RAW);
    float instant = calculateVoltage(med);

    // Evidence-EMA сглаживает медиану; deadband/commit смотрят на неё, не на один тик.
    // |Δ| ≥ FAST_TRACK_V — сразу (стартер / шаг vbat); иначе COMMIT_TICKS при |Δ| ≥ COMMIT_V.
    if (!rail) {
        if (!_filtHave) {
            _evidence = instant;
            _filtVolts = instant;
            _filtHave = true;
            _voltCommitStreak = 0;
        } else {
            _evidence += Adc::EVIDENCE_ALPHA * (instant - _evidence);
            const float d = _evidence - _filtVolts;
            const float ad = (d >= 0.f) ? d : -d;
            const bool fast = ad >= Adc::FAST_TRACK_V;
            if (ad < Adc::DEADBAND_V) {
                _voltCommitStreak = 0;
            } else if (fast || ad >= Adc::COMMIT_V) {
                if (_voltCommitStreak < 255) _voltCommitStreak++;
                const uint8_t need = fast ? 1 : Adc::COMMIT_TICKS;
                if (_voltCommitStreak >= need) {
                    const float dtSec = static_cast<float>(interval) * 0.001f;
                    const float maxStep =
                        (d > 0.f) ? (Adc::SLEW_RISE_V_PER_S * dtSec) : (Adc::SLEW_FALL_V_PER_S * dtSec);
                    if (ad <= maxStep) _filtVolts = _evidence;
                    else _filtVolts += (d > 0.f) ? maxStep : -maxStep;
                }
            } else {
                _voltCommitStreak = 0;
            }
        }
    }

    const uint16_t filteredRaw =
        (!rail && _filtHave && config.getBase().vehicle.adc_voltage_coeff > 0.f)
            ? static_cast<uint16_t>(_filtVolts / config.getBase().vehicle.adc_voltage_coeff + 0.5f)
            : med;
    _lastAdcAvg = filteredRaw;
    _voltageData.voltage = _filtHave ? _filtVolts : instant;
    _voltageData.valid = !rail && _filtHave;
    _voltageData.lastReadTime = now;
    if (_voltageData.valid) noteVoltage_(_voltageData.voltage);
    else if (n > 0) noteVoltageLost_();

    // Устойчивый упор/обрыв АЦП после полного окна фильтра — только липкая диагностика.
    if (_adcFillCount >= Adc::SAMPLES) {
        const bool saturated = (raw == 0) || (raw >= (uint16_t)Adc::MAX_RAW);
        if (saturated) {
            noteAdcReadFail_();
        } else {
            _adcSatStreak = 0;
            clearSensorErrorIf_(ErrorCode::ADC_READ_FAIL);
        }
    }
}

void SensorsController::resetVoltageWindow() {
    _voltageIndex = 0;
    _adcFillCount = 0;
    // Slew не сбрасываем: после прокрутки напряжение уже «здесь», копим только окно доверия.
    // Принудить выборку на следующем update(), чтобы новое окно начало заполняться сразу.
    _lastVoltageRead = millis() - Timing::VOLTAGE_READ_INTERVAL_IDLE_MS;
}

float SensorsController::calculateVoltage(uint16_t raw) const {
    return raw * config.getBase().vehicle.adc_voltage_coeff;
}

bool SensorsController::computeAdcVoltageCoeff(float measuredVolts, float& outCoeff) const {
    // Почти нулевое сырое (редкое окно / обрыв пина) дало бы огромный коэффициент.
    constexpr uint16_t kMinAdcRawForCalibrate = 50;
    if (!(measuredVolts > 0.0f) || !isVoltageWindowFull() || _lastAdcAvg < kMinAdcRawForCalibrate) {
        return false;
    }
    outCoeff = measuredVolts / static_cast<float>(_lastAdcAvg);
    return true;
}
