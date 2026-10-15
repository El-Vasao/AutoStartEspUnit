// include/gsm/GsmTimeParse.h
#pragma once

#include <stdint.h>
#include <time.h>

/**
 * Разбор времени из ответов SIM800 (без Arduino, без TZ-состояния libc; тестируется на хосте).
 * Использует каскад синхронизации времени в GsmController.Ntp.cpp; см. docs/modules/gsm.md.
 */
namespace gsm_time {

/// Дней от 1970-01-01 до даты пролептического григорианского календаря.
int64_t daysFromCivil(int year, int month, int day);

/// Дата-время, заданное в UTC, в epoch. Поля вне диапазона (в том числе 31 апреля) — false.
/// Секунда 60 (високосная) допускается и переходит в следующую минуту.
bool civilToEpochUtc(int year, int month, int day, int hour, int minute, int second, time_t& out);

struct CclkTime {
    time_t utc{0};
    /// Пояс в четвертях часа от GMT, как его сообщил модем.
    int16_t tzQuarters{0};
    /// Пояс — целое число часов в [-12, +14] (только такие хранятся в настройках).
    bool tzWholeHours{false};
    int8_t tzHours{0};
};

/// `+CCLK: "yy/MM/dd,hh:mm:ss±zz"` → UTC. Пояс вне [-48, +56] четвертей — ошибка разбора.
bool parseCclk(const char* line, CclkTime& out);

/// `+CIPGSMLOC: 0,yyyy/MM/dd,hh:mm:ss` (UTC) → epoch. Ненулевой код ответа или «несвежая» дата — false.
bool parseCipgsmloc(const char* line, time_t& utcOut);

/// Отсекает заводские часы модема без NITZ (например 04/01/01): не раньше TimeSync::MIN_SANE_EPOCH_UTC.
bool epochSane(time_t utc);

/// Часовой пояс в четвертях часа для AT+CNTP, ограничен ±48.
int8_t tzHoursToQuarters(int8_t tzOffsetHours);

} // namespace gsm_time
