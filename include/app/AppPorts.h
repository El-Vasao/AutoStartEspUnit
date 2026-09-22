#pragma once

#include <stdint.h>

/**
 * Embedded-friendly dependency injection without virtuals/heap.
 *
 * Pattern:
 * - Modules store an `AppPorts` (or a subset) as plain data.
 * - `core` (composition root) wires function pointers to real methods.
 * - No vtable, no RTTI, no std::function.
 */

struct WdtPort {
    void* ctx{nullptr};
    void (*feed)(void* ctx){nullptr};
    void (*cooperate)(void* ctx){nullptr};  // yield/time-slicing

    inline void feedNow() const {
        if (feed) feed(ctx);
    }
    inline void cooperateNow() const {
        if (cooperate) cooperate(ctx);
    }
};

struct AppControlPort {
    void* ctx{nullptr};
    void (*requestReboot)(void* ctx, uint32_t delayMs){nullptr};
    /// Returns false if program could not start.
    bool (*startProgram)(void* ctx, uint8_t programId){nullptr};
    void (*stopProgram)(void* ctx){nullptr};
    void (*startOtaUpdate)(void* ctx){nullptr};
    void (*factoryReset)(void* ctx){nullptr};

    void (*setThermostat)(void* ctx, bool en){nullptr};
    void (*setBatterySaver)(void* ctx, bool en){nullptr};
    /// Returns false if id not found.
    bool (*setInputRuntime)(void* ctx, uint16_t id, bool en){nullptr};
    bool (*setInputTrigger)(void* ctx, uint16_t id, bool en){nullptr};
    bool (*setTempTrigger)(void* ctx, uint16_t id, bool en){nullptr};
};

struct AppPorts {
    WdtPort wdt;
    AppControlPort control;
};
