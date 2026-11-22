#pragma once

#include <stdint.h>

typedef int gpio_num_t;
typedef int gpio_mode_t;
typedef int gpio_pullup_t;
typedef int gpio_pulldown_t;
typedef int gpio_int_type_t;
typedef int gpio_drive_cap_t;
typedef int esp_err_t;

#ifndef ESP_OK
#define ESP_OK 0
#endif

#define GPIO_MODE_INPUT_OUTPUT 3
#define GPIO_PULLUP_DISABLE 0
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_INTR_DISABLE 0
#define GPIO_DRIVE_CAP_3 3

typedef struct {
    uint64_t pin_bit_mask;
    gpio_mode_t mode;
    gpio_pullup_t pull_up_en;
    gpio_pulldown_t pull_down_en;
    gpio_int_type_t intr_type;
} gpio_config_t;

inline void gpio_reset_pin(gpio_num_t) {}
inline void gpio_hold_dis(gpio_num_t) {}
inline esp_err_t gpio_config(const gpio_config_t*) { return ESP_OK; }
inline esp_err_t gpio_set_drive_capability(gpio_num_t, gpio_drive_cap_t) { return ESP_OK; }
inline esp_err_t gpio_set_level(gpio_num_t, uint32_t) { return ESP_OK; }
