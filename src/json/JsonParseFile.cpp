// src/json/Json.ParseFile.cpp
#include "json/Json.ParseFile.h"

#include "common/Constants.h"
#include "common/EspHal.h"
#include "JsonStreamingParser.h"
#include "JsonListener.h"

#include <stddef.h>

#include <pgmspace.h>

namespace {

constexpr size_t kChunk = PoolLimits::FS_SCRATCH_BYTES;

} // namespace

bool jsonStreamingParseWholeFile(File& file, JsonListener& listener) {
    JsonStreamingParser parser;
    parser.reset();
    parser.setListener(&listener);
    uint8_t chunk[kChunk];
    uint32_t fed = 0;
    while (file.available()) {
        const size_t n = file.read(chunk, sizeof chunk);
        if (n == 0) break;
        for (size_t i = 0; i < n; i++) {
            parser.parse(static_cast<char>(chunk[i]));
        }
        fed += static_cast<uint32_t>(n);
        if ((fed & 0xFFu) == 0) {
            espHalFeedWdt();
        }
    }
    return true;
}

bool jsonStreamingParseProgmem(JsonListener& listener, const char* pgmPtr) {
    if (!pgmPtr) return false;
    JsonStreamingParser parser;
    parser.reset();
    parser.setListener(&listener);
    for (;;) {
        const char c = static_cast<char>(pgm_read_byte(reinterpret_cast<const uint8_t*>(pgmPtr)));
        if (c == '\0') break;
        parser.parse(c);
        pgmPtr++;
        if ((((uintptr_t)pgmPtr) & 0xFFu) == 0) {
            espHalFeedWdt();
        }
    }
    return true;
}
