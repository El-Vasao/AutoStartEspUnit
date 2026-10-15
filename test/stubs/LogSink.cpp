#include "LogSink.h"

#include <string.h>

namespace {
constexpr size_t kMaxLines = 64;
constexpr size_t kLineBytes = 256;
char g_lines[kMaxLines][kLineBytes];
size_t g_count = 0;
} // namespace

// Production definition lives in src/web/WebServer.Core.cpp.
void sseBroadcastLog(const char* message) {
    if (!message) return;
    if (g_count == kMaxLines) {
        memmove(g_lines[0], g_lines[1], sizeof(g_lines[0]) * (kMaxLines - 1));
        g_count--;
    }
    strncpy(g_lines[g_count], message, kLineBytes - 1);
    g_lines[g_count][kLineBytes - 1] = '\0';
    g_count++;
}

namespace test_log {
void clear() { g_count = 0; }
size_t count() { return g_count; }
const char* line(size_t i) { return i < g_count ? g_lines[i] : ""; }
const char* last() { return g_count ? g_lines[g_count - 1] : ""; }
bool contains(const char* needle) {
    for (size_t i = 0; i < g_count; i++) {
        if (strstr(g_lines[i], needle)) return true;
    }
    return false;
}
} // namespace test_log
