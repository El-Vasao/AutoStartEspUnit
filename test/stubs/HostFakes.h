#pragma once

// Флаги исполнителя для нативных тестов MQTT: isRunning() инлайновый, isInStarterStep() — заглушка.
struct ProgramExecutorTestFlags {
    static void set(bool running, bool inStarter);
};
