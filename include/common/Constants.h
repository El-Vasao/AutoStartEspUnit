// include/common/Constants.h
#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include "common/Pins.h"

/**
 * @file Constants.h
 * @brief Глобальные константы/лимиты прошивки.
 * 
 * Принципы:
 * - аппаратные лимиты берём строго из `Pins.h` (см. `HardwareLimits::*`);
 * - строки в конфиге — фиксированные `char[]` (без `String`) → лимиты задаём в байтах, включая '\0';
 * - лимиты JSON/буферов держим небольшими ради предсказуемости RAM на ESP8266.
 */

// ============================================================
// DS18B20 (температурные датчики)
// ============================================================
namespace DS18B20 {
    /// В DallasTemperature это значение равно -127°C (DEVICE_DISCONNECTED_C).
    /// Важно: держим константу локально, чтобы `Constants.h` не тянул тяжёлые заголовки Dallas/OneWire
    /// по всему проекту (compile fanout + лишняя связанность).
    constexpr float DISCONNECTED = -127.0f; ///< значение “датчик отсутствует”
    constexpr uint8_t RESOLUTION = 9;                     ///< 9..12 бит; 9 = быстрее и стабильнее по времени
    constexpr uint16_t CONVERSION_TIMEOUT_MS = 1000;      ///< таймаут конвертации (мс)
    constexpr uint32_t READ_INTERVAL_MS = 5000;           ///< период опроса (мс)
}

// ============================================================
// Аппаратные лимиты (истина = Pins.h / namespace Pin)
// ============================================================
namespace HardwareLimits {
    /// Количество реле = размер массива `Pin::RELAY_PINS`.
    constexpr uint8_t RELAYS = sizeof(Pin::RELAY_PINS) / sizeof(Pin::RELAY_PINS[0]);

    /// Количество цифровых входов = размер массива `Pin::INPUT_PINS`.
    constexpr uint8_t INPUTS = sizeof(Pin::INPUT_PINS) / sizeof(Pin::INPUT_PINS[0]);

    /// Максимальное количество датчиков температуры (задаётся в `Pins.h`).
    constexpr uint8_t SENSORS = Pin::MAX_SENSORS;
}

// ============================================================
// Общесистемные лимиты (не зависят от платы напрямую)
// ============================================================
namespace Limits {
    /// Максимальный размер `/config.json` (байт).
    constexpr size_t CONFIG_JSON_SIZE = 4096;

    constexpr uint8_t MAX_STEPS_PER_PROGRAM = 15;
    constexpr uint8_t MAX_TRIGGERS = 8;
    constexpr uint8_t MAX_PROGRAMS = 10;

    /// Пароль SoftAP WPA2-PSK: 8..63 символа (здесь фиксируем только максимум).
    constexpr uint8_t MAX_PASSWORD_LEN = 63;
}

// ============================================================
// Лимиты строк/буферов (в байтах, включая '\0' для C-строк)
// ============================================================
namespace TextBytes {
    namespace Wifi {
        /// 32 символа + '\0' (802.11 SSID).
        constexpr size_t SSID = 33;
        /// 64 символа + '\0' (пароль WPA2-PSK).
        constexpr size_t PASSWORD = 65;
    }

    namespace Gsm {
        constexpr size_t APN = 33;
        constexpr size_t APN_USER = 33;
        constexpr size_t APN_PASS = 33;
        constexpr size_t PHONE = 20;
        constexpr size_t OPERATOR = 20;
        constexpr size_t DTMF_PASSWORD = 9;
    }

    namespace Mqtt {
        constexpr size_t BROKER = 64;
        constexpr size_t CLIENT_ID = 33;
        constexpr size_t USER = 33;
        constexpr size_t PASS = 33;
        constexpr size_t TOPIC = 65;
    }

    namespace Vehicle {
        /// Строка-режим: "voltage" или "input"
        constexpr size_t ENGINE_DETECTION_SOURCE = 8;
    }

    namespace Sensors {
        constexpr size_t NAME = 16;
        /// "AA:BB:CC:DD:EE:FF:11:22" + '\0'
        constexpr size_t ROM = 24;
        /// Тот же формат, что и ROM
        constexpr size_t ADDR_STRING = 24;
    }

