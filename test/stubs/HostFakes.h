#pragma once

#include <stdint.h>
#include <string.h>
#include <time.h>

#include "program/CompiledStep.h"

class ProgramExecutor;

// Флаги исполнителя для нативных тестов MQTT: isRunning() инлайновый.
// overrideStarter подменяет isInStarterStep(), пока настоящий автомат не запущен.
struct ProgramExecutorTestFlags {
    static void set(bool running, bool inStarter);
    static void set(ProgramExecutor& executor, uint8_t id, bool running, bool inStarter);
    static inline bool overrideStarter = false;
    static inline bool inStarter = false;
};

// Шаги, которые Config::loadProgramCompiled отдаёт нативным тестам исполнителя.
struct ProgramTestFeed {
    static inline bool active = false;
    static inline CompiledStep steps[Limits::MAX_STEPS_PER_PROGRAM]{};
    static inline uint8_t count = 0;
    static inline char name[TextBytes::Programs::NAME]{};

    static void reset() {
        active = false;
        count = 0;
        name[0] = '\0';
    }

    static void set(const CompiledStep* src, uint8_t n, const char* programName) {
        count = n < Limits::MAX_STEPS_PER_PROGRAM ? n : Limits::MAX_STEPS_PER_PROGRAM;
        if (src && count) memcpy(steps, src, sizeof(CompiledStep) * count);
        active = count > 0;
        if (programName) {
            strncpy(name, programName, sizeof(name) - 1);
            name[sizeof(name) - 1] = '\0';
        } else {
            name[0] = '\0';
        }
    }
};

// Счётчики заглушек start()/stop(), пока в нативной сборке нет настоящего ProgramExecutor.
struct ExecutorTestProbe {
    static inline bool intercept = false;
    static inline int starts = 0;
    static inline int stops = 0;
    static inline uint8_t lastStartId = 0;
    static inline bool startOk = true;
    static void reset() {
        starts = 0;
        stops = 0;
        lastStartId = 0;
        startOk = true;
    }
};

struct SensorsTestProbe {
    static inline int windowResets = 0;
};

struct TimeTestProbe {
    static inline struct tm local{};
    static inline bool brokenOk = true;
    static void reset() {
        local = tm{};
        brokenOk = true;
    }
};

struct CoreTestProbe {
    static inline bool engineRunning = false;
};
