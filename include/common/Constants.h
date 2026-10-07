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
 * - лимиты JSON/буферов держим предсказуемыми (ESP32-C3 heap ~170 KB free after boot).
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
    constexpr size_t CONFIG_JSON_SIZE = 5120;

    constexpr uint8_t MAX_STEPS_PER_PROGRAM = 15;
    constexpr uint8_t MAX_TRIGGERS = 8;
    constexpr uint8_t MAX_SCHEDULE_TRIGGERS = 8;
    constexpr uint8_t MAX_PROGRAMS = 10;

    /// Пароль SoftAP WPA2-PSK: 8..63 символа (здесь фиксируем только максимум).
    constexpr uint8_t MAX_PASSWORD_LEN = 63;
}

/// MQTT/SSE error history ring (undelivered one-shot delivery).
namespace ErrorHistory {
    constexpr uint8_t CAPACITY = 5;
    constexpr size_t MSG_MAX = 40; ///< enough for errorCodeToString
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
        /// Full derived topic `{prefix}/{suffix}` incl. NUL.
        constexpr size_t TOPIC = 65;
        /// User-configured prefix only; leave room for longest suffix `/status`.
        constexpr size_t TOPIC_PREFIX = 48;
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
        /// SMS_OWNER step text (GSM-7/ASCII) + NUL.
        constexpr size_t STEP_MESSAGE = 65;
    }

    namespace TimeCfg {
        /// NTP host for AT+CNTP (SIM800).
        constexpr size_t NTP_SERVER = 48;
    }
}

/// Fixed MQTT topic suffixes under `mqtt.topic_prefix` (not user-configurable).
namespace MqttTopics {
    constexpr const char* AVAIL = "avail";
    constexpr const char* STATUS = "status";
    constexpr const char* CMD = "cmd";
    constexpr const char* REPLY = "reply";
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
    constexpr uint32_t TRIGGER_CHECK_INTERVAL_IDLE_MS = 200;
    constexpr uint32_t VOLTAGE_READ_INTERVAL_MS = 200;
    constexpr uint32_t VOLTAGE_READ_INTERVAL_IDLE_MS = 1000;
    constexpr uint32_t TEMPERATURE_READ_INTERVAL_MS = DS18B20::READ_INTERVAL_MS;
    constexpr uint32_t TEMPERATURE_READ_INTERVAL_IDLE_MS = 15000;
    constexpr uint32_t DEBOUNCE_DELAY_MS = 50;
    constexpr uint32_t PULSE_COUNTER_INTERVAL_MS = 100;

    constexpr uint32_t WATCHDOG_FEED_INTERVAL_MS = 1000;
    constexpr uint32_t FS_MAINTENANCE_INTERVAL_MS = 3600000; // 1 час
    constexpr uint32_t ERROR_REPORT_INTERVAL_MS = 30000;
    /// Periodic heap log cadence (tagged snapshots stay event-driven).
    constexpr uint32_t HEAP_SNAPSHOT_INTERVAL_MS = 300000; // 5 min
    /// Skip periodic heap log unless free or maxBlock moved by at least this.
    constexpr uint32_t HEAP_SNAPSHOT_DELTA_BYTES = 4096;
    /// Минимальная частота yield в длинных циклах (FreeRTOS fairness + lwIP).
    constexpr uint32_t COOPERATE_INTERVAL_MS = 20;
    /// Период «диффа» SSE: сравнение блоков железа/режима без обязательной отправки каждого.
    /// 500 ms: ESP32-C3 SoftAP headroom (было 1000 на ESP8266 queue pressure).
    constexpr uint32_t SSE_STATUS_INTERVAL_MS = 500;
    /// Период лёгкого события `clocks`: uptime и таймер программы для синхронизации UI.
    constexpr uint32_t SSE_CLOCKS_INTERVAL_MS = 1000;

