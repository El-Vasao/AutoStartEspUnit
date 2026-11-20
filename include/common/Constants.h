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
 * - лимиты JSON/буферов держим предсказуемыми (ESP32-C3: после старта свободно ~170 КБ кучи).
 */

// ============================================================
// DS18B20 (температурные датчики)
// ============================================================
namespace DS18B20 {
    /// В DallasTemperature это значение равно -127°C (DEVICE_DISCONNECTED_C).
    /// Важно: держим константу локально, чтобы `Constants.h` не тянул тяжёлые заголовки Dallas/OneWire
    /// по всему проекту (разрастание компиляции и лишняя связанность).
    constexpr float DISCONNECTED = -127.0f; ///< значение “датчик отсутствует”
    constexpr uint8_t RESOLUTION = 9;                     ///< 9..12 бит; 9 = быстрее и стабильнее по времени
    constexpr uint16_t CONVERSION_TIMEOUT_MS = 1000;      ///< таймаут конвертации (мс)
    constexpr uint32_t READ_INTERVAL_MS = 5000;           ///< период опроса (мс)
    /// Гистерезис отпускания термостата и температурных триггеров (°C).
    constexpr float RELEASE_HYSTERESIS_C = 0.5f;
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
    constexpr uint8_t MAX_PROGRAMS = 10;

    /// Пароль SoftAP WPA2-PSK: 8..63 символа (здесь фиксируем только максимум).
    constexpr uint8_t MAX_PASSWORD_LEN = 63;
}

/// Кольцо истории ошибок MQTT/SSE (недоставленная одноразовая доставка).
namespace ErrorHistory {
    constexpr uint8_t CAPACITY = 5;
    constexpr size_t MSG_MAX = 40; ///< хватает для errorCodeToString
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
        /// Полный производный топик `{prefix}/{suffix}` вместе с NUL.
        constexpr size_t TOPIC = 65;
        /// Только префикс из настроек; запас под самый длинный суффикс `/programs`.
        constexpr size_t TOPIC_PREFIX = 48;
    }

    namespace Vehicle {
        /// Строка-режим: "voltage" или "input"
        constexpr size_t ENGINE_DETECTION_SOURCE = 8;
    }

    namespace Sensors {
        /// Подпись датчика в UI (кириллица; 15 символов UTF-8 + NUL).
        constexpr size_t NAME = 32;
        /// "AA:BB:CC:DD:EE:FF:11:22" + '\0'
        constexpr size_t ROM = 24;
        /// Тот же формат, что и ROM
        constexpr size_t ADDR_STRING = 24;
    }

    namespace Inputs {
        /// Подпись входа в UI (кириллица; 15 символов UTF-8 + NUL).
        constexpr size_t NAME = 32;
        /// "IN123" и т.п.
        constexpr size_t FALLBACK_NAME = 8;
    }

    namespace Relays {
        /// Подпись реле в UI (кириллица; 15 символов UTF-8 + NUL).
        constexpr size_t NAME = 32;
    }

    namespace Triggers {
        /// "above" / "below" (сравнение)
        constexpr size_t COMPARISON = 8;
    }

    namespace Programs {
        /// "RELAY_PULSE_ON_OFF_ON" и т.п.
        constexpr size_t STEP_ACTION = 24;
        constexpr size_t NAME = 32;
        /// Текст шага SMS_OWNER (GSM-7/ASCII) + NUL.
        constexpr size_t STEP_MESSAGE = 65;
    }

    namespace TimeCfg {
        /// Хост NTP для AT+CNTP (SIM800).
        constexpr size_t NTP_SERVER = 48;
    }
}

/// Фиксированные суффиксы топиков MQTT под `mqtt.topic_prefix` (пользователь их не задаёт).
namespace MqttTopics {
    constexpr const char* ONLINE = "online";
    constexpr const char* STATE = "state";
    constexpr const char* PROGRAMS = "programs";
    constexpr const char* HW_MAP = "hwMap";
    constexpr const char* TRIG_MAP = "trigMap";
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

