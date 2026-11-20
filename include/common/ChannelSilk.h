// include/common/ChannelSilk.h
#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <string.h>

#include "common/Constants.h"

/**
 * Публичный id канала — шёлк с единицы: K1…K5, IN1…IN3, T1…T3.
 * В RAM после разбора: номер 1…n (0 = нет ссылки). Индекс слота = номер − 1.
 * Кириллица и числа вроде 2001 не принимаются.
 */
namespace ChannelSilk {

enum class Kind : uint8_t { Relay, Input, Sensor };

/// Нет ссылки в конфиге / шаге.
constexpr uint8_t NONE = 0;

inline uint8_t countOf(Kind kind) {
    switch (kind) {
        case Kind::Relay: return HardwareLimits::RELAYS;
        case Kind::Input: return HardwareLimits::INPUTS;
        case Kind::Sensor: return HardwareLimits::SENSORS;
    }
    return 0;
}

inline const char* prefixOf(Kind kind) {
    switch (kind) {
        case Kind::Relay: return "K";
        case Kind::Input: return "IN";
        case Kind::Sensor: return "T";
    }
    return "";
}

/// Слот 0..n-1 → номер 1..n; неверный слот → NONE.
inline uint8_t numberFromSlot(uint8_t slot, Kind kind) {
    const uint8_t n = countOf(kind);
    if (slot >= n) return NONE;
    return static_cast<uint8_t>(slot + 1);
}

/// Номер 1..n → слот 0..n-1; NONE/мусор → -1.
inline int8_t slotFromNumber(uint8_t number, Kind kind) {
    if (number == NONE) return -1;
    const uint8_t n = countOf(kind);
    if (number < 1 || number > n) return -1;
    return static_cast<int8_t>(number - 1);
}

/**
 * Разбор "K3" / "IN1" / "T2" / "" → номер 1..n или NONE.
 * Не ASCII-латиница/цифры, старые 2001 и чужой префикс → false.
 */
inline bool parse(const char* s, Kind kind, uint8_t* outNumber) {
    if (!outNumber) return false;
    *outNumber = NONE;
    if (!s || !*s) return true; // пусто = нет ссылки

    const char* prefix = prefixOf(kind);
    const size_t plen = strlen(prefix);
    // Строгий ASCII: K / IN / T. Кириллица и иной регистр отклоняются.
    if (strncmp(s, prefix, plen) != 0) return false;

    const char* num = s + plen;
    if (!*num) return false;
    uint16_t v = 0;
    for (const char* p = num; *p; ++p) {
        if (*p < '0' || *p > '9') return false;
        v = static_cast<uint16_t>(v * 10u + static_cast<uint16_t>(*p - '0'));
        if (v > 255) return false;
    }
    if (v < 1 || v > countOf(kind)) return false;
    *outNumber = static_cast<uint8_t>(v);
    return true;
}

/// Номер → "K3" в out; NONE → пустая строка. outCap ≥ 5.
inline bool format(uint8_t number, Kind kind, char* out, size_t outCap) {
    if (!out || outCap == 0) return false;
    out[0] = '\0';
    if (number == NONE) return true;
    if (slotFromNumber(number, kind) < 0) return false;
    snprintf(out, outCap, "%s%u", prefixOf(kind), (unsigned)number);
    out[outCap - 1] = '\0';
    return true;
}

/// Запись JSON-строки id в Print (с кавычками). NONE → "".
inline void printQuotedId(Print& p, uint8_t number, Kind kind) {
    char buf[8];
    if (!format(number, kind, buf, sizeof(buf)) || buf[0] == '\0') {
        p.print("\"\"");
        return;
    }
    p.print('\"');
    p.print(buf);
    p.print('\"');
}

/// Ключ объекта JSON: "K3": (без пробела после двоеточия).
inline void printJsonKey(Print& p, uint8_t number, Kind kind) {
    char buf[8];
    if (!format(number, kind, buf, sizeof(buf)) || buf[0] == '\0') {
        p.print("\"\":");
        return;
    }
    p.print('\"');
    p.print(buf);
    p.print("\":");
}

} // namespace ChannelSilk
