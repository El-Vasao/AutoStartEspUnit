// src/main.cpp
#include <Arduino.h>
#include "core/Core.h"
#include "common/Logger.h"
#include "common/Version.h"

void setup() {
    logger.begin();

    core.begin();
}

void loop() {
    core.update();
}