    constexpr uint32_t WATCHDOG_FEED_INTERVAL_MS = 1000;
    constexpr uint32_t FS_MAINTENANCE_INTERVAL_MS = 3600000; // 1 час
    constexpr uint32_t ERROR_REPORT_INTERVAL_MS = 30000;
    /// Период журнала кучи (помеченные снимки остаются по событиям).
    constexpr uint32_t HEAP_SNAPSHOT_INTERVAL_MS = 300000; // 5 мин
    /// Периодический журнал кучи пропускается, пока free или maxBlock не сдвинулись хотя бы на это.
    constexpr uint32_t HEAP_SNAPSHOT_DELTA_BYTES = 4096;
    /// Минимальная частота yield в длинных циклах (честность планировщика FreeRTOS и lwIP).
    constexpr uint32_t COOPERATE_INTERVAL_MS = 20;
    /// Период «диффа» SSE: сравнение блоков железа/режима без обязательной отправки каждого.
    /// 500 ms: период диффа SSE при SoftAP.
    constexpr uint32_t SSE_STATUS_INTERVAL_MS = 500;
    /// Период лёгкого события `clocks`: uptime и таймер программы для синхронизации UI.
    constexpr uint32_t SSE_CLOCKS_INTERVAL_MS = 1000;

    constexpr uint32_t OTA_WAIT_LOG_INTERVAL_MS = 5000;
    /// Потоковое OTA по SoftAP: простой без multipart final (крупные пакеты ~1.2 МБ).
    constexpr uint32_t OTA_HTTP_UPLOAD_IDLE_MS = 180000;

    constexpr uint32_t MILLIS_PER_DAY = 86400000UL;

    /// TimeSync: первая задержка повтора после неудачи (она же шаг backoff 0).
    constexpr uint32_t TIME_SYNC_RETRY_MS = 120000;
    /// Шаги backoff повтора после шага 0 (потолок 2 → 5 → 15 → 30 мин).
    constexpr uint32_t TIME_SYNC_BACKOFF_5MIN_MS = 300000UL;
    constexpr uint32_t TIME_SYNC_BACKOFF_15MIN_MS = 900000UL;
    constexpr uint32_t TIME_SYNC_BACKOFF_30MIN_MS = 1800000UL;
    constexpr uint8_t TIME_SYNC_BACKOFF_MAX_STEP = 3;
    /// Промахи CCLK-lite при поднятом MQTT, после которых форсируется полный каскад со сливом.
    constexpr uint8_t TIME_SYNC_LITE_BEFORE_HEAVY = 3;
    /// Короткий повтор, если CCLK-lite не стартовал (шина AT занята посреди CIPSEND).
    constexpr uint32_t TIME_SYNC_LITE_BUSY_RETRY_MS = 5000;
    /// UI/статус: синхронизация устарела после этого возраста (мягкие часы сохраняются).
    constexpr uint32_t TIME_SYNC_STALE_MS = 172800000UL; // 48 ч

    /// Троттлинг строк журнала «flash занят, операция ФС отложена» (Core + FlashCommitCoordinator).
    constexpr uint32_t DEFERRED_FLASH_BUSY_LOG_MS = 2000UL;
}

// ============================================================
// LittleFS: запасы/лимиты записи (важно для atomic write/rename)
// ============================================================
namespace FsSpace {
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
namespace Ota {
    constexpr uint32_t BUFFER_SIZE = 256;
    constexpr uint32_t HEADER_SIZE = 4;      ///< fwSize в little-endian (4 байта)
    /// Максимальный размер образа приложения = слот dual-OTA (`partitions.csv` app0/app1 = 0x140000).
    constexpr uint32_t APP_IMAGE_MAX = 0x140000;
    /// Потолок потокового пакета OTA (прошивка + хвост ФС); ниже раздела LittleFS 0x170000.
    constexpr size_t FILE_MAX_SIZE = 0x160000;
    /// Нижняя граница свободного места перед потоком: место под самый большой gzip-ассет UI и запас (не весь пакет).
    constexpr size_t STREAM_FS_HEADROOM = 262144;
}

// ============================================================
// ADC (измерение напряжения)
// ============================================================
namespace Adc {
    /// АЦП ESP32-C3: напряжение = raw × adc_voltage_coeff (калибровка в поле).
    constexpr uint8_t RESOLUTION_BITS = 12;
    constexpr uint16_t MAX_RAW = (1u << RESOLUTION_BITS) - 1u; ///< 12-битный АЦП (0..4095)
    constexpr uint8_t SAMPLES = 10;      ///< окно усреднения
    /// Грубая оценка стартера (~12 В при raw≈3000); калибровка по измеренному напряжению при сохранении.
    constexpr float DEFAULT_COEFF = 0.004f;
}

/// Эскалация отказов DS18B20 / АЦП (SensorsController).
namespace Sensors {
    constexpr uint8_t OW_TIMEOUT_FAIL_STREAK = 3;
    constexpr uint8_t ADC_SAT_FAIL_STREAK = 8;
}

/// Лимиты и значения по умолчанию стартера и запуска двигателя (секция vehicle конфига).
namespace StarterLimits {
    constexpr float CRANK_TIME_MIN_SEC = 0.5f;
    constexpr float CRANK_TIME_MAX_SEC = 30.0f;
    constexpr float CRANK_TIME_DEFAULT_SEC = 2.0f;

