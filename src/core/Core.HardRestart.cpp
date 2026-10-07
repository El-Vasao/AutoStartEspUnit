#include "core/CoreHardRestart.h"
#include "common/EspHal.h"
#include "common/Constants.h"

#include <WiFi.h>

[[noreturn]] __attribute__((noinline)) void core_hard_restart_now() {
    espHalFeedWdt();
#ifdef SERIAL_DEBUG
    Serial.flush();
#endif
    // После тяжёлого FS/JSON стека даём SDK один проход перед system_restart().
    yield();
    espHalFeedWdt();
    ESP.restart();
    while (true) {
        espHalFeedWdt();
        delayMicroseconds(Delays::HARD_RESTART_SPIN_US);
    }
}
