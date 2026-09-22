#include "mqtt/MqttCommandParser.h"

#include <Arduino.h>
#include <ctype.h>
#include <string.h>

namespace {

uint8_t parseU8(const char* v) {
    if (!v || !*v) return 0;
    char* end = nullptr;
    unsigned long n = strtoul(v, &end, 10);
    if (!end || *end != '\0' || n > 255u) return 0;
    return static_cast<uint8_t>(n);
}

uint16_t parseU16(const char* v) {
    if (!v || !*v) return 0;
    char* end = nullptr;
    unsigned long n = strtoul(v, &end, 10);
    if (!end || *end != '\0' || n > 65535u) return 0;
    return static_cast<uint16_t>(n);
}

bool parseBoolToken(const char* v, bool* out) {
    if (!v || !out) return false;
    if (strcmp(v, "true") == 0 || strcmp(v, "1") == 0) {
        *out = true;
        return true;
    }
    if (strcmp(v, "false") == 0 || strcmp(v, "0") == 0) {
        *out = false;
        return true;
    }
    return false;
}

void skipWs(const char*& p) {
    while (*p && isspace((unsigned char)*p)) p++;
}

bool readJsonStringContent(const char*& p, char* out, size_t outCap) {
    if (!out || outCap == 0) return false;
    size_t w = 0;
    while (*p && *p != '"') {
        if (*p == '\\') {
            p++;
            if (!*p) return false;
            if (w + 1 >= outCap) return false;
            out[w++] = *p++;
        } else {
            if (w + 1 >= outCap) return false;
            out[w++] = *p++;
        }
    }
    if (*p != '"') return false;
    p++;
    out[w] = '\0';
    return true;
}

enum class Key : uint8_t { None, Id, Cmd, Program, Name, Ref, Enabled };

Key keyFrom(const char* k) {
    if (strcmp(k, "id") == 0) return Key::Id;
    if (strcmp(k, "cmd") == 0) return Key::Cmd;
    if (strcmp(k, "program") == 0) return Key::Program;
    if (strcmp(k, "name") == 0) return Key::Name;
    if (strcmp(k, "ref") == 0) return Key::Ref;
    if (strcmp(k, "enabled") == 0) return Key::Enabled;
    return Key::None;
}

MqttSetName setNameFrom(const char* n) {
    if (strcmp(n, "thermostat") == 0) return MqttSetName::Thermostat;
    if (strcmp(n, "battery_saver") == 0) return MqttSetName::BatterySaver;
    if (strcmp(n, "input") == 0) return MqttSetName::Input;
    if (strcmp(n, "trigger") == 0) return MqttSetName::Trigger;
    if (strcmp(n, "temp_trigger") == 0) return MqttSetName::TempTrigger;
    return MqttSetName::None;
}

MqttCommandKind cmdFrom(const char* c) {
    if (strcmp(c, "run") == 0) return MqttCommandKind::Run;
    if (strcmp(c, "stop") == 0) return MqttCommandKind::Stop;
    if (strcmp(c, "list") == 0) return MqttCommandKind::List;
    if (strcmp(c, "status") == 0) return MqttCommandKind::Status;
    if (strcmp(c, "set") == 0) return MqttCommandKind::Set;
    return MqttCommandKind::None;
}

} // namespace

bool mqttExtractReqId(const char* json, char* out, size_t outCap) {
    if (!json || !out || outCap < 2) return false;
    out[0] = '\0';
    const char* p = strstr(json, "\"id\"");
    if (!p) return false;
    p += 4;
    skipWs(p);
    if (*p != ':') return false;
    p++;
    skipWs(p);
    if (*p != '"') return false;
    p++;
    return readJsonStringContent(p, out, outCap) && out[0] != '\0' && strlen(out) <= 16;
}

bool parseMqttCommandJson(const char* json, MqttCommand& out) {
    out = MqttCommand{};
    if (!json || !*json) return false;

    const char* p = json;
    skipWs(p);
    if (*p != '{') return false;
    p++;

    char cmdStr[24]{};
    char nameStr[24]{};
    bool gotCmd = false;

    for (;;) {
        skipWs(p);
        if (*p == '}') {
            p++;
            skipWs(p);
            if (*p != '\0') return false;
            break;
        }
        if (*p != '"') return false;
        p++;

        char keybuf[16];
        if (!readJsonStringContent(p, keybuf, sizeof keybuf)) return false;

        skipWs(p);
        if (*p != ':') return false;
        p++;
        skipWs(p);

        const Key key = keyFrom(keybuf);

        if (*p == '"') {
            p++;
            char vbuf[48];
            if (!readJsonStringContent(p, vbuf, sizeof vbuf)) return false;
            switch (key) {
                case Key::Id:
                    if (vbuf[0] == '\0' || strlen(vbuf) > 16) return false;
                    strlcpy(out.id, vbuf, sizeof(out.id));
                    break;
                case Key::Cmd:
                    strlcpy(cmdStr, vbuf, sizeof(cmdStr));
                    gotCmd = true;
                    break;
                case Key::Name:
                    strlcpy(nameStr, vbuf, sizeof(nameStr));
                    break;
                case Key::Program:
                    out.programId = parseU8(vbuf);
                    break;
                case Key::Ref:
                    out.ref = parseU16(vbuf);
                    break;
                case Key::Enabled: {
                    bool b = false;
                    if (!parseBoolToken(vbuf, &b)) return false;
                    out.enabled = b;
                    out.hasEnabled = true;
                    break;
                }
                default:
                    break;
            }
        } else if (*p == 't' || *p == 'f') {
            // bare true/false
            char vbuf[8]{};
            size_t i = 0;
            while (*p && isalpha((unsigned char)*p) && i + 1 < sizeof(vbuf)) vbuf[i++] = *p++;
            vbuf[i] = '\0';
            if (key == Key::Enabled) {
                bool b = false;
                if (!parseBoolToken(vbuf, &b)) return false;
                out.enabled = b;
                out.hasEnabled = true;
            }
        } else {
            const char* n0 = p;
            if (*p == '-') p++;
            while (*p && (isdigit((unsigned char)*p))) p++;
            const size_t nlen = static_cast<size_t>(p - n0);
            char nbuf[16];
            if (nlen == 0 || nlen >= sizeof nbuf) return false;
            memcpy(nbuf, n0, nlen);
            nbuf[nlen] = '\0';
            if (key == Key::Program) out.programId = parseU8(nbuf);
            else if (key == Key::Ref) out.ref = parseU16(nbuf);
            else if (key == Key::Enabled) {
                bool b = false;
                if (!parseBoolToken(nbuf, &b)) return false;
                out.enabled = b;
                out.hasEnabled = true;
            }
        }

        skipWs(p);
        if (*p == ',') {
            p++;
            continue;
        }
        if (*p == '}') {
            p++;
            skipWs(p);
            if (*p != '\0') return false;
            break;
        }
        return false;
    }

    if (!gotCmd || out.id[0] == '\0') return false;
    out.kind = cmdFrom(cmdStr);
    if (out.kind == MqttCommandKind::None) return false;
    if (out.kind == MqttCommandKind::Set) {
        out.setName = setNameFrom(nameStr);
    }
    return true;
}