    constexpr uint16_t MAX_TIME_MIN_SEC = 1;
    constexpr uint16_t MAX_TIME_MAX_SEC = 30;
    constexpr uint16_t MAX_TIME_DEFAULT_SEC = 5;

    constexpr uint16_t WAIT_AFTER_START_MIN_SEC = 2;
    constexpr uint16_t WAIT_AFTER_START_MAX_SEC = 120;
    constexpr uint16_t WAIT_AFTER_START_DEFAULT_SEC = 10;

    constexpr uint16_t RETRY_PAUSE_MAX_SEC = 600;
    constexpr uint16_t RETRY_PAUSE_DEFAULT_SEC = 5;

    constexpr uint8_t ATTEMPTS_MIN = 1;
    constexpr uint8_t ATTEMPTS_MAX = 5;
    constexpr uint8_t ATTEMPTS_DEFAULT = 3;

    constexpr float RUNNING_VOLTAGE_DEFAULT = 13.2f;
    constexpr float STOPPED_VOLTAGE_DEFAULT = 12.8f;

    /// Дополнительное ожидание полного окна АЦП после `wait_after_start_sec`.
    constexpr uint32_t VOLTAGE_WINDOW_EXTRA_MS = 2000UL;
    /// Минимальная блокировка стартера после срабатывания защиты.
    constexpr uint32_t LOCKOUT_MIN_MS = 5000UL;
    /// Страховка esp_timer срабатывает на столько позже `starter_max_time_sec`.
    constexpr uint32_t HW_BACKSTOP_MARGIN_MS = 300UL;
}

/// Измерение оборотов двигателя (импульсные входы).
namespace Rpm {
    /// Фронты чаще этого предела RPM отбрасываются как шум.
    constexpr uint32_t MAX_RPM = 12000;
    /// Нет принятого фронта столько времени → 0 RPM.
    constexpr uint32_t ZERO_TIMEOUT_MS = 250;
    /// Порог должен держаться непрерывно столько, чтобы подтвердиться.
    constexpr uint32_t CONFIRM_MS = 100;
    /// Кольцо меток фронтов (степень двойки); одна оценка охватывает не больше RING-1 интервалов.
    constexpr uint8_t RING = 16;
    /// Подряд отклонённых фронтов до перезапуска цепочки (чтобы не отсечь настоящий быстрый сигнал).
    constexpr uint8_t MAX_SUSPECT_STREAK = 3;
    /// Как часто DigitalInputs::update() пересчитывает RPM.
    constexpr uint32_t UPDATE_MS = 10;
}

/// Проверка вменяемости мягких часов (отклонять UTC раньше ~2023-11).
namespace TimeSync {
    constexpr long MIN_SANE_EPOCH_UTC = 1700000000L;
    constexpr int MIN_SANE_YEAR = 2023;
}

/// Заводские значения и запасной разбор SAX (должны совпадать с замыслом DefaultConfig.h).
namespace Defaults {
    constexpr uint16_t MQTT_PUBLISH_INTERVAL_SEC = 60;
    constexpr uint8_t TIME_SYNC_INTERVAL_HOURS = 6;
}

// ============================================================
// SoftAP: параметры по умолчанию
// ============================================================
namespace ApConfig {
    constexpr char SSID_PREFIX[] = "AutoStart-";
    constexpr char DEFAULT_PASSWORD[] = "12345678";
    constexpr uint8_t CHANNEL = 1;
    constexpr uint8_t HIDDEN = 0;
    /// Потолок TX SoftAP: Arduino `WIFI_POWER_8_5dBm` (единицы 0.25 дБм).
    constexpr int8_t TX_POWER = 34;
    /// SoftAP в SETUP: несколько одновременных STA.
    constexpr uint8_t SETUP_MAX_CONNECTIONS = 4;
    /// В NORMAL допускаем несколько станций (ESP32-C3 имеет больше RAM).
    constexpr uint8_t NORMAL_MAX_CONNECTIONS = 4;
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
    /// Строки из задач вне основного цикла (async_tcp, esp_timer), ждущие отправки в SSE.
    constexpr size_t DEFERRED_SLOTS = 4;
}

// ============================================================
// JSON: лимиты размеров на проводе / статические буферы (парсинг через JsonStreamingParser из lib/; единого JsonDocument в прошивке нет).
// ============================================================
namespace JsonBytes {
    /// Максимум байт для «больших» JSON как файлы конфигурации и программы, POST и atomic write (`Limits::CONFIG_JSON_SIZE`).
    constexpr size_t MAX_FILE_JSON_BYTES = Limits::CONFIG_JSON_SIZE;

