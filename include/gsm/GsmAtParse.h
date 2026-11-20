#pragma once

#include <stddef.h>
#include <stdint.h>

namespace gsm_at {

bool parseCregStat(const char* line, int8_t& statOut);
bool parseCgattStat(const char* line, int8_t& statOut);
bool isIpv4Line(const char* s);

/// Годный bearer SAPBR: status==1 и ненулевой IPv4 в кавычках (например +SAPBR: 1,1,"1.2.3.4").
/// Отклоняет закрытый bearer +SAPBR: 1,3,"0.0.0.0".
bool sapbrLineHasUsableBearer(const char* line);

/// Копирует из `in` в `out` только цифры (с завершающим NUL). Возвращает число цифр.
size_t extractPhoneDigits(const char* in, char* out, size_t outCap);

/// Сверка владельца и CLIP: сравнение хвостов цифр (Gsm::PHONE_MATCH_MIN_DIGITS, или целиком, если короче).
bool phonesMatch(const char* a, const char* b);

/// Разбор +CLIP: "number",... в буфер цифр (только цифры).
bool parseClipDigits(const char* line, char* digitsOut, size_t digitsCap);

/// Разбор +DTMF: X или +DDET: X в один символ тона ('0'-'9','*','#').
bool parseDtmfTone(const char* line, char& toneOut);

/// +CLCC stat: 0 активен (ответили), 2 набор, 3 вызов. false, если строка не +CLCC.
bool parseClccStat(const char* line, int8_t& statOut);

} // namespace gsm_at
