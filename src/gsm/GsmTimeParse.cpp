// src/gsm/GsmTimeParse.cpp
#include "gsm/GsmTimeParse.h"

#include <stdio.h>
#include <string.h>

#include "common/Constants.h"

namespace gsm_time {

namespace {
bool isLeap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

int daysInMonth(int y, int m) {
    static const uint8_t kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return (m == 2 && isLeap(y)) ? 29 : kDays[m - 1];
}
} // namespace

int64_t daysFromCivil(int year, int month, int day) {
    // H. Hinnant, "chrono-compatible low-level date algorithms".
    const int y = year - (month <= 2 ? 1 : 0);
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;
    const int64_t mp = month > 2 ? month - 3 : month + 9;
    const int64_t doy = (153 * mp + 2) / 5 + day - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

bool civilToEpochUtc(int year, int month, int day, int hour, int minute, int second, time_t& out) {
    if (month < 1 || month > 12) return false;
    if (day < 1 || day > daysInMonth(year, month)) return false;
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 60) return false;
    const int64_t secs = daysFromCivil(year, month, day) * 86400LL + hour * 3600LL + minute * 60LL + second;
    out = (time_t)secs;
    return true;
}

bool parseCclk(const char* line, CclkTime& out) {
    out = CclkTime{};
    if (!line) return false;
    const char* p = strstr(line, "+CCLK:");
    if (!p) return false;
    p = strchr(p, '"');
    if (!p) return false;
    p++;

    int yy = 0, MM = 0, dd = 0, hh = 0, mm = 0, ss = 0, tzq = 0;
    char sign = '+';
    if (sscanf(p, "%2d/%2d/%2d,%2d:%2d:%2d%c%d", &yy, &MM, &dd, &hh, &mm, &ss, &sign, &tzq) < 8) {
        return false;
    }
    if (sign == '-') tzq = -tzq;
    else if (sign != '+') return false;
    if (tzq < -48 || tzq > 56) return false;

    time_t local = 0;
    if (!civilToEpochUtc(2000 + yy, MM, dd, hh, mm, ss, local)) return false;
    out.utc = local - (time_t)((int32_t)tzq * 15 * 60);
    if (out.utc <= 0) return false;

    out.tzQuarters = (int16_t)tzq;
    if ((tzq % 4) == 0) {
        const int hours = tzq / 4;
        if (hours >= -12 && hours <= 14) {
            out.tzWholeHours = true;
            out.tzHours = (int8_t)hours;
        }
    }
    return true;
}

bool parseCipgsmloc(const char* line, time_t& utcOut) {
    if (!line) return false;
    const char* p = strstr(line, "+CIPGSMLOC:");
    if (!p) return false;
    p += 11;
    while (*p == ' ') p++;

    int code = -1;
    int yyyy = 0, MM = 0, dd = 0, hh = 0, mm = 0, ss = 0;
    if (sscanf(p, "%d,%d/%d/%d,%d:%d:%d", &code, &yyyy, &MM, &dd, &hh, &mm, &ss) < 7) return false;
    if (code != 0) return false;
    if (yyyy < TimeSync::MIN_SANE_YEAR) return false;
    time_t utc = 0;
    if (!civilToEpochUtc(yyyy, MM, dd, hh, mm, ss, utc)) return false;
    if (!epochSane(utc)) return false;
    utcOut = utc;
    return true;
}

bool epochSane(time_t utc) { return utc >= (time_t)TimeSync::MIN_SANE_EPOCH_UTC; }

int8_t tzHoursToQuarters(int8_t tzOffsetHours) {
    int q = static_cast<int>(tzOffsetHours) * 4;
    if (q > 48) q = 48;
    if (q < -48) q = -48;
    return (int8_t)q;
}

} // namespace gsm_time
