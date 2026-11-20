#pragma once

// JsonParseFile.cpp подключает <pgmspace.h>. Чтение байта уже определено в Arduino.h.
#include <stdint.h>

#include <string.h>

#ifndef pgm_read_byte
#define pgm_read_byte(addr) (*(const uint8_t*)(addr))
#endif
#ifndef memcpy_P
#define memcpy_P(dest, src, n) memcpy((dest), (src), (n))
#endif
#ifndef strcmp_P
#define strcmp_P(a, b) strcmp((a), (b))
#endif