    namespace Inputs {
        constexpr size_t NAME = 16;
        /// "IN123" и т.п.
        constexpr size_t FALLBACK_NAME = 8;
    }

    namespace Triggers {
        /// "above" / "below" (сравнение)
        constexpr size_t COMPARISON = 8;
    }

    namespace Programs {
        /// "RELAY_PULSE_ON_OFF_ON" и т.п.
        constexpr size_t STEP_ACTION = 24;
        constexpr size_t NAME = 32;
    }
}

namespace BufferBytes {
    namespace Web {
        constexpr size_t IP_STRING = 16;
        constexpr size_t URL = 96;
    }

    namespace Reset {
        constexpr size_t REASON = 64;
    }

    namespace Fs {
        constexpr size_t PATH = 64;
        constexpr size_t TEMP_PATH = 96;
        constexpr size_t GZIP_PATH = 128;
        constexpr size_t PROGRAM_PATH = 32;
        constexpr size_t WEB_ASSET_PATH = 48;
        constexpr size_t WEB_ASSET_GZIP_PATH = 52;
    }

    namespace Config {
        constexpr size_t KEY = 16;
    }
}

// ============================================================
// Тайминги системы (все в миллисекундах)
// ============================================================
namespace Timing {
    constexpr uint32_t TRIGGER_CHECK_INTERVAL_MS = 50;
    constexpr uint32_t VOLTAGE_READ_INTERVAL_MS = 200;
    constexpr uint32_t TEMPERATURE_READ_INTERVAL_MS = DS18B20::READ_INTERVAL_MS;
    constexpr uint32_t DEBOUNCE_DELAY_MS = 50;
    constexpr uint32_t PULSE_COUNTER_INTERVAL_MS = 100;

    constexpr uint32_t WATCHDOG_FEED_INTERVAL_MS = 1000;
    constexpr uint32_t FS_MAINTENANCE_INTERVAL_MS = 3600000; // 1 час
    constexpr uint32_t ERROR_REPORT_INTERVAL_MS = 30000;
    /// Минимальная частота yield/delay(0) в длинных циклах (ESP8266 WiFi/lwIP + soft WDT).
    constexpr uint32_t COOPERATE_INTERVAL_MS = 20;
    /// Период «диффа» SSE: сравнение блоков железа/режима без обязательной отправки каждого.
    /// 1000 ms: меньше churn/queue pressure на ESP8266 при открытом UI (было 500).
    constexpr uint32_t SSE_STATUS_INTERVAL_MS = 1000;
    /// Период лёгкого события `clocks`: uptime и таймер программы для синхронизации UI.
    constexpr uint32_t SSE_CLOCKS_INTERVAL_MS = 1000;

    constexpr uint32_t OTA_WAIT_LOG_INTERVAL_MS = 5000;
    /// HTTP upload → OTA: если за это время не пришёл «final» multipart, выходим в NORMAL и чистим `/update.bin`.
    constexpr uint32_t OTA_HTTP_UPLOAD_IDLE_MS = 30000;

    constexpr uint32_t MILLIS_PER_DAY = 86400000UL;
}

// ============================================================
// LittleFS: запасы/лимиты записи (важно для atomic write/rename)
// ============================================================
namespace FSystem {
    /// Минимальный запас свободного места перед GC/записью (байт).
    constexpr size_t MIN_FREE_SPACE = 4096;
    /// Запас под atomic write/rename (байт).
    constexpr size_t GC_SPACE_MARGIN = 1024;
    /// Запас под потоковую запись (байт).
    constexpr size_t STREAM_SPACE_MARGIN = 4096;
}

// ============================================================
// OTA (прошивка и сопутствующие файлы)
// ============================================================
namespace OTA {
    constexpr uint32_t BUFFER_SIZE = 256;
    constexpr uint32_t HEADER_SIZE = 4;      ///< little-endian fwSize (4 байта)
    constexpr size_t FILE_MAX_SIZE = 524288; ///< максимальный размер `/update.bin` на ФС
}