    namespace Web {
        /// Максимальный размер сериализованного SSE payload для `/events` ("data: ...\n\n").
        /// Лимит BSS для SSE (только по частям: hardware/program/... — полный снимок live в GET /bootstrap).
        /// Прежний цельный статус был ~782 B; худший блок — hardware с tempSensors; 800 + малая маржа.
        constexpr size_t SSE_STATUS_JSON_MAX = 832;
        /// Потолок буферизованного JSON API (`sendJsonBuffered`), включая собранный `/bootstrap`.
        constexpr size_t BOOTSTRAP_JSON_MAX = 4096;
    }

    namespace Mqtt {
        constexpr size_t CMD_JSON_MAX = 256; ///< макс. размер входящей JSON-команды (payload) + '\0'
        /// Лимит тела MQTT PUBLISH (JSON) при длине топика до `TextBytes::Mqtt::TOPIC-1`.
        /// На проводе: payload + 1 (fixed) + ≤4 (RL) + 2 (длина топика) + topicLen ≤ TX_MAX.
        constexpr uint16_t STATUS_PAYLOAD_MAX_BYTES = 900;
        /// Тело retained-топика `programs` (только массив).
        constexpr size_t PROGRAMS_TOPIC_JSON_MAX = 960;
        /// Тело retained-топика `hwMap` (реле/входы/датчики).
        constexpr size_t HWMAP_TOPIC_JSON_MAX = 1500; // silk ids + 32-byte channel names
        /// Тело retained-топика `trigMap` (триггеры входов и температуры).
        constexpr size_t TRIGMAP_TOPIC_JSON_MAX = 1200;
        /// Короткий ответ `{id,code[,seq]}`.
        constexpr size_t THIN_REPLY_JSON_MAX = 96;
        /// Изменение напряжения меньше этого не считается значимым для периодической выборки.
        constexpr float STATUS_VOLTAGE_EPS = 0.05f;
        /// Изменение температуры меньше этого не считается значимым для периодической выборки.
        constexpr float STATUS_TEMP_EPS = 0.1f;
        /// Публикация по фронту: напряжение должно сдвинуться хотя бы на столько.
        constexpr float EDGE_VOLTAGE_EPS = 0.5f;
        /// Публикация по фронту: температура должна сдвинуться хотя бы на столько.
        constexpr float EDGE_TEMP_EPS = 1.0f;
        /// Изменение freeHeap меньше этого не считается значимым и не попадает в дельту.
        constexpr uint32_t STATUS_HEAP_EPS = 2048;
        /// Изменение timerRemaining (сек) меньше этого не считается значимым (если не сменился флаг running).
        constexpr uint32_t STATUS_TIMER_EPS_SEC = 1;
    }
}

/// Лимиты очередей MQTT cmd/reply (контракт на проводе в docs/modules/mqtt.md).
namespace MqttCmd {
    constexpr uint8_t INBOUND_DEPTH = 8;
    constexpr uint8_t PENDING_REPLY_DEPTH = 4;
    constexpr uint8_t ID_MAX_LEN = 16;
    /// Кольцо идемпотентности для run/stop/set/set_time (только RAM; очищается при обрыве сессии).
    constexpr uint8_t ID_RING_DEPTH = 16;

