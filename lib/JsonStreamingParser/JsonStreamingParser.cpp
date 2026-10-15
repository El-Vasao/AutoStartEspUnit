/**The MIT License (MIT)

Copyright (c) 2015 by Daniel Eichhorn

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

See more at http://blog.squix.ch and https://github.com/squix78/json-streaming-parser
*/

#include "JsonStreamingParser.h"

#include <string.h>

JsonStreamingParser::JsonStreamingParser() {
    reset();
}

void JsonStreamingParser::reset() {
    state = STATE_START_DOCUMENT;
    stackPos = 0;
    error = false;
    bufferPos = 0;
    unicodeEscapeBufferPos = 0;
    unicodeBufferPos = 0;
    unicodeHighSurrogate = -1;
    characterCounter = 0;
}

void JsonStreamingParser::setListener(JsonListener* listener) {
    myListener = listener;
}

void JsonStreamingParser::fail() {
    error = true;
}

void JsonStreamingParser::parse(char c) {
    if (error) return;
    // UTF-8 BOM (EF BB BF) before the root value, e.g. a config.json saved by a Windows editor.
    if (state == STATE_START_DOCUMENT && characterCounter < 3) {
        static const unsigned char kBom[3] = {0xEF, 0xBB, 0xBF};
        if (static_cast<unsigned char>(c) == kBom[characterCounter]) {
            characterCounter++;
            return;
        }
    }
    // JSON whitespace (RFC 8259): space, tab, LF, CR — significant only inside strings and numbers.
    if ((c == ' ' || c == '\t' || c == '\n' || c == '\r') &&
        !(state == STATE_IN_STRING || state == STATE_UNICODE || state == STATE_START_ESCAPE ||
          state == STATE_UNICODE_SURROGATE || state == STATE_IN_NUMBER)) {
        return;
    }
    switch (state) {
    case STATE_IN_STRING:
      if (c == '"') {
        endString();
      } else if (c == '\\') {
        state = STATE_START_ESCAPE;
      } else if (static_cast<unsigned char>(c) < 0x20) {
        fail();  // unescaped control character
      } else {
        appendByte(c);
      }
      break;
    case STATE_IN_ARRAY:
      if (c == ']') {
        endArray();
      } else {
        startValue(c);
      }
      break;
    case STATE_IN_OBJECT:
      if (c == '}') {
        endObject();
      } else if (c == '"') {
        startKey();
      } else {
        fail();
      }
      break;
    case STATE_END_KEY:
      if (c != ':') {
        fail();
        break;
      }
      state = STATE_AFTER_KEY;
      break;
    case STATE_AFTER_KEY:
      startValue(c);
      break;
    case STATE_START_ESCAPE:
      processEscapeCharacters(c);
      break;
    case STATE_UNICODE:
      processUnicodeCharacter(c);
      break;
    case STATE_UNICODE_SURROGATE:
      unicodeEscapeBuffer[unicodeEscapeBufferPos++] = c;
      if (unicodeEscapeBufferPos == 2) {
        endUnicodeSurrogateInterstitial();
      }
      break;
    case STATE_AFTER_VALUE: {
      if (stackPos <= 0) {
        fail();
        break;
      }
      const int within = stack[stackPos - 1];
      if (within == STACK_OBJECT) {
        if (c == '}') {
          endObject();
        } else if (c == ',') {
          state = STATE_IN_OBJECT;
        } else {
          fail();
        }
      } else if (within == STACK_ARRAY) {
        if (c == ']') {
          endArray();
        } else if (c == ',') {
          state = STATE_IN_ARRAY;
        } else {
          fail();
        }
      } else {
        fail();
      }
    } break;
    case STATE_IN_NUMBER:
      if (c >= '0' && c <= '9') {
        appendByte(c);
      } else if (c == '.') {
        if (doesCharArrayContain(buffer, bufferPos, '.') || doesCharArrayContain(buffer, bufferPos, 'e') ||
            doesCharArrayContain(buffer, bufferPos, 'E')) {
          fail();
          break;
        }
        appendByte(c);
      } else if (c == 'e' || c == 'E') {
        if (doesCharArrayContain(buffer, bufferPos, 'e') || doesCharArrayContain(buffer, bufferPos, 'E')) {
          fail();
          break;
        }
        appendByte(c);
      } else if (c == '+' || c == '-') {
        const char last = bufferPos > 0 ? buffer[bufferPos - 1] : '\0';
        if (!(last == 'e' || last == 'E')) {
          fail();
          break;
        }
        appendByte(c);
      } else {
        endNumber();
        // The character after the number belongs to the enclosing structure.
        parse(c);
      }
      break;
    case STATE_IN_TRUE:
      appendByte(c);
      if (bufferPos == 4) endLiteral("true");
      break;
    case STATE_IN_FALSE:
      appendByte(c);
      if (bufferPos == 5) endLiteral("false");
      break;
    case STATE_IN_NULL:
      appendByte(c);
      if (bufferPos == 4) endLiteral("null");
      break;
    case STATE_START_DOCUMENT:
      if (c == '[') {
        if (myListener) myListener->startDocument();
        startArray();
      } else if (c == '{') {
        if (myListener) myListener->startDocument();
        startObject();
      } else {
        fail();  // document must start with an object or array
      }
      break;
    case STATE_DONE:
      fail();  // non-whitespace after the root value
      break;
    default:
      fail();
      break;
    }
    characterCounter++;
}

