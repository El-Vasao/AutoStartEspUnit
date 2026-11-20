#pragma once

// Замена FS.h в RAM. Разбор JSON читает буфер; LittleFS.open делит буфер с томом.
#include "Print.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <memory>
#include <string>

class File : public Stream {
public:
    File() = default;
    File(const char* data, size_t n) { assign(data, n); }
    explicit File(const char* text) { assign(text, text ? strlen(text) : 0); }

    void assign(const void* data, size_t n) {
        if (!data || n == 0) data_ = std::make_shared<std::string>();
        else data_ = std::make_shared<std::string>(static_cast<const char*>(data), n);
        pos_ = 0;
        open_ = true;
        writable_ = false;
    }

    // Буфер уже лежит в карте тома (открытие LittleFS).
    static File adopt(const std::shared_ptr<std::string>& data, bool writable) {
        File f;
        f.data_ = data;
        f.pos_ = 0;
        f.open_ = static_cast<bool>(data);
        f.writable_ = writable && f.open_;
        return f;
    }

    size_t write(uint8_t c) override {
        if (!open_ || !writable_ || !data_) return 0;
        data_->push_back(static_cast<char>(c));
        return 1;
    }
    size_t write(const uint8_t* buffer, size_t size) override {
        if (!open_ || !writable_ || !data_ || size == 0 || !buffer) return 0;
        data_->append(reinterpret_cast<const char*>(buffer), size);
        return size;
    }
    using Print::write;

    int available() override {
        if (!open_ || !data_ || pos_ >= data_->size()) return 0;
        const size_t n = data_->size() - pos_;
        return n > 0x7FFFFFFFu ? 0x7FFFFFFF : static_cast<int>(n);
    }
    int read() override {
        if (!open_ || !data_ || pos_ >= data_->size()) return -1;
        return static_cast<uint8_t>((*data_)[pos_++]);
    }
    int peek() override {
        if (!open_ || !data_ || pos_ >= data_->size()) return -1;
        return static_cast<uint8_t>((*data_)[pos_]);
    }

    size_t read(uint8_t* out, size_t n) {
        if (!out || !open_ || !data_ || pos_ >= data_->size() || n == 0) return 0;
        size_t c = data_->size() - pos_;
        if (c > n) c = n;
        memcpy(out, data_->data() + pos_, c);
        pos_ += c;
        return c;
    }

    size_t readBytes(char* buffer, size_t length) { return read(reinterpret_cast<uint8_t*>(buffer), length); }

    size_t size() const { return data_ ? data_->size() : 0; }
    void close() { open_ = false; }

    explicit operator bool() const { return open_; }

private:
    std::shared_ptr<std::string> data_;
    size_t pos_{0};
    bool open_{false};
    bool writable_{false};
};

// База для LittleFS: FsManager::webFs() возвращает FS&.
class FS {};