// ============================================================
// ADC (измерение напряжения)
// ============================================================
namespace ADC {
    /// ESP32-C3 ADC with attenuation — calibrate later against known battery voltage.
    constexpr float VREF = 3.3f;
    constexpr uint16_t MAX_RAW = 4095;   ///< 12-bit ADC (0..4095)
    constexpr uint8_t SAMPLES = 10;      ///< окно усреднения
    constexpr uint8_t DIVIDER_RATIO = 15;
    /// Approx if attenuation set so ~1 V at divider output ≈ full scale; needs field cal.
    /// Physical divider still outputs ~0–1 V into ADC (see SensorsController::begin).
    constexpr float DEFAULT_COEFF = (1.0f * DIVIDER_RATIO) / 4095.0f;
}

// ============================================================
// SoftAP: параметры по умолчанию
// ============================================================
namespace APConfig {
    constexpr char SSID_PREFIX[] = "AutoStart-";
    constexpr char DEFAULT_PASSWORD[] = "12345678";
    constexpr uint8_t CHANNEL = 1;
    constexpr uint8_t HIDDEN = 0;
    /// ESP32-C3 SoftAP: allow a few concurrent STA (was 1 on ESP8266 RAM gates).
    constexpr uint8_t SETUP_MAX_CONNECTIONS = 4;
    /// В NORMAL допускаем несколько станций (ESP32-C3 имеет больше RAM).
    constexpr uint8_t NORMAL_MAX_CONNECTIONS = 4;
    /// Legacy alias для мест, где нужна compile-time константа.
    constexpr uint8_t MAX_CONNECTIONS = NORMAL_MAX_CONNECTIONS;
}

namespace WebCache {
    /// Можно кэшировать в памяти, но с обязательной ревалидацией (без “вечного” кэша между прошивками).
    constexpr char SESSION_REVALIDATE[] = "private, max-age=0, must-revalidate";
}

// ============================================================
// Режимы работы системы
// ============================================================
enum class CoreMode : uint8_t {
    BOOT = 0,          ///< Стартовый режим (инициализация)
    EMERGENCY_AP,      ///< Аварийная точка доступа (не удалось загрузить конфиг)
    SETUP_AP,          ///< Режим настройки (точка доступа с SSID из конфига)
    NORMAL,            ///< Нормальный режим (всё активно)
    NORMAL_SILENT,     ///< Нормальный режим без WIFI
    OTA_UPDATE         ///< Режим обновления прошивки
};

// ============================================================
// Heap: пороги (ESP32-C3 — legacy frag auto-restart removed)
// ============================================================
namespace ValidationLimits {
    /// Legacy; auto-restart on frag removed for ESP32-C3.
    constexpr uint8_t HEAP_FRAG_THRESHOLD = 95;     ///< % (unused)
    constexpr uint32_t MIN_HEAP_BLOCK_SIZE = 4096;  ///< байт (unused for reboot)
}

// ============================================================
// Строковые константы (имена режимов и т.п.)
// ============================================================
namespace StateStrings {
    constexpr const char* MODE_BOOT          = "boot";
    constexpr const char* MODE_EMERGENCY     = "emergency_ap";
    constexpr const char* MODE_SETUP         = "setup_ap";
    constexpr const char* MODE_NORMAL        = "normal";
    constexpr const char* MODE_NORMAL_SILENT = "normal_silent";
    constexpr const char* MODE_OTA           = "ota_update";
    constexpr const char* MODE_REBOOT_REQUIRED = "reboot_required";
}

// ============================================================
// Параметры логирования
// ============================================================
namespace Logging {
    constexpr size_t MAX_MESSAGE_LENGTH = 256;   ///< включая '\0'
}

// ============================================================
// JSON: лимиты размеров на wire / статические буферы (парсинг через vendored JsonStreamingParser в lib/; единого JsonDocument в прошивке нет).
// ============================================================
namespace JsonBytes {
    /// Максимум байт для «больших» JSON как файлы конфигурации и программы, POST и atomic write (`Limits::CONFIG_JSON_SIZE`).
    constexpr size_t MAX_FILE_JSON_BYTES = Limits::CONFIG_JSON_SIZE;