    constexpr uint16_t CODE_OK = 200;
    constexpr uint16_t CODE_ACCEPTED = 202;  ///< принято / поставлено в очередь
    constexpr uint16_t CODE_BAD_REQUEST = 400;
    constexpr uint16_t CODE_NOT_FOUND = 404;
    constexpr uint16_t CODE_CONFLICT = 409;
    constexpr uint16_t CODE_UNPROCESSABLE = 422;
    constexpr uint16_t CODE_INTERNAL = 500;
    constexpr uint16_t CODE_UNAVAILABLE = 503; ///< входящая очередь полна
}

namespace PoolLimits {
    /// Кусок “scratch” для FS операций (держим небольшим ради RAM/стека).
    constexpr size_t FS_SCRATCH_BYTES = 256;
}

namespace NetTiming {
    constexpr uint32_t MQTT_RECONNECT_INTERVAL_MS = 5000;
    /// Ожидание CONNACK после CONNECT на GSM (секунды RTT + очередь оператора).
    constexpr uint32_t MQTT_FSM_CONNECT_TIMEOUT_MS = 30000;
    /// Сторож keepalive: принудительный Error, если нет MQTT RX в течение (keepAliveSec * NUM / DEN).
    constexpr uint16_t MQTT_KEEPALIVE_DEADMAN_NUM = 3;
    constexpr uint16_t MQTT_KEEPALIVE_DEADMAN_DEN = 2;

    /// Поле keepAlive в MQTT CONNECT (секунды); им же задаётся период PINGREQ.
    constexpr uint16_t MQTT_KEEPALIVE_SEC = 30;
    /// Задержка перед первой публикацией статуса после подъёма сессии.
    constexpr uint32_t MQTT_FIRST_STATUS_DELAY_MS = 1500;
    /// Ожидание SUBACK и таймаут повторной подписки.
    constexpr uint32_t MQTT_SUBACK_TIMEOUT_MS = 8000;
    /// Бюджет настенного времени на кодирование и постановку в очередь одной публикации статуса (мс).
    constexpr uint32_t MQTT_PUBLISH_BUDGET_MS = 10;
    /// Минимальный зазор между дельтами `/status` по фронту (программа, реле, входы, …).
    constexpr uint32_t MQTT_EDGE_STATUS_GAP_MS = 250;
    /// Настенный таймер TCP-connect MqttFsmClient (поверх Sim800Tcp CONNECT_TIMEOUT_MS).
    constexpr uint32_t MQTT_TCP_CONNECT_TIMEOUT_MS = 45000;

    /// Размер сборки одного кадра MQTT/TCP (слот TX MqttFsm + кольца RX/TX Sim800Tcp).
    constexpr uint16_t MQTT_FRAME_MAX = 3072;
    /// Жёсткий потолок одного CIPSEND. Кадры больше этого отвергаются до UART.
    constexpr uint16_t MQTT_CIPSEND_MAX = 1600;
    /// Ответ QoS1: ждать PUBACK, затем один DUP, затем освободить слот.
    constexpr uint32_t MQTT_REPLY_PUBACK_TIMEOUT_MS = 5000;
    /// Буфер копии одного незавершённого короткого ответа QoS1 (это не слот очереди TX).
    constexpr uint16_t MQTT_REPLY_QOS1_COPY_MAX = 128;
    /// Глубина исходящей очереди MqttFsmClient (резерв Ctrl + Tele + кадр в полёте).
    constexpr uint8_t MQTT_TX_Q_DEPTH = 3;
    /// Буфер сборки RX (неполный кадр).
    constexpr uint16_t MQTT_RX_ASSEMBLE_MAX = 320;
    constexpr uint32_t MQTT_RX_ASSEMBLE_TIMEOUT_MS = 3000;

    /// Бюджеты автомата на один тик, передаваемые в MqttFsmClient::tick.
    constexpr uint16_t MQTT_TICK_MAX_READ_BYTES = 256;
    constexpr uint16_t MQTT_TICK_MAX_WRITE_BYTES = MQTT_FRAME_MAX;
    constexpr uint8_t MQTT_TICK_MAX_PARSE_FRAMES = 4;
    constexpr uint8_t MQTT_TICK_MAX_MS = 20;
}

// ============================================================
// Web/DNS/AP (перехватывающий портал)
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
    constexpr uint8_t MAX_UI_SESSIONS = ApConfig::NORMAL_MAX_CONNECTIONS * 2;
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
    /// Жёсткий лимит = SSE_MAX_QUEUED_MESSAGES в platformio.ini (= soft + 1 слот под drop-notice).
    constexpr size_t SSE_SOFT_QUEUE_MAX = 20;
    /// +1 reserved slot: soft+1 == hard; drop-notice может уйти, когда soft уже заполнен логикой soft gate.
    constexpr size_t SSE_QUEUE_RESERVED = 1;
    constexpr size_t STATUS_QUEUE_MAX = SSE_SOFT_QUEUE_MAX;
    constexpr size_t STATUS_FORCE_QUEUE_MAX = 8;
    /// Сколько держать хвостовой диагностический журнал после последней активности SSE.
    constexpr uint32_t DIAG_TAIL_MS = 5000UL;
}

