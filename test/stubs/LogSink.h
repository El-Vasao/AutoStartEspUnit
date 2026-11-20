#pragma once

#include <stddef.h>

// Ловит строки, которые Logger отдаёт в sseBroadcastLog() в нативных тестах.
namespace test_log {
void clear();
size_t count();
/// i = 0 — самая старая пойманная строка.
const char* line(size_t i);
/// Пустая строка, если ничего не поймано.
const char* last();
bool contains(const char* needle);
} // пространство имён test_log
