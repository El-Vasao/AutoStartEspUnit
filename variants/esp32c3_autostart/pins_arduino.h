#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>
#include "soc/soc_caps.h"

// Variant for ESP-C3-12F on AutoStart PCB (not DevKitM).
// Stock esp32c3 pins_arduino.h binds GPIO8 to NeoPixel/RGB_BUILTIN and
// defaults Wire to SDA=8/SCL=9 — that fights K4 (GPIO8) and 1-Wire (GPIO9).

static const uint8_t LED_BUILTIN = SOC_GPIO_PIN_COUNT + 10; // no on-module RGB; unused sentinel
#define BUILTIN_LED LED_BUILTIN
#define LED_BUILTIN LED_BUILTIN

static const uint8_t TX = 21;
static const uint8_t RX = 20;

// Keep defaults off relay / 1-Wire / button pads (8, 9, 10).
static const uint8_t SDA = 6;
static const uint8_t SCL = 7;

static const uint8_t SS = 7;
static const uint8_t MOSI = 6;
static const uint8_t MISO = 5;
static const uint8_t SCK = 4;

static const uint8_t A0 = 0;
static const uint8_t A1 = 1;
static const uint8_t A2 = 2;
static const uint8_t A3 = 3;
static const uint8_t A4 = 4;
static const uint8_t A5 = 5;

#endif /* Pins_Arduino_h */