    constexpr uint32_t OTA_WAIT_LOG_INTERVAL_MS = 5000;
    /// SoftAP stream OTA: idle without multipart final (large ~1.2 MB packages).
    constexpr uint32_t OTA_HTTP_UPLOAD_IDLE_MS = 180000;

    constexpr uint32_t MILLIS_PER_DAY = 86400000UL;

    /// TimeSync: first fail-retry delay (also backoff step 0).
    constexpr uint32_t TIME_SYNC_RETRY_MS = 120000;
    /// Fail-retry backoff steps after step 0 (2 → 5 → 15 → 30 min cap).
    constexpr uint32_t TIME_SYNC_BACKOFF_5MIN_MS = 300000UL;
    constexpr uint32_t TIME_SYNC_BACKOFF_15MIN_MS = 900000UL;
    constexpr uint32_t TIME_SYNC_BACKOFF_30MIN_MS = 1800000UL;
    constexpr uint8_t TIME_SYNC_BACKOFF_MAX_STEP = 3;
    /// CCLK-lite misses while MQTT up before forcing a full drain cascade.
    constexpr uint8_t TIME_SYNC_LITE_BEFORE_HEAVY = 3;
    /// Short re-arm when CCLK-lite cannot start (AT bus busy mid-CIPSEND).
    constexpr uint32_t TIME_SYNC_LITE_BUSY_RETRY_MS = 5000;
    /// UI/status: mark sync stale after this age (still keep soft clock).
    constexpr uint32_t TIME_SYNC_STALE_MS = 172800000UL; // 48 h

    /// Throttle for "flash busy, defer FS op" log lines (Core + FlashCommitCoordinator).
    constexpr uint32_t DEFERRED_FLASH_BUSY_LOG_MS = 2000UL;
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
    /// Max app image size = dual-OTA slot (`partitions.csv` app0/app1 = 0x140000).
    constexpr uint32_t APP_IMAGE_MAX = 0x140000;
    /// Cap for streamed OTA package (fw + FS tail); below LittleFS partition 0x170000.
    constexpr size_t FILE_MAX_SIZE = 0x160000;
    /// Free-space floor before stream: room for largest gzip UI asset + margin (not full package).
    constexpr size_t STREAM_FS_HEADROOM = 262144;
}

// ============================================================
// ADC (измерение напряжения)
// ============================================================
namespace ADC {
    /// ESP32-C3 ADC with attenuation — calibrate later against known battery voltage.
    constexpr float VREF = 3.3f;
    constexpr uint8_t RESOLUTION_BITS = 12;
    constexpr uint16_t MAX_RAW = (1u << RESOLUTION_BITS) - 1u; ///< 12-bit ADC (0..4095)
    constexpr uint8_t SAMPLES = 10;      ///< окно усреднения
    constexpr uint8_t DIVIDER_RATIO = 15;
    /// Approx if attenuation set so ~1 V at divider output ≈ full scale; needs field cal.
    /// Physical divider still outputs ~0–1 V into ADC (see SensorsController::begin).
    constexpr float DEFAULT_COEFF = (1.0f * DIVIDER_RATIO) / static_cast<float>(MAX_RAW);
}

/// DS18B20 / ADC fault escalation (SensorsController).
namespace Sensors {
    constexpr uint8_t OW_TIMEOUT_FAIL_STREAK = 3;
    constexpr uint8_t ADC_SAT_FAIL_STREAK = 8;
}

/// ProgramExecutor action timings (not from config).
namespace ProgramTiming {
    /// Pause between starter retry attempts (phase 3).
    constexpr uint32_t STARTER_RETRY_GAP_MS = 1000UL;
    /// CALL_OWNER: ring duration below this uses modem default (`GSM::VOICE_DEFAULT_RING_MS`).
    constexpr uint32_t CALL_OWNER_MIN_RING_MS = 3000UL;
}

/// Soft wall-clock sanity (reject pre-~2023-11 UTC).
namespace TimeSync {
    constexpr long MIN_SANE_EPOCH_UTC = 1700000000L;
    constexpr int MIN_SANE_YEAR = 2023;
}