    namespace Web {
        /// Максимальный размер сериализованного SSE payload для `/events` ("data: ...\n\n").
        /// Лимит BSS для SSE (только incremental: hardware/program/... — полный live в GET /bootstrap).
        /// Прежний monolithic статус был ~782 B; худший блок — hardware с tempSensors; 800 + малая маржа.
        constexpr size_t SSE_STATUS_JSON_MAX = 832;
        constexpr size_t API_RESPONSE_JSON_MAX = 1024;
        /// Cap for buffered API JSON (`sendJsonBuffered`), incl. merged `/bootstrap`.
        constexpr size_t BOOTSTRAP_JSON_MAX = 4096;
        /// Устаревшее имя: бюджет под большой ответ тем же порядком, что `MAX_FILE_JSON_BYTES`.
        constexpr size_t DOC_CAPACITY = MAX_FILE_JSON_BYTES;
        constexpr size_t SSE_STATUS_DOC_CAPACITY = DOC_CAPACITY;
        constexpr size_t API_RESPONSE_DOC_CAPACITY = DOC_CAPACITY;
    }

    namespace Mqtt {
        constexpr size_t STATUS_DOC_CAPACITY = 448;
        constexpr size_t CMD_DOC_CAPACITY = 384;
        constexpr size_t CMD_JSON_MAX = 256; ///< макс. размер входящей JSON-команды (payload) + '\0'
        /// Лимит тела MQTT PUBLISH (JSON) при `MqttFsmClient::TX_MAX=448` и длине топика до `TextBytes::Mqtt::TOPIC-1`.
        constexpr uint16_t STATUS_PAYLOAD_MAX_BYTES = 376;
        constexpr size_t STATUS_JSON_MAX = 448;
        constexpr size_t LIST_PROGRAMS_JSON_MAX = 420;
        constexpr size_t LIST_PROGRAMS_DOC_CAPACITY = MAX_FILE_JSON_BYTES;
    }

    namespace Programs {
        constexpr size_t INDEX_FILTER_DOC_CAPACITY = 64;
    }
}

namespace PoolLimits {
    /// Кусок “scratch” для FS операций (держим небольшим ради RAM/стека).
    constexpr size_t FS_SCRATCH_BYTES = 256;
}

namespace NetTiming {
    constexpr uint32_t MQTT_RECONNECT_INTERVAL_MS = 5000;
    /// Ожидание CONNACK после CONNECT на GSM (секунды RTT + очередь оператора).
    constexpr uint32_t MQTT_FSM_CONNECT_TIMEOUT_MS = 30000;
}

// ============================================================
// Web/DNS/AP (captive portal)
// ============================================================
namespace HttpPostJson {
    constexpr const char* TMP_CONFIG = "/__http/cfg.tmp";
    constexpr const char* TMP_PROGRAM = "/__http/prg.tmp";
    /// Единый лимит на размер POST-body и scratch-буферы.
    constexpr size_t MAX_BYTES = Limits::CONFIG_JSON_SIZE;
}

namespace WebConfig {
    constexpr uint16_t HTTP_PORT = 80;
    constexpr uint16_t DNS_PORT = 53;

    /// Подсеть SoftAP по умолчанию (классическая для ESP: 192.168.4.1/24).
    /// `static` даёт internal linkage: в каждом TU будет своя копия, но без ODR-рисков.
    static const IPAddress AP_IP(192, 168, 4, 1);
    static const IPAddress AP_NETMASK(255, 255, 255, 0);
}

namespace WebUi {
    /// Фиксированный пул UI-сессий (без динамических аллокаций).
    constexpr uint8_t MAX_UI_SESSIONS = APConfig::MAX_CONNECTIONS * 2;
    constexpr uint32_t HEARTBEAT_INTERVAL_MS = 5000;
    /// Таймаут должен быть > heartbeat (мобильные браузеры могут “подвисать” на фоне).
    constexpr uint32_t SESSION_TIMEOUT_MS = 60000;
}

