/**The MIT License (MIT)

Copyright (c) 2015 by Daniel Eichhorn

Fork note: `key`/`value` take null-terminated C strings pointing into the parser's
scratch buffer; copy before returning if you need to retain the text across parse() calls.
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
