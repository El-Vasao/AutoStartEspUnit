/**
 * @file WebRamAssets.h
 * @brief Отдача файлов UI без LittleFS в колбэке async.
 *
 * Мелочь (gzip оболочки кроме app.js, схемы до 16 КБ) копируется в кучу на цикле.
 * app.js.gz (~122 КБ) и прочие крупные файлы читаются окном с цикла: колбэк только копирует RAM.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

class AsyncWebServerRequest;
class AsyncWebServerResponse;

namespace web_ram {

enum class Take : uint8_t { Ready, Missing, Unavailable };

/// Насос и предзагрузка. Только из цикла (`WebServer::update`).
void tick();

/**
 * Ответ из RAM или поток, который читает ФС на цикле.
 * `*out` заполнен только при Ready; заголовки кэша добавляет вызывающий до `send`.
 * Вызывать только из задачи async.
 */
Take acquire(AsyncWebServerRequest* request, const char* path, const char* contentType, bool gzip,
             AsyncWebServerResponse** out);

} // namespace web_ram
