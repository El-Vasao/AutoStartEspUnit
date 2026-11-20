#pragma once

// JsonParseFile.cpp includes <pgmspace.h>. Byte reads are already defined in Arduino.h.
#include <stdint.h>

#ifndef pgm_read_byte
#define pgm_read_byte(addr) (*(const uint8_t*)(addr))
#endif