/// Factory / SAX fallback defaults (must match DefaultConfig.h intent).
namespace Defaults {
    constexpr uint16_t MQTT_PUBLISH_INTERVAL_SEC = 60;
    constexpr uint8_t TIME_SYNC_INTERVAL_HOURS = 6;
}

// ============================================================
// SoftAP: параметры по умолчанию
// ============================================================
namespace APConfig {
    constexpr char SSID_PREFIX[] = "AutoStart-";
    constexpr char DEFAULT_PASSWORD[] = "12345678";
    constexpr uint8_t CHANNEL = 1;
    constexpr uint8_t HIDDEN = 0;
    /// SoftAP TX cap: Arduino `WIFI_POWER_8_5dBm` (0.25 dBm units).
    constexpr int8_t TX_POWER = 34;
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
        /// Cap for buffered API JSON (`sendJsonBuffered`), incl. merged `/bootstrap`.
        constexpr size_t BOOTSTRAP_JSON_MAX = 4096;
    }

    namespace Mqtt {
        constexpr size_t CMD_JSON_MAX = 256; ///< макс. размер входящей JSON-команды (payload) + '\0'
        /// Лимит тела MQTT PUBLISH (JSON) при frame max 1024 и длине топика до `TextBytes::Mqtt::TOPIC-1`.
        /// Wire: payload + 1 (fixed) + ≤4 (RL) + 2 (topic len) + topicLen ≤ TX_MAX.
        constexpr uint16_t STATUS_PAYLOAD_MAX_BYTES = 900;
        constexpr size_t LIST_PROGRAMS_JSON_MAX = 960; ///< list reply: envelope + programs array
        /// status cmd fat /reply: id+code + nested status object (≤ STATUS_PAYLOAD_MAX_BYTES).
        constexpr size_t STATUS_REPLY_JSON_MAX = 960;
        /// Voltage change below this does not count as significant / does not enter a delta.
        constexpr float STATUS_VOLTAGE_EPS = 0.05f;
        /// Temperature change below this does not count as significant / does not enter a delta.
        constexpr float STATUS_TEMP_EPS = 0.1f;
        /// freeHeap change below this does not count as significant / does not enter a delta.
        constexpr uint32_t STATUS_HEAP_EPS = 2048;
        /// timerRemaining (sec) change below this does not count as significant (unless running flips).
        constexpr uint32_t STATUS_TIMER_EPS_SEC = 1;
    }
}

/// MQTT cmd/reply queue limits (wire contract in docs/modules/mqtt.md).
/// No idempotency LRU on device — client may retry the same `id`.
namespace MqttCmd {
    constexpr uint8_t INBOUND_DEPTH = 8;
    constexpr uint8_t PENDING_REPLY_DEPTH = 4;
    constexpr uint8_t ID_MAX_LEN = 16;

    constexpr uint16_t CODE_OK = 200;
    constexpr uint16_t CODE_ACCEPTED = 202;  ///< received / queued
    constexpr uint16_t CODE_BAD_REQUEST = 400;
    constexpr uint16_t CODE_NOT_FOUND = 404;
    constexpr uint16_t CODE_CONFLICT = 409;
    constexpr uint16_t CODE_UNPROCESSABLE = 422;
    constexpr uint16_t CODE_INTERNAL = 500;
    constexpr uint16_t CODE_UNAVAILABLE = 503; ///< inbound queue full
}

namespace PoolLimits {
    /// Кусок “scratch” для FS операций (держим небольшим ради RAM/стека).
    constexpr size_t FS_SCRATCH_BYTES = 256;
}

