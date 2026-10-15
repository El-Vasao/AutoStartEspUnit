// include/common/Utils.h
#pragma once

#include <Arduino.h>
#include <FS.h>
#include <stddef.h>
#include <stdint.h>

#include "common/Constants.h"
#include "common/Crc16.h"
#include "common/EspHal.h"
#include "common/TimeMath.h"

/**
 * @brief Утилиты, которым нужны ФС или Wi-Fi. Чистые части — в Crc16.h и TimeMath.h
 * (подключаются здесь же, чтобы существующие include не менялись).
 */

/// CRC по открытому файлу; на длинных чтениях периодически отдаёт управление (espHalFeedWdt()).
uint16_t crc16ModbusStreamFile(File& f);

/// SSID вида `<prefix>XXXXXX` из трёх младших байтов MAC, без heap.
void generateUniqueSSID(char* dst, size_t dstLen, const char* prefix = ApConfig::SSID_PREFIX);

// Короткие атомарные блоки (данные, общие с ISR).
#define ATOMIC_START() noInterrupts()
#define ATOMIC_END() interrupts()
