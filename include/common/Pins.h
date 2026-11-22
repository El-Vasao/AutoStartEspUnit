// include/common/Pins.h
#pragma once

#include <Arduino.h>

/**
 * @file Pins.h
 * @brief Карта выводов ESP-C3-12F, встающего на место платы AutoStart ESP-12.
 *
 * Ai-Thinker ESP-12F pad → ESP-C3-12F GPIO (спека V1.1, SoftRF/Electrodragon):
 *   TXD/GPIO1   → GPIO21     RXD/GPIO3 → GPIO20
 *   GPIO16 (K5) → GPIO2      GPIO14 (K1) → GPIO3
 *   GPIO12 (IN2)→ GPIO4      GPIO13 (IN1)→ GPIO5
 *   GPIO15 (K4) → GPIO8      GPIO2 (IN3) → GPIO10
 *   GPIO0 (1w)  → GPIO9      GPIO5 (K2)  → GPIO19
 *   GPIO4 (K3)  → GPIO18     A0          → GPIO1
 *
 * K1=GPIO3 и K2=GPIO19 на поле подтверждают эту таблицу. Не путать с «оставить
 * те же номера, что у ESP8266» (GPIO2 кнопки ≠ GPIO2 на C3).
 */

namespace Pin {

    // GSM на площадках UART0 (общий с отладочным Serial)
    constexpr uint8_t GSM_TX = 21;     // площадка TX модуля → SIM800 RXD
    constexpr uint8_t GSM_RX = 20;     // площадка RX модуля ← SIM800 TXD

    // Шина 1-Wire (площадка ESP8266 GPIO0 → C3 GPIO9; strap: LOW = download)
    constexpr uint8_t ONEWIRE = 9;

    // Кнопка платы (площадка ESP8266 GPIO2 → C3 GPIO10)
    constexpr uint8_t BUTTON = 10;

    // Порядок = публичный id IN1…INn (ChannelSilk).
    constexpr uint8_t INPUT_PINS[] = {
        5,       // IN1 (была GPIO13)
        4,       // IN2 (была GPIO12)
        BUTTON   // IN3
    };

    // Порядок = публичный id K1…Kn (ChannelSilk). Реле, активный уровень HIGH.
    constexpr uint8_t RELAY_PINS[] = {
        3,   // K1 (была GPIO14)
        19,  // K2 (была GPIO5)
        18,  // K3 (была GPIO4)
        8,   // K4 (была GPIO15) — strap C3 GPIO8
        2    // K5 (была GPIO16) — strap C3 GPIO2
    };

    // Измерение батареи (был A0 → C3 GPIO1 / ADC1_CH1); внешний делитель 1:15 в ~0–1 В
    constexpr uint8_t VBAT = 1;

    constexpr uint8_t MAX_SENSORS = 3;

} // namespace Pin
