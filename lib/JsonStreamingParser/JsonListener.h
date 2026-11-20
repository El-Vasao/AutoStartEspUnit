/**The MIT License (MIT)

Copyright (c) 2015 by Daniel Eichhorn

Заметка ветки: `key`/`value` получают строки C с завершающим нулём, указывающие в
черновой буфер парсера; скопируйте текст до возврата, если он нужен после следующих вызовов parse().
*/

#pragma once

#include <Arduino.h>

class JsonListener {
public:
    virtual void whitespace(char c) = 0;

    virtual void startDocument() = 0;

    virtual void key(const char* key) = 0;

    virtual void value(const char* value) = 0;

    virtual void endArray() = 0;

    virtual void endObject() = 0;

    virtual void endDocument() = 0;

    virtual void startArray() = 0;

    virtual void startObject() = 0;
};