void JsonStreamingParser::appendByte(char c) {
    // Over-long tokens are truncated (fixed fields copy at most their size anyway).
    if (bufferPos < BUFFER_MAX_LENGTH - 1) buffer[bufferPos++] = c;
}

void JsonStreamingParser::appendUtf8(uint32_t cp) {
    if (cp < 0x80) {
        appendByte(static_cast<char>(cp));
    } else if (cp < 0x800) {
        appendByte(static_cast<char>(0xC0 | (cp >> 6)));
        appendByte(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        appendByte(static_cast<char>(0xE0 | (cp >> 12)));
        appendByte(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        appendByte(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        appendByte(static_cast<char>(0xF0 | (cp >> 18)));
        appendByte(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        appendByte(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        appendByte(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

bool JsonStreamingParser::push(int kind) {
    if (stackPos >= JSON_MAX_DEPTH) {
        fail();
        return false;
    }
    stack[stackPos++] = kind;
    return true;
}

void JsonStreamingParser::endString() {
    if (stackPos <= 0) {
        fail();
        return;
    }
    const int popped = stack[--stackPos];
    buffer[bufferPos] = '\0';
    if (popped == STACK_KEY) {
        if (myListener) myListener->key(buffer);
        state = STATE_END_KEY;
    } else if (popped == STACK_STRING) {
        if (myListener) myListener->value(buffer);
        state = STATE_AFTER_VALUE;
    } else {
        fail();
    }
    bufferPos = 0;
}

void JsonStreamingParser::startValue(char c) {
    if (c == '[') {
      startArray();
    } else if (c == '{') {
      startObject();
    } else if (c == '"') {
      startString();
    } else if (isDigit(c)) {
      startNumber(c);
    } else if (c == 't') {
      state = STATE_IN_TRUE;
      appendByte(c);
    } else if (c == 'f') {
      state = STATE_IN_FALSE;
      appendByte(c);
    } else if (c == 'n') {
      state = STATE_IN_NULL;
      appendByte(c);
    } else {
      fail();
    }
}

bool JsonStreamingParser::isDigit(char c) {
    // Only concerned with the first character in a number.
    return (c >= '0' && c <= '9') || c == '-';
}

void JsonStreamingParser::endArray() {
    if (stackPos <= 0 || stack[stackPos - 1] != STACK_ARRAY) {
        fail();
        return;
    }
    stackPos--;
    if (myListener) myListener->endArray();
    state = STATE_AFTER_VALUE;
    if (stackPos == 0) endDocument();
}

void JsonStreamingParser::startKey() {
    if (!push(STACK_KEY)) return;
    state = STATE_IN_STRING;
}

void JsonStreamingParser::endObject() {
    if (stackPos <= 0 || stack[stackPos - 1] != STACK_OBJECT) {
        fail();
        return;
    }
    stackPos--;
    if (myListener) myListener->endObject();
    state = STATE_AFTER_VALUE;
    if (stackPos == 0) endDocument();
}

void JsonStreamingParser::processEscapeCharacters(char c) {
    switch (c) {
        case '"':  appendByte('"'); break;
        case '\\': appendByte('\\'); break;
        case '/':  appendByte('/'); break;
        case 'b':  appendByte('\b'); break;
        case 'f':  appendByte('\f'); break;
        case 'n':  appendByte('\n'); break;
        case 'r':  appendByte('\r'); break;
        case 't':  appendByte('\t'); break;
        case 'u':
            unicodeBufferPos = 0;
            state = STATE_UNICODE;
            return;
        default:
            fail();
            return;
    }
    state = STATE_IN_STRING;
}

void JsonStreamingParser::processUnicodeCharacter(char c) {
    if (!isHexCharacter(c)) {
        fail();
        return;
    }
    unicodeBuffer[unicodeBufferPos++] = c;
    if (unicodeBufferPos < 4) return;

    const int codepoint = getHexArrayAsDecimal(unicodeBuffer, unicodeBufferPos);
    unicodeBufferPos = 0;
    if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
        if (unicodeHighSurrogate != -1) {
            fail();  // two high surrogates in a row
            return;
        }
        unicodeHighSurrogate = codepoint;
        unicodeEscapeBufferPos = 0;
        state = STATE_UNICODE_SURROGATE;  // expect "\u" + low surrogate
        return;
    }
    if (codepoint >= 0xDC00 && codepoint <= 0xDFFF) {
        if (unicodeHighSurrogate == -1) {
            fail();  // lone low surrogate
            return;
        }
        const uint32_t combined =
            0x10000u + ((uint32_t)(unicodeHighSurrogate - 0xD800) << 10) + (uint32_t)(codepoint - 0xDC00);
        unicodeHighSurrogate = -1;
        appendUtf8(combined);
        state = STATE_IN_STRING;
        return;
    }
    if (unicodeHighSurrogate != -1) {
        fail();  // high surrogate not followed by a low one
        return;
    }
    endUnicodeCharacter(codepoint);
}

bool JsonStreamingParser::isHexCharacter(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

int JsonStreamingParser::getHexArrayAsDecimal(const char* hexArray, int length) {
    int result = 0;
    for (int i = 0; i < length; i++) {
        const char current = hexArray[i];
        int value = 0;
        if (current >= 'a' && current <= 'f') {
            value = current - 'a' + 10;
        } else if (current >= 'A' && current <= 'F') {
            value = current - 'A' + 10;
        } else if (current >= '0' && current <= '9') {
            value = current - '0';
        }
        result = result * 16 + value;
    }
    return result;
}

bool JsonStreamingParser::doesCharArrayContain(const char* myArray, int length, char c) {
    for (int i = 0; i < length; i++) {
        if (myArray[i] == c) return true;
    }
    return false;
}

void JsonStreamingParser::endUnicodeSurrogateInterstitial() {
    unicodeEscapeBufferPos = 0;
    if (unicodeEscapeBuffer[0] != '\\' || unicodeEscapeBuffer[1] != 'u') {
        fail();  // high surrogate must be followed by "\u"
        return;
    }
    unicodeBufferPos = 0;
    state = STATE_UNICODE;
}

void JsonStreamingParser::endNumber() {
    buffer[bufferPos] = '\0';
    if (bufferPos == 1 && buffer[0] == '-') {
        fail();
        return;
    }
    if (myListener) myListener->value(buffer);
    bufferPos = 0;
    state = STATE_AFTER_VALUE;
}

void JsonStreamingParser::endDocument() {
    if (myListener) myListener->endDocument();
    state = STATE_DONE;
}

void JsonStreamingParser::endLiteral(const char* expected) {
    buffer[bufferPos] = '\0';
    bufferPos = 0;
    if (strcmp(buffer, expected) != 0) {
        fail();
        return;
    }
    if (myListener) myListener->value(expected);
    state = STATE_AFTER_VALUE;
}

void JsonStreamingParser::startArray() {
    if (!push(STACK_ARRAY)) return;
    if (myListener) myListener->startArray();
    state = STATE_IN_ARRAY;
}

void JsonStreamingParser::startObject() {
    if (!push(STACK_OBJECT)) return;
    if (myListener) myListener->startObject();
    state = STATE_IN_OBJECT;
}

void JsonStreamingParser::startString() {
    if (!push(STACK_STRING)) return;
    state = STATE_IN_STRING;
}

void JsonStreamingParser::startNumber(char c) {
    state = STATE_IN_NUMBER;
    appendByte(c);
}

void JsonStreamingParser::endUnicodeCharacter(int codepoint) {
    appendUtf8(static_cast<uint32_t>(codepoint));
    unicodeBufferPos = 0;
    unicodeHighSurrogate = -1;
    state = STATE_IN_STRING;
}
