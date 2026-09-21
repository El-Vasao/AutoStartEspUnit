# `common`: Logger и устойчивость hot-path

## Logger (SSE-only)
Файлы:
- `include/common/Logger.h`
- `src/common/Logger.Core.cpp`

Политика:
- Логи идут в UI через SSE (UART0 занят GSM).
- Дроп при перегрузке — на стороне **SSE soft/OTA queue gates** в `WebServer`
  (встроенного token-bucket throttle в `Logger` нет).

Цель: **лучше потерять часть логов, чем повесить цикл** при активной UI-сессии и GSM churn.

# `common`: константы, буферы, логирование, утилиты

## `Constants.h`
Единая точка лимитов:
- размеры строк (`TextBytes::*`)
- лимиты JSON (`JsonBytes::*`)
- тайминги (`Timing::*`)
- лимиты SSE очередей (`WebSseLimits::*`)
- FS/OTA лимиты

Идея: лимиты — часть архитектуры; менять централизованно в `Constants.h`.

## `Logger`
Централизованный вывод:
- форматирование в фиксированный буфер
- доставка в UI через SSE (без хранения истории на устройстве)

## `Utils`
CRC, таймеры, безопасные helper’ы без динамики.

