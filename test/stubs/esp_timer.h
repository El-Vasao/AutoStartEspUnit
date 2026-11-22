#pragma once

// Подмена esp_timer для нативных тестов. Один слот: страховка стартера создаёт один таймер.
#include <stdint.h>

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1

typedef enum {
    ESP_TIMER_TASK = 0,
    ESP_TIMER_ISR = 1,
} esp_timer_dispatch_t;

struct esp_timer {
    int id;
};

typedef void (*esp_timer_cb_t)(void* arg);

typedef struct {
    esp_timer_cb_t callback;
    void* arg;
    esp_timer_dispatch_t dispatch_method;
    const char* name;
    bool skip_unhandled_events;
} esp_timer_create_args_t;

namespace test_esp_timer {

struct Slot {
    esp_timer_cb_t callback;
    void* arg;
    bool alive;
    bool armed;
    uint64_t timeoutUs;
    const char* name;
};

inline Slot& slot() {
    static Slot s{};
    return s;
}

inline void reset() { slot() = Slot{}; }

inline bool armed() { return slot().alive && slot().armed; }

inline uint64_t timeoutUs() { return slot().timeoutUs; }

inline void fire() {
    Slot& s = slot();
    if (!s.alive || !s.armed || !s.callback) return;
    s.callback(s.arg);
}

} // namespace test_esp_timer

inline esp_err_t esp_timer_create(const esp_timer_create_args_t* args, esp_timer** out) {
    static esp_timer handle{1};
    test_esp_timer::Slot& s = test_esp_timer::slot();
    s = test_esp_timer::Slot{};
    if (args) {
        s.callback = args->callback;
        s.arg = args->arg;
        s.name = args->name;
    }
    s.alive = true;
    if (out) *out = &handle;
    return ESP_OK;
}

inline esp_err_t esp_timer_stop(esp_timer*) {
    test_esp_timer::slot().armed = false;
    return ESP_OK;
}

inline esp_err_t esp_timer_start_once(esp_timer*, uint64_t timeout_us) {
    test_esp_timer::Slot& s = test_esp_timer::slot();
    s.armed = true;
    s.timeoutUs = timeout_us;
    return ESP_OK;
}
