#pragma once

// Нативная заглушка FreeRTOS: портовый spinlock заменён std::mutex.
// Нужна только OtaHandler.Core.cpp (кольцо HTTP → цикл) в тестах test_ota_stream.
#include <mutex>

struct portMUX_TYPE {
    std::mutex m;
};
#define portMUX_INITIALIZER_UNLOCKED {}

inline void hostCriticalEnter(portMUX_TYPE* mux) { mux->m.lock(); }
inline void hostCriticalExit(portMUX_TYPE* mux) { mux->m.unlock(); }
#define taskENTER_CRITICAL(mux) hostCriticalEnter(mux)
#define taskEXIT_CRITICAL(mux) hostCriticalExit(mux)
