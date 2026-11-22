// Конструкторы и методы, которые BatterySaver вызывает, но настоящие TU сенсоров и часов
// в нативную сборку не входят (тянут OneWire и каскад модема).
#include "HostFakes.h"
#include "config/Config.h"
#include "core/TimeSyncManager.h"
#include "gsm/GsmController.h"
#include "io/SensorsController.h"

#include <string.h>

SensorsController::SensorsController()
    : _sensorCount(0),
      _conversionInProgress(false),
      _conversionStartTime(0),
      _lastTemperatureRequest(0),
      _voltageData{0, 0, false},
      _voltageIndex(0),
      _lastVoltageRead(0) {
    memset(_voltageBuffer, 0, sizeof(_voltageBuffer));
    memset(_foundAddresses, 0, sizeof(_foundAddresses));
    memset(_sensorData, 0, sizeof(_sensorData));
}

float SensorsController::getTemperature(uint8_t index) const {
    if (index >= HardwareLimits::SENSORS) return DS18B20::DISCONNECTED;
    return _sensorData[index].temperature;
}

bool SensorsController::isTemperatureValid(uint8_t index) const {
    if (index >= HardwareLimits::SENSORS) return false;
    return _sensorData[index].valid;
}

uint32_t SensorsController::getLastTemperatureTime(uint8_t index) const {
    if (index >= HardwareLimits::SENSORS) return 0;
    return _sensorData[index].lastReadTime;
}

void SensorsController::resetVoltageWindow() {
    _voltageIndex = 0;
    _adcFillCount = 0;
    SensorsTestProbe::windowResets++;
}

TimeSyncManager::TimeSyncManager(Config& config, GsmController& gsm) : _config(config), _gsm(gsm) {}

bool TimeSyncManager::localBrokenDown(struct tm& out) const {
    if (!_synced || !TimeTestProbe::brokenOk) return false;
    out = TimeTestProbe::local;
    return true;
}
