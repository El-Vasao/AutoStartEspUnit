#include "core/CoreHardRestart.h"
#include "common/EspHal.h"
#include "common/Constants.h"
#include "common/Logger.h"

#include <WiFi.h>

[[noreturn]] __attribute__((noinline)) void coreHardRestartNow() {
    espHalFeedWdt();
    logger.flushUart();
    // После тяжёлого FS/JSON стека даём SDK один проход перед system_restart().
    yield();
    espHalFeedWdt();
    ESP.restart();
    while (true) {
        espHalFeedWdt();
        delayMicroseconds(Delays::HARD_RESTART_SPIN_US);
    }
}
