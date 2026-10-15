// include/json/Json.ParseFile.h — обёртка над JsonStreamingParser (lib/) + File / PROGMEM
#pragma once

#include <Arduino.h>
#include <FS.h>

class JsonListener;
class JsonStreamingParser;

/// Feed whole File to parser in chunks with periodic WDT.
bool jsonStreamingParseWholeFile(File& file, JsonListener& listener);

/// Parse JSON from NUL-terminated flash string (PROGMEM-safe via pgm_read_byte).
bool jsonStreamingParseProgmem(JsonListener& listener, const char* pgmPtr);
