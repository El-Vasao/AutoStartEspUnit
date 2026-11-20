#pragma once

#include <stdint.h>
#include <stddef.h>

/**
 * Внедрение зависимостей без виртуальных методов и кучи, удобное для встраиваемых.
 *
 * Схема:
 * - Модули хранят `AppPorts` (или подмножество) как обычные данные.
 * - `core` (корень композиции) связывает указатели на функции с настоящими методами.
 * - Без vtable, без RTTI, без std::function.
 */

struct WdtPort {
    void* ctx{nullptr};
    void (*feed)(void* ctx){nullptr};
    void (*cooperate)(void* ctx){nullptr};  // yield / отдача кванта времени

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
    /// false, если программу не удалось запустить.
    bool (*startProgram)(void* ctx, uint8_t programId){nullptr};
    void (*stopProgram)(void* ctx){nullptr};
    void (*startOtaUpdate)(void* ctx){nullptr};
    void (*factoryReset)(void* ctx){nullptr};

    void (*setThermostat)(void* ctx, bool en){nullptr};
    void (*setBatterySaver)(void* ctx, bool en){nullptr};
    /// false, если id не найден.
    bool (*setInputRuntime)(void* ctx, uint16_t id, bool en){nullptr};
    bool (*setInputTrigger)(void* ctx, uint16_t id, bool en){nullptr};
    bool (*setTempTrigger)(void* ctx, uint16_t id, bool en){nullptr};
    /// Пробуждение SoftAP из NORMAL_SILENT в NORMAL. false, если режим не тихий или enabled==false.
    bool (*wakeWifiAp)(void* ctx, bool en){nullptr};
    void (*requestModemReboot)(void* ctx){nullptr};
};

/// Потоковое OTA из обработчиков HTTP-загрузки (подача безопасна для async; смена режима отложена в Core::update).
struct OtaStreamPort {
    void* ctx{nullptr};
    void (*onUploadOpened)(void* ctx){nullptr};
    bool (*feed)(void* ctx, const uint8_t* data, size_t len){nullptr};
    bool (*finish)(void* ctx){nullptr};
    void (*abort)(void* ctx){nullptr};
    void (*notifyComplete)(void* ctx, bool ok){nullptr};
    bool (*isOtaMode)(void* ctx){nullptr};
};

struct AppPorts {
    WdtPort wdt;
    AppControlPort control;
    OtaStreamPort ota;
};