// ============================================================
// SSE: ограничения очередей (защита lwIP/RAM).
// Пер-клиентская глубина очереди сообщений задаётся в ESPAsyncWebServer макросом
// SSE_MAX_QUEUED_MESSAGES (см. platformio.ini); здесь только наши пороги на отправку.
// ============================================================
namespace WebSseLimits {
    /// Максимум одновременных SSE `/events` клиентов (ESP32-C3).
    constexpr uint8_t MAX_SSE_CLIENTS = 4;
    /// Общий мягкий порог avgPacketsWaiting() перед send (SSE log и incremental статус одной очередью).
    /// Держать ниже SSE_MAX_QUEUED_MESSAGES (platformio.ini), иначе soft gate бесполезен.
    constexpr size_t SSE_SOFT_QUEUE_MAX = 8;
    constexpr size_t STATUS_QUEUE_MAX = SSE_SOFT_QUEUE_MAX;
    constexpr size_t STATUS_FORCE_QUEUE_MAX = 8;
    /// Пред-OTA окно (идёт upload, но режим OTA ещё не активен): пороги ниже, чтобы освободить heap для Update.begin.
    constexpr size_t OTA_PREP_STATUS_QUEUE_MAX = 4;
    constexpr size_t OTA_PREP_LOG_QUEUE_MAX = 2;
}

// ============================================================
// Web assets (LittleFS) — список обязательных файлов UI
// ============================================================
namespace WebAssets {
    /// Logical URL paths; on LittleFS the bytes live only as `<path>.gz` (build.py axiom).
    static const char INDEX_HTML[] PROGMEM = "/index.html";
    static const char STYLE_CSS[] PROGMEM = "/style.css";
    static const char APP_JS[] PROGMEM = "/app.js";
    static const char* const REQUIRED[] PROGMEM = {
        INDEX_HTML,
        STYLE_CSS,
        APP_JS,
    };

    constexpr size_t REQUIRED_COUNT = sizeof(REQUIRED) / sizeof(REQUIRED[0]);
}

// ============================================================
// Задержки (delay) в “железных” переходах состояний
// ============================================================
namespace Delays {
    constexpr uint32_t WIFI_DISCONNECT_SETTLE_MS = 50;
    constexpr uint32_t WIFI_OFF_SETTLE_MS = 200;

    constexpr uint32_t WEBSERVER_STOP_SETTLE_MS = 200;
    constexpr uint32_t WEBSERVER_MODE_SWITCH_MS = 100;
    constexpr uint32_t WEBSERVER_AP_START_SETTLE_MS = 50;

    constexpr uint32_t OTA_MODE_SWITCH_MS = 100;
    constexpr uint32_t REBOOT_HTTP_REPLY_MS = 100;
}

// ============================================================
// GSM (SIM800): тайминги/лимиты автомата
// ============================================================
namespace GSM {
    constexpr uint32_t BOOT_DELAY_MS = 1000;
    constexpr uint8_t MAX_RETRIES = 3;

    /// Целевая скорость UART MCU↔модем (гипотеза по умолчанию; проверка/запись NV через `AT+IPR?` / `AT+IPR=`).
    constexpr uint32_t UART_BAUD = 115200;
    /// Пауза после `Serial.begin` на старте и при возврате к гипотезе между раундами поиска скорости.
    constexpr uint32_t UART_SETTLE_MS = 80;
    /// Старт отсчёта — с первой отправки `AT` на `UART_BAUD` в INIT (после quiet window). До истечения не включаем поиск альтернативных baud.
    constexpr uint32_t BAUD_FALLBACK_AFTER_MS = 60000;
    /// Пауза между полными неудачными проходами таблицы скоростей (антиспам модема и loop).
    constexpr uint32_t BAUD_SEARCH_ROUND_COOLDOWN_MS = 30000;
    /// Задержка после `Serial.begin(rate)` на каждой пробе в режиме поиска baud.
    constexpr uint32_t BAUD_SEARCH_SETTLE_PER_BAUD_MS = 120;

