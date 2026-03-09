# Platform profile: NIXIE_6_SPI

Цель: формализовать «набор платформы» отдельно от базовой логики времени/NTP/OTA.

## 1) Идентификатор платформы
- `clock_type = CLOCK_TYPE_NIXIE`
- `clock_digits = 6`
- `nix6_output_mode = NIX6_OUTPUT_STD | NIX6_OUTPUT_REVERSE_INVERT`

## 2) Подключённые модули
- Дисплей: `Nixie6SpiDriver` (74HC595, bit-bang)
- Ручное управление:
  - `ENC_A`, `ENC_B`
  - `ENC_BTN`
  - `ALARM_BTN` (дополнительная кнопка)
- Звук (опционально): DFPlayer (`audio_module_enabled = true`)

## 3) Платформенные возможности (capabilities)
Расчёт выполняется в `platform_profile.cpp`.

- `display_enabled = true`
- `display_supports_nixie_tuning = true`
- `sound_enabled = audio_module_enabled`
- `alarm_enabled = audio_module_enabled`
- `controls_enabled = (ui_control_mode != UI_CONTROL_NONE)`
- `button_enabled = (ui_control_mode == UI_CONTROL_BUTTON_ONLY || ui_control_mode == UI_CONTROL_ENCODER_BUTTON)`
- `encoder_enabled = (ui_control_mode == UI_CONTROL_ENCODER_BUTTON)`
- `display_navigation_enabled = (clock_type == NIXIE && clock_digits == 6 && controls_enabled)`

## 4) Политика обновления дисплея
- `DisplayTickMode::EverySecond`
- Обновление из `processSecondTick()` через `displayManager.updateFromLocalTime(...)`

## 5) Политика меню
- Раздел будильников показывается только при `alarm_enabled == true`
- Команды будильников блокируются, если модуль отключён

## 6) Политика входов
- `ENC_BTN` и `ALARM_BTN` обрабатываются одинаково (short/long/very long)
- События входов маршрутизируются в display actions только если `display_navigation_enabled == true`

## 7) Минимальный чек-лист верификации
1. `clock_type=NIXIE`, `digits=6`, `ui_control_mode=enc`.
2. Проверить реакцию на `ENC_BTN` и `ALARM_BTN`:
   - short -> `NextMainView`
   - long -> `NextAuxView`
   - very long -> enter/exit edit placeholder
3. `audio off`:
   - меню будильников скрыто
   - alarm handler не выполняет команды/срабатывание
4. OTA/NTP/время продолжают работать без изменений.

## 8) Что добавлять дальше
Для новых типов часов добавлять аналогичный профиль:
- идентификатор
- список модулей
- таблица capabilities
- policy для display tick
- policy для меню/входов
