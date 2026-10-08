#include "gsm/GsmAtParse.h"

#include <stdlib.h>
#include <string.h>

namespace gsm_at {

bool parseCregStat(const char* line, int8_t& statOut) {
    if (!line) return false;
    const char* p = strstr(line, "+CREG:");
    if (!p) return false;
    p += 6;
    while (*p == ' ' || *p == ':') p++;
    char* end = nullptr;
    const long first = strtol(p, &end, 10);
    if (!end || end == p) return false;
    while (*end == ' ') end++;
    long stat = first;
    if (*end == ',') {
        end++;
        while (*end == ' ') end++;
        if (*end == '"') {
            stat = first;
        } else {
            stat = strtol(end, &end, 10);
        }
    }
    if (stat < 0 || stat > 5) return false;
    statOut = (int8_t)stat;
    return true;
}

bool parseCgattStat(const char* line, int8_t& statOut) {
    if (!line) return false;
    const char* p = strstr(line, "+CGATT:");
    if (!p) return false;
    p += 7;
    while (*p == ' ' || *p == ':') p++;
    char* end = nullptr;
    long v = strtol(p, &end, 10);
    if (v != 0 && v != 1) return false;
    statOut = (int8_t)v;
    return true;
}

bool isIpv4Line(const char* s) {
    if (!s) return false;
    int parts = 0;
    int acc = -1;
    for (const char* p = s; *p; p++) {
        const char c = *p;
        if (c >= '0' && c <= '9') {
            int d = c - '0';
            acc = (acc < 0) ? d : (acc * 10 + d);
            if (acc > 255) return false;
        } else if (c == '.') {
            if (acc < 0) return false;
            parts++;
            acc = -1;
        } else if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            break;
        } else {
            return false;
        }
    }
    return (parts == 3 && acc >= 0);
}

bool sapbrLineHasUsableBearer(const char* line) {
    if (!line) return false;
    const char* p = strstr(line, "+SAPBR:");
    if (!p) return false;
    p += 7;
    while (*p == ' ' || *p == ':') p++;

    // +SAPBR: <cid>,<status>,"<ip>"
    char* end = nullptr;
    (void)strtol(p, &end, 10); // cid
    if (!end || end == p || *end != ',') return false;
    end++;
    while (*end == ' ') end++;
    const long status = strtol(end, &end, 10);
    if (status != 1) return false;

    const char* q1 = strchr(end, '\"');
    if (!q1) return false;
    q1++;
    const char* q2 = strchr(q1, '\"');
    if (!q2) return false;
    char ip[32];
    const size_t n = (size_t)(q2 - q1);
    if (n == 0 || n >= sizeof(ip)) return false;
    memcpy(ip, q1, n);
    ip[n] = '\0';
    if (strcmp(ip, "0.0.0.0") == 0) return false;
    return isIpv4Line(ip);
}

size_t extractPhoneDigits(const char* in, char* out, size_t outCap) {
    if (!out || outCap == 0) return 0;
    out[0] = '\0';
    if (!in) return 0;
    size_t n = 0;
    for (const char* p = in; *p && n + 1 < outCap; p++) {
        if (*p >= '0' && *p <= '9') {
            out[n++] = *p;
        }
    }
    out[n] = '\0';
    return n;
}

bool phonesMatch(const char* a, const char* b) {
    char da[24];
    char db[24];
    const size_t na = extractPhoneDigits(a, da, sizeof(da));
    const size_t nb = extractPhoneDigits(b, db, sizeof(db));
    if (na == 0 || nb == 0) return false;
    const size_t need = (na < 10 && nb < 10) ? ((na < nb) ? na : nb) : 10;
    if (na < need || nb < need) {
        return na == nb && strcmp(da, db) == 0;
    }
    return strcmp(da + (na - need), db + (nb - need)) == 0;
}

bool parseClipDigits(const char* line, char* digitsOut, size_t digitsCap) {
    if (!line || !digitsOut || digitsCap == 0) return false;
    digitsOut[0] = '\0';
    const char* p = strstr(line, "+CLIP:");
    if (!p) return false;
    const char* q = strchr(p, '"');
    if (!q) return false;
    q++;
    const char* r = strchr(q, '"');
    if (!r || r <= q) return false;
    char raw[24];
    size_t n = (size_t)(r - q);
    if (n >= sizeof(raw)) n = sizeof(raw) - 1;
    memcpy(raw, q, n);
    raw[n] = '\0';
    return extractPhoneDigits(raw, digitsOut, digitsCap) > 0;
}

bool parseDtmfTone(const char* line, char& toneOut) {
    if (!line) return false;
    const char* p = strstr(line, "+DTMF:");
    if (!p) p = strstr(line, "+DDET:");
    if (!p) return false;
    p = strchr(p, ':');
    if (!p) return false;
    p++;
    while (*p == ' ' || *p == '"') p++;
    const char c = *p;
    if ((c >= '0' && c <= '9') || c == '*' || c == '#') {
        toneOut = c;
        return true;
    }
    return false;
}

} // namespace gsm_at