// ============================================================
// Web-ассеты LittleFS — список обязательных файлов UI
// ============================================================
namespace WebAssets {
    /// Логические URL; на LittleFS байты лежат только как `<path>.gz` (аксиома build.py).
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
    /// Задержка кручения после неудачного ESP.restart() (delayMicroseconds).
    constexpr uint32_t HARD_RESTART_SPIN_US = 5000;
}

// ============================================================
// GSM (SIM800): тайминги/лимиты автомата
// ============================================================
namespace Gsm {
    constexpr uint32_t BOOT_DELAY_MS = 1000;
    constexpr uint8_t MAX_RETRIES = 3;

    /// Целевая скорость UART MCU↔модем (гипотеза по умолчанию; проверка/запись NV через `AT+IPR?` / `AT+IPR=`).
    constexpr uint32_t UART_BAUD = 115200;
    /// Таблица перебора скоростей: сначала гипотеза, затем обычные скорости SIM800.
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
    /// Тишина после `begin()` до первой пробы AT по гипотезе скорости (модему нужно время после питания/рестарта ESP).
    constexpr uint32_t POST_BOOT_QUIET_MS = 5000;
    /// CellularCore: не трогать модем N мс после первого service() (питание/USB/SoftAP стабилизация).
    constexpr uint32_t POST_BOOT_SETTLE_MS = 20000;
    /// Пауза между повторными пробами гипотезы `AT` / `AT+CGMI` (антиспам UART при нет ответа).
    constexpr uint32_t HYP_RETRY_GAP_MS = 750;
    /// После неудачной пробы гипотезы на `UART_BAUD` — одна попытка `AT+CFUN=1,1` без гарантии, до перебора скоростей.
    constexpr uint32_t PRE_CFUN_AFTER_MS = 20000;
    /// Пауза после soft-reboot модема (`CFUN=1,1`) перед повторными AT.
    constexpr uint32_t POST_CFUN_QUIET_MS = 20000;
    /// Окно повторной пробы гипотезы на `UART_BAUD` после PreCfun; затем перебор скоростей.
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
    /// Период AT+CREG?, пока модем ещё ищет сеть (stat ≠ 1/5).
    constexpr uint32_t REG_POLL_INTERVAL_MS = 5000;
    constexpr uint32_t APN_TIMEOUT_MS = 10000;
    constexpr uint32_t PDP_ACTIVATE_TIMEOUT_MS = 20000;
    constexpr uint32_t GET_IP_TIMEOUT_MS = 10000;

    constexpr uint32_t READY_SIGNAL_INTERVAL_MS = 300000;   // 5 мин
    constexpr uint32_t READY_OPERATOR_INTERVAL_MS = 900000; // 15 мин
    constexpr uint32_t TCP_CLOSED_REATTACH_WINDOW_MS = 60000; // 1 мин
    constexpr uint8_t TCP_CLOSED_REATTACH_THRESHOLD = 3;      // 3 обрыва в окне
    constexpr uint32_t ERROR_RECOVERY_DELAY_MS = 30000;
    constexpr uint8_t ERROR_RECOVERY_MAX_CYCLES = 3;
    /// После исчерпания циклов L1–L4 ждать столько до следующей попытки (или до перезагрузки из UI).
    constexpr uint32_t ERROR_RECOVERY_EXHAUSTED_MS = 300000UL; // 5 мин
    /// Короткая пауза между шагами AT восстановления ERROR (CIPSHUT / деактивация SAPBR).
    constexpr uint32_t ERROR_RECOVERY_COOLDOWN_MS = 2000UL;
    /// Таймаут принятия деактивации AT+SAPBR=0,1.
    constexpr uint32_t SAPBR_DEACT_TIMEOUT_MS = 8000UL;
    /// Таймаут принятия AnyLine для AT+CFUN=1,1 во время восстановления.
    constexpr uint32_t CFUN_ACCEPT_TIMEOUT_MS = 1000UL;
    /// Период журнала отпечатка зависания, пока INIT/GPRS не двигается.
    constexpr uint32_t STALL_LOG_INTERVAL_MS = 8000UL;

