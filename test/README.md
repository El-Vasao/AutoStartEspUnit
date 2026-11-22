# Нативные тесты (`pio test -e native`)

Наборы Unity в `test/test_*`. Часть гоняет настоящие модули на заглушках `test/stubs/`.

Отдельного набора `test_mqtt_protocol` нет: провод, FSM и команды — это `test_mqtt_wire`, `test_mqtt_fsm`, `test_mqtt_commands`.

Дополнительно к разборщикам: `test_relay_controller`, `test_digital_inputs`, `test_battery_saver`, `test_mqtt_status_builder`, `test_gsm_fsm`, `test_program_executor`, `test_posted_body`, `test_sse_snapshot`.

```bat
pio test -e native
```

Нужен хостовый `g++` (MinGW/WinLibs). На Windows, если в имени пользователя есть пробел, `test/native_pre.py` ставит `GCC_EXEC_PREFIX` на короткий путь 8.3, чтобы `ld` нашёл `default-manifest.o`.

Полевой лист [`docs/modules/gsm_mqtt_field_log.md`](../docs/modules/gsm_mqtt_field_log.md) остаётся главным прогоном регрессии на железе.