    constexpr uint32_t AT_OK_TIMEOUT_MS = 5000;
    /// INIT: `AT+CGATT?` до готовности SIM часто даёт `+CME ERROR: SIM busy` — пауза между повторами.
    constexpr uint32_t INIT_CGATT_RETRY_DELAY_MS = 2000;
    constexpr uint8_t INIT_CGATT_RETRY_MAX = 8;
    /// Таймаут `AT+CGMI` (шаг подтверждения производителя); отдельное имя для тюнинга (число по умолчанию = AT_OK_TIMEOUT_MS).
    constexpr uint32_t MODEM_ID_VERIFY_TIMEOUT_MS = AT_OK_TIMEOUT_MS;
    /// Опционально: принудительный фоллбэк после длительной тишины на RX (0 = выкл.; не используется без кода в INIT).
    constexpr uint32_t BAUD_FALLBACK_SILENCE_FORCE_MS = 0;
    /// Лимит неудачных раундов поиска baud подряд (0 = без лимита).
    constexpr uint8_t BAUD_SEARCH_MAX_PASSES = 0;
    /// Подстрока в ответе на `AT+CGMI` (ожидаемый производитель; SIM800 семейство).
    inline constexpr char MODEM_VERIFY_MANUFACTURER_SUBSTR[] = "SIMCOM";
    /// Логировать URC `+CSQ` только если RSSI изменился не меньше чем на это значение (шкала 0..31).
    constexpr int16_t URC_RSSI_LOG_DELTA = 3;

    constexpr uint32_t REG_TIMEOUT_MS = 60000;
    constexpr uint32_t APN_TIMEOUT_MS = 10000;
    constexpr uint32_t GPRS_ATTACH_TIMEOUT_MS = 15000;
    constexpr uint32_t PDP_ACTIVATE_TIMEOUT_MS = 20000;
    constexpr uint32_t GET_IP_TIMEOUT_MS = 10000;

    constexpr uint32_t READY_POLL_INTERVAL_MS = 10000;
    // READY diagnostics should be rare: do not constantly poke the modem.
    constexpr uint32_t READY_SIGNAL_INTERVAL_MS = 300000;   // 5 min
    constexpr uint32_t READY_OPERATOR_INTERVAL_MS = 900000; // 15 min
    constexpr uint32_t READY_TCP_STATS_INTERVAL_MS = 60000; // 1 min
    constexpr uint32_t TCP_CLOSED_REATTACH_WINDOW_MS = 60000; // 1 min
    constexpr uint8_t TCP_CLOSED_REATTACH_THRESHOLD = 3;      // 3 drops within window
    constexpr uint32_t ERROR_RECOVERY_DELAY_MS = 30000;

    // RX ring for waiter/diagnostic snippets. Keep compact to save RAM.
    constexpr size_t RESPONSE_BUFFER_SIZE = 128;
    constexpr size_t CMD_BUFFER_SIZE = 128;
}

// ============================================================
// SIM800 TCP transport (URC-first): таймбюджеты/лимиты
// ============================================================
namespace Sim800Tcp {
    /// Дополнительные строки TCP (+IPD, часть URC) в лог; без `SERIAL_DEBUG` по умолчанию выкл.
    constexpr bool TCP_VERBOSE_LOG = false;

    // Budget for waiting TCP CONNECT OK (URC) after CIPSTART.
    // Must be <= broker/network worst-case, but not too high to avoid long blocks.
    constexpr uint32_t CONNECT_TIMEOUT_MS = 15000;

    // AT acceptance timeout for CIPSTART command (OK/ERROR), actual connect is via URC.
    constexpr uint32_t CIPSTART_ACCEPT_TIMEOUT_MS = 15000;

    // Micro-budget for helping modem progress after write() (to avoid long stalls during MQTT handshake).
    constexpr uint32_t WRITE_PUMP_BUDGET_MS = 50;
}

// ============================================================
// FS метаданные (ожидаемые максимальные размеры файлов)
// ============================================================
namespace FileMax {
    /// index.html на FS (CSS отдельно в style.css).
    constexpr uint32_t INDEX_HTML = 262144;
    constexpr uint32_t STYLE_CSS = 262144;
    /// Bundled Alpine+app JS (несжатый); на FS обычно лежит .gz.
    constexpr uint32_t APP_JS = 786432;
    constexpr uint32_t FAVICON_ICO = 2048;
}

// ============================================================
// Время/математика: коэффициенты пересчёта (частоты и т.п.)
// ============================================================
namespace Time {
    constexpr uint32_t MS_PER_SEC = 1000UL;
    constexpr float MS_PER_SEC_F = 1000.0f;
    constexpr float SEC_PER_MIN_F = 60.0f;
}

