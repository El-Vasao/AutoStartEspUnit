// Символы, которые MqttClient.Commands / GsmController.Voice+Core тянут из Core, Config и соседних TU.
// В прошивке их дают MqttClient.Core и остальные GSM/Core; здесь только то, без чего native не линкуется.
#include "Arduino.h"
#include "HostFakes.h"
#include "config/Config.h"
#include "core/Core.h"
#include "core/ErrorManager.h"
#include "gsm/GsmController.h"
#include "mqtt/MqttClient.h"
#include "program/ProgramExecutor.h"

namespace {
bool g_inStarterStep = false;
}

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

void Core::applyWallClockEpoch(uint32_t) {}

ProgramExecutor::ProgramExecutor()
    : _programId(0),
      _running(false),
      _currentStep(0),
      _rt{0, 0},
      _lastProgramId(0),
      _localSteps(nullptr),
      _localActions(nullptr),
      _localStepCount(0) {
    _currentProgramName[0] = '\0';
    _lastProgramName[0] = '\0';
}

bool ProgramExecutor::isInStarterStep() const { return g_inStarterStep; }

void ProgramExecutorTestFlags::set(bool running, bool inStarter) {
    ProgramExecutor& executor = core.getProgramExecutor();
    executor._running = running;
    g_inStarterStep = inStarter;
}

MqttClient::MqttClient(Client& client)
    : _netClient(client), _lastStatusPublish(0), _lastReconnectAttempt(0), _fsm(client) {
    _fsm.setPublishCallback(&MqttClient::onPublishThunk, this);
}

bool MqttClient::publishStatus(bool) { return false; }
bool MqttClient::publishProgramsBody_() { return false; }
bool MqttClient::publishHwMapBody_() { return false; }
bool MqttClient::publishTrigMapBody_() { return false; }

void GsmController::drainAtResult_() {}
void GsmController::timeFailStep_(const char*) {}
void GsmController::gsmResetSessionAfterStop() {}
bool GsmController::apnMatchesApplied_() const { return true; }