namespace NetTiming {
    constexpr uint32_t MQTT_RECONNECT_INTERVAL_MS = 5000;
    /// Ожидание CONNACK после CONNECT на GSM (секунды RTT + очередь оператора).
    constexpr uint32_t MQTT_FSM_CONNECT_TIMEOUT_MS = 30000;
    /// Keepalive dead-man: force Error if no MQTT RX for (keepAliveSec * NUM / DEN).
    constexpr uint16_t MQTT_KEEPALIVE_DEADMAN_NUM = 3;
    constexpr uint16_t MQTT_KEEPALIVE_DEADMAN_DEN = 2;

    /// MQTT CONNECT keepAlive field (seconds); also drives PINGREQ cadence.
    constexpr uint16_t MQTT_KEEPALIVE_SEC = 30;
    /// Delay before first status publish after session comes up.
    constexpr uint32_t MQTT_FIRST_STATUS_DELAY_MS = 1500;
    /// SUBACK wait / resubscribe timeout.
    constexpr uint32_t MQTT_SUBACK_TIMEOUT_MS = 8000;
    /// Wall budget for encode+queue of one status publish (ms).
    constexpr uint32_t MQTT_PUBLISH_BUDGET_MS = 10;
    /// MqttFsmClient TCP connect wall-clock (layered over Sim800Tcp CONNECT_TIMEOUT_MS).
    constexpr uint32_t MQTT_TCP_CONNECT_TIMEOUT_MS = 45000;

    /// One MQTT/TCP frame staging size (MqttFsm TX slot + Sim800Tcp RX/TX rings).
    constexpr uint16_t MQTT_FRAME_MAX = 1024;
    /// MqttFsmClient outbound queue depth (Ctrl reserve + Tele + in-flight).
    constexpr uint8_t MQTT_TX_Q_DEPTH = 3;
    /// RX assemble buffer (incomplete frame).
    constexpr uint16_t MQTT_RX_ASSEMBLE_MAX = 320;
    constexpr uint32_t MQTT_RX_ASSEMBLE_TIMEOUT_MS = 3000;
    /// Stack CONNECT / SUBSCRIBE payload scratch.
    constexpr uint16_t MQTT_CONNECT_PAYLOAD_MAX = 200;
    constexpr uint16_t MQTT_SUBSCRIBE_PAYLOAD_MAX = 128;

    /// Per-tick FSM budgets passed to MqttFsmClient::tick.
    constexpr uint16_t MQTT_TICK_MAX_READ_BYTES = 256;
    constexpr uint16_t MQTT_TICK_MAX_WRITE_BYTES = MQTT_FRAME_MAX;
    constexpr uint8_t MQTT_TICK_MAX_PARSE_FRAMES = 4;
    constexpr uint8_t MQTT_TICK_MAX_MS = 20;
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
    /// Мягкий порог avgPacketsWaiting() для обычных log/status (одна очередь `/events`).
    /// Hard = SSE_MAX_QUEUED_MESSAGES в platformio.ini (= soft + 1 reserved под drop-notice).
    constexpr size_t SSE_SOFT_QUEUE_MAX = 20;
    /// +1 reserved slot: soft+1 == hard; drop-notice может уйти, когда soft уже заполнен логикой soft gate.
    constexpr size_t SSE_QUEUE_RESERVED = 1;
    constexpr size_t STATUS_QUEUE_MAX = SSE_SOFT_QUEUE_MAX;
    constexpr size_t STATUS_FORCE_QUEUE_MAX = 8;
    /// How long after last SSE activity to keep diag tail logging.
    constexpr uint32_t DIAG_TAIL_MS = 5000UL;
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
    constexpr uint32_t WIFI_OFF_SETTLE_MS = 100;
    constexpr uint32_t WEBSERVER_STOP_SETTLE_MS = 100;
    constexpr uint32_t WEBSERVER_MODE_SWITCH_MS = 50;
    constexpr uint32_t WEBSERVER_AP_START_SETTLE_MS = 30;

    constexpr uint32_t OTA_MODE_SWITCH_MS = 100;
    constexpr uint32_t REBOOT_HTTP_REPLY_MS = 100;
    /// Spin delay after failed ESP.restart() (delayMicroseconds).
    constexpr uint32_t HARD_RESTART_SPIN_US = 5000;
}

