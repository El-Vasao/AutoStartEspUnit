#pragma once

#include <stddef.h>
#include <stdint.h>

namespace gsm_at {

bool parseCregStat(const char* line, int8_t& statOut);
bool parseCgattStat(const char* line, int8_t& statOut);
bool isIpv4Line(const char* s);

/// Usable SAPBR bearer: status==1 and non-zero quoted IPv4 (e.g. +SAPBR: 1,1,"1.2.3.4").
/// Rejects closed bearer +SAPBR: 1,3,"0.0.0.0".
bool sapbrLineHasUsableBearer(const char* line);

/// Copy only digits from `in` into `out` (NUL-terminated). Returns digit count.
size_t extractPhoneDigits(const char* in, char* out, size_t outCap);

/// Match owner vs CLIP: compare digit suffixes (min 10 digits, or full if shorter).
bool phonesMatch(const char* a, const char* b);

/// Parse +CLIP: "number",... into digit buffer (digits only).
bool parseClipDigits(const char* line, char* digitsOut, size_t digitsCap);

/// Parse +DTMF: X or +DDET: X into a single tone char ('0'-'9','*','#').
bool parseDtmfTone(const char* line, char& toneOut);

} // namespace gsm_at
