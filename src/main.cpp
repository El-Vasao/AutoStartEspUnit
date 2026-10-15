// src/main.cpp
#include <Arduino.h>
#include "core/Core.h"
#include "common/Logger.h"
#include "common/Version.h"

void setup() {
    logger.begin();

    if (!core.begin()) {
        logger.log(LogModule::Core, "begin failed; loop continues for diagnostics\n");
    }
}

void loop() {
    core.update();
}