// ============================================================
// GSM (SIM800): тайминги/лимиты автомата
// ============================================================
namespace GSM {
    constexpr uint32_t BOOT_DELAY_MS = 1000;
    constexpr uint8_t MAX_RETRIES = 3;

    /// Целевая скорость UART MCU↔модем (гипотеза по умолчанию; проверка/запись NV через `AT+IPR?` / `AT+IPR=`).
    constexpr uint32_t UART_BAUD = 115200;
    /// Baud search table: hypothesis first, then common SIM800 rates.
    inline constexpr uint32_t UART_BAUD_CANDIDATES[] = {
        UART_BAUD,
        57600,
        38400,
        19200,
        9600,
    };
    constexpr uint8_t UART_BAUD_CANDIDATE_COUNT =
        sizeof(UART_BAUD_CANDIDATES) / sizeof(UART_BAUD_CANDIDATES[0]);
    /// Пауза после `Serial.begin` на старте и при возврате к гипотезе между раундами поиска скорости.
    constexpr uint32_t UART_SETTLE_MS = 80;
    /// Quiet после `begin()` до первой hypothesis AT (модему нужно время после питания/рестарта ESP).
    constexpr uint32_t POST_BOOT_QUIET_MS = 5000;
    /// CellularCore: не трогать модем N мс после первого service() (питание/USB/SoftAP стабилизация).
    constexpr uint32_t POST_BOOT_SETTLE_MS = 20000;
    /// Пауза между повторными hypothesis `AT` / `AT+CGMI` (антиспам UART при нет ответа).
    constexpr uint32_t HYP_RETRY_GAP_MS = 750;
    /// После неудачной hypothesis на `UART_BAUD` — один best-effort `AT+CFUN=1,1` до baud search.
    constexpr uint32_t PRE_CFUN_AFTER_MS = 20000;
    /// Пауза после soft-reboot модема (`CFUN=1,1`) перед повторными AT.
    constexpr uint32_t POST_CFUN_QUIET_MS = 20000;
    /// Окно повторной hypothesis на `UART_BAUD` после PreCfun; затем baud search.
    constexpr uint32_t POST_CFUN_RETRY_MS = 15000;
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
    /// Лимит неудачных раундов поиска baud подряд (0 = без лимита).
    constexpr uint8_t BAUD_SEARCH_MAX_PASSES = 4;
    /// Подстрока в ответе на `AT+CGMI` (ожидаемый производитель; SIM800 семейство).
    inline constexpr char MODEM_VERIFY_MANUFACTURER_SUBSTR[] = "SIMCOM";
    /// Логировать URC `+CSQ` только если RSSI изменился не меньше чем на это значение (шкала 0..31).
    constexpr int16_t URC_RSSI_LOG_DELTA = 3;

    constexpr uint32_t REG_TIMEOUT_MS = 60000;
    /// Cadence for AT+CREG? while modem is still searching (stat ≠ 1/5).
    constexpr uint32_t REG_POLL_INTERVAL_MS = 5000;
    constexpr uint32_t APN_TIMEOUT_MS = 10000;
    constexpr uint32_t GPRS_ATTACH_TIMEOUT_MS = 15000;
    constexpr uint32_t PDP_ACTIVATE_TIMEOUT_MS = 20000;
    constexpr uint32_t GET_IP_TIMEOUT_MS = 10000;

