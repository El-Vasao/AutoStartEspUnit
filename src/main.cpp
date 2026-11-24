// src/main.cpp
#include <Arduino.h>
#include "core/Core.h"
#include "common/Logger.h"
#include "common/Version.h"

void setup() {
    // UART-журнал уже поднят в initVariant() (до loopTask); здесь только привязка задачи цикла.
    logger.begin();
    logger.log(LogModule::Core, "setup\n");

    if (!core.begin()) {
        logger.log(LogModule::Core, "begin failed; loop continues for diagnostics\n");
    }
}

void loop() {
    core.update();
}
