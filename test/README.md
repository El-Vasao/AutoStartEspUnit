# Нативные тесты (`pio test -e native`)

Наборы Unity для чистых разборщиков и контрактов (без железа GSM, Web и FS):

| Набор | Что покрыто |
|-------|-------------|
| `test_gsm_at_parse` | Разбор строк AT (`GsmAtParse`) |
| `test_mqtt_command_parser` | JSON команды MQTT (`MqttCommandParser`) |
| `test_program_action` | Карта строк `ActionId` |
| `test_mqtt_protocol` | Коды MQTT, застой RX и контракт шлюза подтверждений (зеркальные помощники) |
| `test_rpm_estimator` | Обороты по периодам фронтов, фильтр шума, нулевой таймаут, подтверждение 100 мс (`RpmEstimator`) |
| `test_vehicle_rules` | Настройки автомобиля и стартера: нормализация при загрузке, строгая проверка при сохранении (`vehicle_rules`) |
| `test_starter_machine` | Автомат ENGINE_START: время, обороты, вход, повторы, блокировка (`starter::Machine`) |

```bat
pio test -e native
```

Нужен хостовый `g++` (MinGW/WinLibs). На Windows, если в имени пользователя есть пробел, `test/native_pre.py` ставит `GCC_EXEC_PREFIX` на короткий путь 8.3, чтобы `ld` нашёл `default-manifest.o`.

Полевой лист [`docs/modules/gsm_mqtt_field_log.md`](../docs/modules/gsm_mqtt_field_log.md) остаётся главным прогоном регрессии на железе.
