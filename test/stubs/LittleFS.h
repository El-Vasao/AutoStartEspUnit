#pragma once

// RAM-том: путь → байты. Хватает exists/open/rename/remove/mkdir/totalBytes/usedBytes.
#include "FS.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ram_fs {

enum class RamOpKind : uint8_t { Rename, Remove };

struct RamOp {
    RamOpKind kind;
    bool ok;
    std::string from;
    std::string to;
};

struct Entry {
    std::shared_ptr<std::string> data;
    bool isDir{false};
};

struct State {
    std::map<std::string, Entry> files;
    std::vector<RamOp> trace;
    std::string failRenameTo;
    bool failArmed{false};
};

inline State& state() {
    static State s;
    return s;
}

inline void reset() { state() = State{}; }

inline const std::vector<RamOp>& trace() { return state().trace; }

inline void failNextRenameTo(const char* dest) {
    state().failRenameTo = dest ? dest : "";
    state().failArmed = true;
}

inline bool exists(const char* path) {
    if (!path) return false;
    return state().files.find(path) != state().files.end();
}

inline void put(const char* path, const char* text) {
    if (!path) return;
    Entry& e = state().files[path];
    if (!e.data) e.data = std::make_shared<std::string>();
    e.data->assign(text ? text : "");
    e.isDir = false;
}

inline std::string read(const char* path) {
    if (!path) return {};
    auto it = state().files.find(path);
    if (it == state().files.end() || !it->second.data) return {};
    return *it->second.data;
}

inline bool mkdir(const char* path) {
    if (!path || !path[0]) return false;
    Entry& e = state().files[path];
    if (e.data && !e.isDir && !e.data->empty()) return false;
    e.isDir = true;
    if (!e.data) e.data = std::make_shared<std::string>();
    return true;
}

inline bool remove(const char* path) {
    RamOp op{RamOpKind::Remove, false, path ? path : "", ""};
    auto& files = state().files;
    auto it = files.find(op.from);
    op.ok = it != files.end();
    if (op.ok) files.erase(it);
    state().trace.push_back(op);
    return op.ok;
}

inline bool rename(const char* from, const char* to) {
    RamOp op{RamOpKind::Rename, false, from ? from : "", to ? to : ""};
    auto& st = state();
    if (st.failArmed && op.to == st.failRenameTo) {
        st.failArmed = false;
        st.trace.push_back(op);
        return false;
    }
    auto it = st.files.find(op.from);
    if (it == st.files.end() || st.files.find(op.to) != st.files.end()) {
        st.trace.push_back(op);
        return false;
    }
    Entry node = std::move(it->second);
    st.files.erase(it);
    st.files.emplace(op.to, std::move(node));
    op.ok = true;
    st.trace.push_back(op);
    return true;
}

inline File open(const char* path, const char* mode) {
    const bool writing = mode && (mode[0] == 'w' || mode[0] == 'a' || mode[0] == 'W');
    auto& st = state();
    if (!writing) {
        auto it = st.files.find(path ? path : "");
        if (it == st.files.end() || it->second.isDir || !it->second.data) return File();
        return File::adopt(it->second.data, false);
    }
    if (!path || !path[0]) return File();
    Entry& e = st.files[path];
    if (!e.data) e.data = std::make_shared<std::string>();
    if (mode[0] == 'w' || mode[0] == 'W') e.data->clear();
    e.isDir = false;
    return File::adopt(e.data, true);
}

inline size_t totalBytes() { return 1024u * 1024u; }

inline size_t usedBytes() {
    size_t n = 0;
    for (const auto& kv : state().files) {
        if (kv.second.isDir || !kv.second.data) continue;
        n += kv.second.data->size();
    }
    return n;
}

} // пространство имён ram_fs

class LittleFSClass : public FS {
public:
    bool begin(bool = false) { return true; }
    void end() {}
    bool format() {
        ram_fs::reset();
        return true;
    }
    bool exists(const char* path) { return ram_fs::exists(path); }
    bool remove(const char* path) { return ram_fs::remove(path); }
    bool rename(const char* from, const char* to) { return ram_fs::rename(from, to); }
    bool mkdir(const char* path) { return ram_fs::mkdir(path); }
    bool rmdir(const char* path) { return ram_fs::remove(path); }
    File open(const char* path, const char* mode = "r") { return ram_fs::open(path, mode); }
    size_t totalBytes() { return ram_fs::totalBytes(); }
    size_t usedBytes() { return ram_fs::usedBytes(); }
};

inline LittleFSClass LittleFS;