    constexpr uint32_t READY_SIGNAL_INTERVAL_MS = 300000;   // 5 min
    constexpr uint32_t READY_OPERATOR_INTERVAL_MS = 900000; // 15 min
    constexpr uint32_t TCP_CLOSED_REATTACH_WINDOW_MS = 60000; // 1 min
    constexpr uint8_t TCP_CLOSED_REATTACH_THRESHOLD = 3;      // 3 drops within window
    constexpr uint32_t ERROR_RECOVERY_DELAY_MS = 30000;
    constexpr uint8_t ERROR_RECOVERY_MAX_CYCLES = 3;
    /// After exhausting L1–L4 cycles, wait this long before another attempt (or until UI reboot).
    constexpr uint32_t ERROR_RECOVERY_EXHAUSTED_MS = 300000UL; // 5 min
    /// Short cooldown between ERROR recovery AT steps (CIPSHUT / SAPBR deact).
    constexpr uint32_t ERROR_RECOVERY_COOLDOWN_MS = 2000UL;
    /// AT+SAPBR=0,1 deactivate accept timeout.
    constexpr uint32_t SAPBR_DEACT_TIMEOUT_MS = 8000UL;
    /// AT+CFUN=1,1 AnyLine accept timeout during recovery.
    constexpr uint32_t CFUN_ACCEPT_TIMEOUT_MS = 1000UL;
    /// Stall-fingerprint log cadence while INIT/GPRS is stuck.
    constexpr uint32_t STALL_LOG_INTERVAL_MS = 8000UL;

    /// Reattach exponential backoff: base << (step-1), capped.
    constexpr uint32_t REATTACH_BACKOFF_BASE_MS = 5000UL;
    constexpr uint8_t REATTACH_BACKOFF_MAX_STEP = 6;
    constexpr uint32_t REATTACH_BACKOFF_MAX_MS = 180000UL;
    /// Floor delay when RSSI is weak/unknown.
    constexpr uint32_t REATTACH_WEAK_RSSI_FLOOR_MS = 60000UL;
    constexpr int16_t REATTACH_WEAK_RSSI_MAX = 6;
    constexpr int16_t REATTACH_RSSI_UNKNOWN = 99;
    /// Modem NTP (AT+CNTP): accept timeout for setup commands.
    constexpr uint32_t CNTP_AT_TIMEOUT_MS = 10000;
    /// Wait for +CNTP: URC after AT+CNTP kick.
    constexpr uint32_t CNTP_SYNC_TIMEOUT_MS = 60000;
    /// AT+CCLK? read (NITZ probe or after successful NTP).
    constexpr uint32_t CCLK_TIMEOUT_MS = 5000;
    /// AT+CIPGSMLOC=2,1 (GSM location time) — can be slow.
    constexpr uint32_t CIPGSMLOC_TIMEOUT_MS = 45000;
    /// After CIPGSMLOC OK, wait this long for late +CIPGSMLOC URC.
    constexpr uint32_t CIPGSMLOC_URC_GRACE_MS = 5000UL;
    /// Escalate ERROR recovery level if bring-up stuck this long (same order as exhausted wait).
    constexpr uint32_t BRINGUP_ESCALATE_MS = ERROR_RECOVERY_EXHAUSTED_MS;
    /// Max baud accepted when parsing AT+IPR? / IPR write.
    constexpr uint32_t UART_BAUD_PARSE_MAX = 4000000UL;
    /// Diagnostic event ring depth (GSMController).
    constexpr uint8_t EVENT_RING_SIZE = 8;
    /// AtSession normal / high-priority queue depths.
    constexpr uint8_t AT_QSIZE = 6;
    constexpr uint8_t AT_HQSIZE = 2;
    /// ModemUart framed line buffer.
    constexpr uint16_t UART_LINE_BUF = 160;

    // RX ring for waiter/diagnostic snippets. Keep compact to save RAM.
    constexpr size_t RESPONSE_BUFFER_SIZE = 128;
    constexpr size_t CMD_BUFFER_SIZE = 128;

    /// Voice / SMS exclusive modem epoch (MQTT CIP suspended).
    constexpr uint32_t VOICE_DEFAULT_RING_MS = 25000;
    constexpr uint32_t VOICE_PASSWORD_TIMEOUT_MS = 30000;
    constexpr uint32_t VOICE_PROGID_TIMEOUT_MS = 15000;
    constexpr uint32_t VOICE_CALL_CEILING_MS = 60000;
    constexpr uint32_t VOICE_CLIP_WAIT_MS = 8000;
    constexpr uint32_t SMS_CMGS_TIMEOUT_MS = 30000;
    /// Min digit suffix length used when matching CLIP vs owner_phone.
    constexpr uint8_t PHONE_MATCH_MIN_DIGITS = 10;
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

