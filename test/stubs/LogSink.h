#pragma once

#include <stddef.h>

// Captures lines that Logger sends to sseBroadcastLog() in native tests.
namespace test_log {
void clear();
size_t count();
/// i = 0 is the oldest captured line.
const char* line(size_t i);
/// Empty string when nothing was captured.
const char* last();
bool contains(const char* needle);
} // namespace test_log
