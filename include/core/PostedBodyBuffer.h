#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "common/Constants.h"

/**
 * Тело POST до записи во временный файл. Без ФС и без Core: границы проверяются здесь.
 * drop() снимает вооружение, но не затирает байты — flush читает их сразу после drop().
 */
class PostedBodyBuffer {
public:
    bool arm(size_t total) {
        if (armed_) return false;
        if (total == 0 || total > Limits::CONFIG_JSON_SIZE) return false;
        len_ = 0;
        expect_ = total;
        armed_ = true;
        return true;
    }

    bool append(size_t index, const uint8_t* data, size_t len) {
        if (!armed_ || !data || len == 0) return false;
        if (index != len_ || len > expect_ - len_) return false;
        memcpy(body_ + len_, data, len);
        len_ += len;
        return true;
    }

    bool complete() const { return armed_ && expect_ > 0 && len_ == expect_; }

    void drop() {
        armed_ = false;
        len_ = 0;
        expect_ = 0;
    }

    const uint8_t* data() const { return body_; }
    size_t length() const { return len_; }

private:
    uint8_t body_[Limits::CONFIG_JSON_SIZE]{};
    size_t len_{0};
    size_t expect_{0};
    bool armed_{false};
};
