// include/common/Pins.h
#pragma once

#include <Arduino.h>

/**
 * @file Pins.h
 * @brief Hardware pin map for ESP-C3-12F drop-in on the AutoStart ESP-12 PCB.
 *
 * Pad→GPIO remap (same PCB nets, ESP32-C3 chip numbers):
 * - UART TX/RX pads → GPIO21 / GPIO20 (SIM800 + debug Serial share UART0)
 * - Former GPIO16 pad → GPIO2 (RELAY5)
 * - Former GPIO15 pad → GPIO8 (RELAY4, C3 strapping: prefer HIGH at boot)
 * - Former GPIO0 pad → GPIO9 (1-Wire, C3 strapping: LOW = download)
 * - ADC pad → GPIO1
 */

namespace Pin {

    // GSM on UART0 pads (shared with debug Serial)
    constexpr uint8_t GSM_TX = 21;     // module TX pad → SIM800 RXD
    constexpr uint8_t GSM_RX = 20;     // module RX pad ← SIM800 TXD

    // 1-Wire bus (was ESP8266 GPIO0 pad → C3 GPIO9; keep HIGH at boot)
    constexpr uint8_t ONEWIRE = 9;

    // PCB button (was GPIO2 pad → C3 GPIO10)
    constexpr uint8_t BUTTON = 10;

    constexpr uint8_t INPUT_PINS[] = {
        5,       // IN1 (was GPIO13)
        4,       // IN2 (was GPIO12)
        BUTTON   // IN3
    };

    constexpr uint16_t INPUT_IDS[] = {
        1001,
        1002,
        1003
    };

    // Relays active HIGH
    constexpr uint8_t RELAY_PINS[] = {
        3,   // RELAY1 (was GPIO14)
        19,  // RELAY2 (was GPIO5)
        18,  // RELAY3 (was GPIO4)
        8,   // RELAY4 (was GPIO15) — C3 strap GPIO8
        2    // RELAY5 (was GPIO16)
    };

    constexpr uint16_t RELAY_IDS[] = {
        2001,
        2002,
        2003,
        2004,
        2005
    };

    static_assert(sizeof(INPUT_PINS) / sizeof(INPUT_PINS[0]) == sizeof(INPUT_IDS) / sizeof(INPUT_IDS[0]),
                  "INPUT_PINS and INPUT_IDS must have same length");
    static_assert(sizeof(RELAY_PINS) / sizeof(RELAY_PINS[0]) == sizeof(RELAY_IDS) / sizeof(RELAY_IDS[0]),
                  "RELAY_PINS and RELAY_IDS must have same length");

    // Battery sense (was A0 → C3 GPIO1 / ADC1_CH1); external 1:15 into ~0–1 V
    constexpr uint8_t VBAT = 1;

    constexpr uint8_t MAX_SENSORS = 3;

} // namespace Pin
