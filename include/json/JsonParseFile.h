// include/json/JsonParseFile.h
#pragma once

#include <Arduino.h>
#include <FS.h>

class JsonListener;

/**
 * Подача JSON из файла или PROGMEM в потоковый парсер (lib/JsonStreamingParser). См. docs/modules/json.md.
 *
 * Возвращают true, только если корневой объект/массив закрыт и синтаксических ошибок не было.
 * События слушателю приходят по ходу чтения, поэтому при false слушатель мог получить часть
 * документа: вызывающий решает, принимать ли частичный результат.
 */

/// Читает файл кусками, периодически отдавая управление (WDT).
bool jsonStreamingParseWholeFile(File& file, JsonListener& listener);

/// Строка JSON во flash (PROGMEM), завершённая нулём.
bool jsonStreamingParseProgmem(JsonListener& listener, const char* pgmPtr);
