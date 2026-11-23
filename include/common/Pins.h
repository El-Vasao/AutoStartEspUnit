// include/common/Pins.h
#pragma once

#include <Arduino.h>

/**
 * @file Pins.h
 * @brief Исчерпывающая карта выводов платы AutoStart на модуле XH-C3F
 *        (форм-фактор ESP-12E/F). Единственный источник правды по пинам.
 *
 * Не путать с Ai-Thinker ESP-C3-12F — у XH другая раскладка IO на тех же площадках.
 *
 * ---------------------------------------------------------------------------
 * Цепи платы (как в исходном списке ESP-8266 → сейчас XH-C3F IO)
 * ---------------------------------------------------------------------------
 *
 *   ESP-32-C3 (XH-C3F)  --> SIM800C (SIM800L)
 *   TXD (IO21)          --> RXD
 *   RXD (IO20)          --> TXD
 *   (UART0 общий с отладочным Serial)
 *
 *   1-Wire (DS18B20, DS2413) - IO9     // strap C3: LOW = download
 *   Button on PCB / IN3       - IO8     // strap C3; active_state LOW (на GND)
 *   IN1                       - IO5
 *   IN2                       - IO4
 *
 *   распиновка реле (active HIGH → ULN2003):
 *   K1-  IO3
 *   K2-  IO19
 *   K3-  IO18
 *   K4-  IO10
 *   K5-  IO1
 *
 *   VBAT (делитель 1:15 → ~0–1 В) - IO0 / ADC1_CH0
 *
 * ---------------------------------------------------------------------------
 * Площадки модуля ESP-12 ↔ XH-C3F (левый / правый ряд)
 * ---------------------------------------------------------------------------
 *
 *   Площадка ESP-12     XH-C3F     Цепь на плате AutoStart
 *   RST                 EN         enable модуля (не GPIO); у XH EN сверху слева
 *   ADC                 IO0        VBAT
 *   EN                  IO2        не используется прошивкой (бывш. EN у 12E)
 *   GPIO16              IO1        K5
 *   GPIO14              IO3        K1
 *   GPIO12              IO4        IN2
 *   GPIO13              IO5        IN1
 *   VCC                 3V3        питание
 *   TXD0                IO21       GSM TX (MCU→SIM800)
 *   RXD0                IO20       GSM RX (SIM800→MCU)
 *   GPIO5               IO19       K2
 *   GPIO4               IO18       K3
 *   GPIO0               IO9        1-Wire
 *   GPIO2               IO8        Button / IN3
 *   GPIO15              IO10       K4
 *   GND                 GND        земля
 *
 */

namespace Pin {

    // GSM на площадках UART0 (общий с отладочным Serial)
    constexpr uint8_t GSM_TX = 21;     // TXD → SIM800 RXD
    constexpr uint8_t GSM_RX = 20;     // RXD ← SIM800 TXD

    // Шина 1-Wire
    constexpr uint8_t ONEWIRE = 9;

    // Кнопка платы = IN3
    constexpr uint8_t BUTTON = 8;

    // Порядок = публичный id IN1…INn (ChannelSilk).
    constexpr uint8_t INPUT_PINS[] = {
        5,       // IN1
        4,       // IN2
        BUTTON   // IN3
    };

    // Порядок = публичный id K1…Kn (ChannelSilk). Реле, активный уровень HIGH.
    constexpr uint8_t RELAY_PINS[] = {
        3,   // K1
        19,  // K2
        18,  // K3
        10,  // K4
        1    // K5
    };

    // Измерение батареи; внешний делитель 1:15 в ~0–1 В
    constexpr uint8_t VBAT = 0;


    constexpr uint8_t MAX_SENSORS =
     3;

} // namespace Pin