    /// Экспоненциальный backoff повторного присоединения: base << (step-1), с потолком.
    constexpr uint32_t REATTACH_BACKOFF_BASE_MS = 5000UL;
    constexpr uint8_t REATTACH_BACKOFF_MAX_STEP = 6;
    constexpr uint32_t REATTACH_BACKOFF_MAX_MS = 180000UL;
    /// Нижняя задержка, когда RSSI слабый или неизвестен.
    constexpr uint32_t REATTACH_WEAK_RSSI_FLOOR_MS = 60000UL;
    constexpr int16_t REATTACH_WEAK_RSSI_MAX = 6;
    constexpr int16_t REATTACH_RSSI_UNKNOWN = 99;
    /// NTP модема (AT+CNTP): таймаут принятия команд настройки.
    constexpr uint32_t CNTP_AT_TIMEOUT_MS = 10000;
    /// Ожидание URC +CNTP: после запуска AT+CNTP.
    constexpr uint32_t CNTP_SYNC_TIMEOUT_MS = 60000;
    /// Чтение AT+CCLK? (проба NITZ или после успешного NTP).
    constexpr uint32_t CCLK_TIMEOUT_MS = 5000;
    /// AT+CIPGSMLOC=2,1 (время по GSM-локации) — может быть медленным.
    constexpr uint32_t CIPGSMLOC_TIMEOUT_MS = 45000;
    /// После CIPGSMLOC OK ждать столько запоздалый URC +CIPGSMLOC.
    constexpr uint32_t CIPGSMLOC_URC_GRACE_MS = 5000UL;
    /// Поднять уровень восстановления ERROR, если подъём застрял на столько (тот же порядок, что ожидание после исчерпания).
    constexpr uint32_t BRINGUP_ESCALATE_MS = ERROR_RECOVERY_EXHAUSTED_MS;
    /// Максимальная скорость, принимаемая при разборе AT+IPR? и записи IPR.
    constexpr uint32_t UART_BAUD_PARSE_MAX = 4000000UL;
    /// Глубина кольца диагностических событий (GsmController).
    constexpr uint8_t EVENT_RING_SIZE = 8;
    /// Глубины обычной и высокоприоритетной очередей AtSession.
    constexpr uint8_t AT_QSIZE = 6;
    constexpr uint8_t AT_HQSIZE = 2;
    /// Буфер кадрированной строки ModemUart.
    constexpr uint16_t UART_LINE_BUF = 160;

    // Кольцо RX для ожидающих и диагностических фрагментов. Держим компактным, чтобы экономить RAM.
    constexpr size_t RESPONSE_BUFFER_SIZE = 128;
    constexpr size_t CMD_BUFFER_SIZE = 128;

    /// Исключительная эпоха модема для голоса / SMS (MQTT CIP приостановлен).
    /// CALL_OWNER звонит, пока CLCC не сообщит active, или столько времени, затем ATH.
    constexpr uint32_t VOICE_DEFAULT_RING_MS = 25000;
    constexpr uint32_t VOICE_CLCC_POLL_MS = 2000;
    constexpr uint32_t VOICE_PASSWORD_TIMEOUT_MS = 30000;
    constexpr uint32_t VOICE_PROGID_TIMEOUT_MS = 15000;
    constexpr uint32_t VOICE_CALL_CEILING_MS = 60000;
    constexpr uint32_t VOICE_CLIP_WAIT_MS = 8000;
    constexpr uint32_t VOICE_HANGUP_TIMEOUT_MS = 8000;
    constexpr uint32_t SMS_CMGS_TIMEOUT_MS = 30000;
    /// Минимальная длина цифрового суффикса при сравнении CLIP с owner_phone.
    constexpr uint8_t PHONE_MATCH_MIN_DIGITS = 10;
}

// ============================================================
// SIM800 TCP transport (URC-first): таймбюджеты/лимиты
// ============================================================
namespace Sim800Tcp {
    /// Дополнительные строки TCP (+IPD, часть URC) в лог; без `SERIAL_DEBUG` по умолчанию выкл.
    constexpr bool TCP_VERBOSE_LOG = false;

    // Бюджет ожидания TCP CONNECT OK (URC) после CIPSTART.
    // Не больше худшего случая брокера/сети и не слишком большой, чтобы не блокировать надолго.
    constexpr uint32_t CONNECT_TIMEOUT_MS = 15000;

    // Таймаут принятия AT для команды CIPSTART (OK/ERROR); само соединение приходит через URC.
    constexpr uint32_t CIPSTART_ACCEPT_TIMEOUT_MS = 15000;

    /// Сторож соединения на стороне транспорта (от connectStart); снимает застрявший `_connecting`.
    /// Поверх ожидания TCP MqttFsmClient (~45 с): этот сторож поднимает модем раньше.
    constexpr uint32_t CONNECT_WATCHDOG_MS = CONNECT_TIMEOUT_MS;

