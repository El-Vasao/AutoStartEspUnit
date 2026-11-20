// include/common/Pins.h
#pragma once

#include <Arduino.h>

/**
 * @file Pins.h
 * @brief Карта выводов ESP-C3-12F, встающего на место платы AutoStart ESP-12.
 *
 * Переназначение площадка→GPIO (те же цепи платы, номера выводов ESP32-C3):
 * - площадки UART TX/RX → GPIO21 / GPIO20 (SIM800 и отладочный Serial делят UART0)
 * - бывшая площадка GPIO16 → GPIO2 (RELAY5)
 * - бывшая площадка GPIO15 → GPIO8 (RELAY4, страп C3: при старте лучше HIGH)
 * - бывшая площадка GPIO0 → GPIO9 (1-Wire, страп C3: LOW = загрузка)
 * - площадка АЦП → GPIO1
 */

namespace Pin {

    // GSM на площадках UART0 (общий с отладочным Serial)
    constexpr uint8_t GSM_TX = 21;     // площадка TX модуля → SIM800 RXD
    constexpr uint8_t GSM_RX = 20;     // площадка RX модуля ← SIM800 TXD

    // Шина 1-Wire (была площадка ESP8266 GPIO0 → C3 GPIO9; при старте держать HIGH)
    constexpr uint8_t ONEWIRE = 9;

    // Кнопка платы (была площадка GPIO2 → C3 GPIO10)
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
        8,   // K4 (была GPIO15) — страп C3 GPIO8
        2    // K5 (была GPIO16)
    };

    // Измерение батареи (был A0 → C3 GPIO1 / ADC1_CH1); внешний делитель 1:15 в ~0–1 В
    constexpr uint8_t VBAT = 1;

    constexpr uint8_t MAX_SENSORS = 3;

} // namespace Pin
