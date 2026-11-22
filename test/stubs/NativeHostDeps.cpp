// Символы, которые MqttClient.Commands / GsmController.Voice+Core тянут из Core, Config и соседних TU.
// В прошивке их дают MqttClient.Core и остальные GSM/Core; здесь только то, без чего native не линкуется.
// disconnect()/loop() — урезанное зеркало Core (идемпотентный online=false + тик FSM на слив).
#include "Arduino.h"
#include "HostFakes.h"
#include "config/Config.h"
#include "core/Core.h"
#include "core/ErrorManager.h"
#include "gsm/GsmController.h"
#include "mqtt/MqttClient.h"
#include "io/DigitalInputs.h"
#include "io/RelayController.h"
#include "io/SensorsController.h"
#include "program/ProgramExecutor.h"

HardwareSerial Serial;
Config config;
Core core;

Config::Config() : configCRC(0), loaded(false), lastLoadError_(ErrorCode::NONE) {}

Core::Core() = default;

void Core::logHeapSnapshot(const char*) const {}

void Core::feedWatchdog() {}

void Core::cooperate() {}

void Core::reboot() {}

void Core::exitOtaToNormalMode() {}

void Core::onOtaHttpUploadAwaitTimedOut() {}

ErrorManager& Core::getErrorManager() {
    static ErrorManager err;
    return err;
}

ProgramExecutor& Core::getProgramExecutor() {
    static ProgramExecutor executor;
    return executor;
}

RelayController& Core::getRelay() {
    static RelayController relays;
    return relays;
}

DigitalInputs& Core::getInputs() {
    static DigitalInputs inputs;
    return inputs;
}

SensorsController& Core::getSensors() {
    static SensorsController sensors;
    return sensors;
}

GsmController& Core::getGSM() {
    static GsmController gsm;
    return gsm;
}

void Core::setTriggerRuntime(uint8_t, bool) {}
void Core::setTempTriggerRuntime(uint8_t, bool) {}
void Core::setBatterySaverRuntime(bool) {}
void Core::setThermostatRuntime(bool) {}

void Core::applyWallClockEpoch(uint32_t) {}

bool Config::loadProgramCompiled(uint8_t, CompiledStep* outSteps, uint8_t maxSteps, uint8_t* outCount,
                                 char* outName, size_t outNameSize) {
    if (!ProgramTestFeed::active || !outSteps || !outCount) return false;
    if (ProgramTestFeed::count == 0 || ProgramTestFeed::count > maxSteps) return false;
    memcpy(outSteps, ProgramTestFeed::steps, sizeof(CompiledStep) * ProgramTestFeed::count);
    *outCount = ProgramTestFeed::count;
    if (outName && outNameSize) {
        strncpy(outName, ProgramTestFeed::name, outNameSize - 1);
        outName[outNameSize - 1] = '\0';
    }
    return true;
}

void ProgramExecutorTestFlags::set(bool running, bool inStarter) {
    ProgramExecutor& executor = core.getProgramExecutor();
    executor._running = running;
    overrideStarter = true;
    ProgramExecutorTestFlags::inStarter = inStarter;
}

void ProgramExecutorTestFlags::set(ProgramExecutor& executor, uint8_t id, bool running, bool inStarter) {
    executor._programId = id;
    executor._running = running;
    overrideStarter = true;
    ProgramExecutorTestFlags::inStarter = inStarter;
}

bool Core::isEngineRunning() const { return CoreTestProbe::engineRunning; }

MqttClient::MqttClient(Client& client)
    : _netClient(client), _lastStatusPublish(0), _lastReconnectAttempt(0), _fsm(client) {
    _fsm.setPublishCallback(&MqttClient::onPublishThunk, this);
}

bool MqttClient::publishStatus(bool) { return false; }
bool MqttClient::publishProgramsBody_() { return false; }
bool MqttClient::publishHwMapBody_() { return false; }
bool MqttClient::publishTrigMapBody_() { return false; }

void MqttClient::disconnect() {
    // Зеркало MqttClient.Core: повторный слив не кладёт второй online=false.
    if (_fsm.isDisconnectPending()) return;
    if (_fsm.isConnected() && _topicOnline[0]) {
        static constexpr uint8_t kFalse[] = {'f', 'a', 'l', 's', 'e'};
        (void)_fsm.publish(_topicOnline, kFalse, 5, true, true);
    }
    _fsm.requestDisconnect();
    _subscribeQueued = false;
    _mqttWasConnected = false;
}

void MqttClient::loop() {
    MqttFsmClient::Budgets b{};
    b.maxReadBytesPerTick = 512;
    b.maxWriteBytesPerTick = 1024;
    b.maxParseFramesPerTick = 8;
    b.maxMsPerTick = 0;
    _fsm.tick(b);
    // Слив: только качать FSM, без subscribe/birth (как в Core).
    if (_fsm.isDisconnectPending()) return;
}

void GsmController::drainAtResult_() {}
void GsmController::timeFailStep_(const char*) {}