    /// Transport-side connect watchdog (from connectStart); clears stuck `_connecting`.
    /// Layered with MqttFsmClient TCP wait (~45s): this WD recovers the modem sooner.
    constexpr uint32_t CONNECT_WATCHDOG_MS = CONNECT_TIMEOUT_MS;

    /// After CIPSEND '>' / payload write: wait for SEND OK / SEND FAIL.
    constexpr uint32_t SEND_WATCHDOG_MS = 15000;

    /// Min gap between CIPSTART attempts (non-blocking Client::connect retries every MQTT tick).
    constexpr uint32_t CONNECT_RETRY_COOLDOWN_MS = 3000;

    /// Min gap between CIPSHUT recover attempts.
    constexpr uint32_t STACK_RECOVER_COOLDOWN_MS = 5000;

    /// Stack recovers without CONNECT OK before asking GSM for bearer reattach.
    constexpr uint8_t STACK_RECOVER_REATTACH_THRESHOLD = 3;

    /// Short AT accept for CIPCLOSE / CIPRXGET / CIPHEAD / CIPMUX.
    constexpr uint32_t AT_CONFIG_TIMEOUT_MS = 3000;
    /// CIPSHUT accept (transport recover + GSM ERROR recovery).
    constexpr uint32_t CIPSHUT_TIMEOUT_MS = 10000;
    /// CIPSEND wait for '>' prompt.
    constexpr uint32_t CIPSEND_PROMPT_TIMEOUT_MS = 5000;
    /// RX ring + TX staging size (must match NetTiming::MQTT_FRAME_MAX).
    constexpr uint16_t TCP_BUF_SIZE = NetTiming::MQTT_FRAME_MAX;
    /// Short CRLF control-line sniff buffer in raw push parser.
    constexpr uint8_t RAW_LINE_BUF = 64;
}

/// Boot/yield time cascade budget (defined after Sim800Tcp so CIPSHUT timeout is shared).
namespace GSM {
    constexpr uint32_t BOOT_BUDGET_SLACK_MS = 15000UL;
    constexpr uint32_t BOOT_TIME_BUDGET_MS =
        CCLK_TIMEOUT_MS + Sim800Tcp::CIPSHUT_TIMEOUT_MS + CIPGSMLOC_TIMEOUT_MS +
        CNTP_AT_TIMEOUT_MS * 3UL + CNTP_SYNC_TIMEOUT_MS + BOOT_BUDGET_SLACK_MS;
}

/// CellularCore glue (GSM + MQTT). Placed after Sim800Tcp to reuse stack-recover thresholds.
namespace Cellular {
    /// MQTT disconnect drain loop budget (gsm.update + mqtt.loop iterations).
    constexpr uint8_t MQTT_DRAIN_MAX_TICKS = 50;
    /// Cadence for "settle remaining" progress log during post-boot quiet.
    constexpr uint32_t SETTLE_LOG_INTERVAL_MS = 5000UL;
    /// Consecutive MQTT connect fails before requesting GSM bearer reattach.
    constexpr uint8_t MQTT_FAIL_STREAK_REATTACH = Sim800Tcp::STACK_RECOVER_REATTACH_THRESHOLD;
    /// Min gap between Cellular→GSM reattach requests (same order as stack recover cooldown).
    constexpr uint32_t MQTT_REATTACH_REQUEST_COOLDOWN_MS = Sim800Tcp::STACK_RECOVER_COOLDOWN_MS;
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
    constexpr uint32_t SEC_PER_HOUR = 3600UL;
    constexpr uint32_t MS_PER_HOUR = SEC_PER_HOUR * MS_PER_SEC;
}

