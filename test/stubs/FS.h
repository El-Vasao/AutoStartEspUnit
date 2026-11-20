#pragma once

// RAM stand-in for FS.h. JsonParseFile only needs available() and read(buf, n).
#include "Print.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <string>

class File : public Stream {
public:
    File() = default;
    File(const char* data, size_t n) { assign(data, n); }
    explicit File(const char* text) { assign(text, text ? strlen(text) : 0); }

    void assign(const void* data, size_t n) {
        buf_.assign(static_cast<const char*>(data), data ? n : 0);
        pos_ = 0;
    }

    size_t write(uint8_t) override { return 0; }
    using Print::write;

    int available() override {
        if (pos_ >= buf_.size()) return 0;
        const size_t n = buf_.size() - pos_;
        return n > 0x7FFFFFFFu ? 0x7FFFFFFF : static_cast<int>(n);
    }
    int read() override { return pos_ < buf_.size() ? static_cast<uint8_t>(buf_[pos_++]) : -1; }
    int peek() override { return pos_ < buf_.size() ? static_cast<uint8_t>(buf_[pos_]) : -1; }

    size_t read(uint8_t* out, size_t n) {
        if (!out || pos_ >= buf_.size() || n == 0) return 0;
        size_t c = buf_.size() - pos_;
        if (c > n) c = n;
        memcpy(out, buf_.data() + pos_, c);
        pos_ += c;
        return c;
    }

    explicit operator bool() const { return true; }

private:
    std::string buf_;
    size_t pos_{0};
};