    /// После CIPSEND '>' и записи payload: ждать SEND OK / SEND FAIL.
    constexpr uint32_t SEND_WATCHDOG_MS = 15000;

    /// Минимальный зазор между попытками CIPSTART (неблокирующие повторы Client::connect на каждом тике MQTT).
    constexpr uint32_t CONNECT_RETRY_COOLDOWN_MS = 3000;

    /// Минимальный зазор между попытками восстановления CIPSHUT.
    constexpr uint32_t STACK_RECOVER_COOLDOWN_MS = 5000;

    /// Столько восстановлений стека без CONNECT OK, прежде чем просить GSM заново поднять bearer.
    constexpr uint8_t STACK_RECOVER_REATTACH_THRESHOLD = 3;

    /// Короткое принятие AT для CIPCLOSE / CIPRXGET / CIPHEAD / CIPMUX.
    constexpr uint32_t AT_CONFIG_TIMEOUT_MS = 3000;
    /// Принятие CIPSHUT (восстановление транспорта и восстановление GSM ERROR).
    constexpr uint32_t CIPSHUT_TIMEOUT_MS = 10000;
    /// Ожидание приглашения '>' у CIPSEND.
    constexpr uint32_t CIPSEND_PROMPT_TIMEOUT_MS = 5000;
    /// Размер кольца RX и сборки TX (должен совпадать с NetTiming::MQTT_FRAME_MAX).
    constexpr uint16_t TCP_BUF_SIZE = NetTiming::MQTT_FRAME_MAX;
    /// Короткий буфер подсматривания управляющей строки CRLF в сыром push-разборщике.
    constexpr uint8_t RAW_LINE_BUF = 64;
}

/// Бюджет каскада времени на старте и при yield (после Sim800Tcp, чтобы делить таймаут CIPSHUT).
namespace Gsm {
    constexpr uint32_t BOOT_BUDGET_SLACK_MS = 15000UL;
    constexpr uint32_t BOOT_TIME_BUDGET_MS =
        CCLK_TIMEOUT_MS + Sim800Tcp::CIPSHUT_TIMEOUT_MS + CIPGSMLOC_TIMEOUT_MS +
        CNTP_AT_TIMEOUT_MS * 3UL + CNTP_SYNC_TIMEOUT_MS + BOOT_BUDGET_SLACK_MS;
}

/// Связка CellularCore (GSM + MQTT). Стоит после Sim800Tcp, чтобы переиспользовать пороги восстановления стека.
namespace Cellular {
    /// Бюджет цикла слива отключения MQTT (итерации gsm.update + mqtt.loop).
    constexpr uint8_t MQTT_DRAIN_MAX_TICKS = 50;
    constexpr uint32_t MQTT_DRAIN_MAX_MS = 200;
    /// Период журнала прогресса «осталось до settle» во время тишины после старта.
    constexpr uint32_t SETTLE_LOG_INTERVAL_MS = 5000UL;
    /// Подряд неудач соединения MQTT, после которых запрашивается повторный подъём bearer GSM.
    constexpr uint8_t MQTT_FAIL_STREAK_REATTACH = Sim800Tcp::STACK_RECOVER_REATTACH_THRESHOLD;
    /// Минимальный зазор между запросами Cellular→GSM на повторное присоединение (тот же порядок, что пауза восстановления стека).
    constexpr uint32_t MQTT_REATTACH_REQUEST_COOLDOWN_MS = Sim800Tcp::STACK_RECOVER_COOLDOWN_MS;
}

// ============================================================
// FS метаданные (ожидаемые максимальные размеры файлов)
// ============================================================
namespace FileMax {
    /// index.html на FS (CSS отдельно в style.css).
    constexpr uint32_t INDEX_HTML = 262144;
    constexpr uint32_t STYLE_CSS = 262144;
    /// Собранный Alpine+app JS (несжатый); на FS обычно лежит .gz.
    constexpr uint32_t APP_JS = 786432;
    constexpr uint32_t FAVICON_ICO = 2048;
}

// ============================================================
// Время/математика: коэффициенты пересчёта (частоты и т.п.)
// ============================================================
namespace Time {
    constexpr uint32_t MS_PER_SEC = 1000UL;
    constexpr uint32_t SEC_PER_HOUR = 3600UL;
    constexpr uint32_t MS_PER_HOUR = SEC_PER_HOUR * MS_PER_SEC;
}

