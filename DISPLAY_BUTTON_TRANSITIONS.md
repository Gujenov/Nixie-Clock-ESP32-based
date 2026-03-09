# Таблица переходов по нажатиям (текущая реализация)

Дата: 2026-03-09  
Статус: Актуально для текущего кода

---

## 1) Где в коде реализована логика

- Маппинг событий кнопок в действия дисплея:
  - `src/main.cpp` (`handleDisplayButtonAction()`)
- Лог `ALARM_BTN` в терминал:
  - `src/main.cpp` (`onAlarmButtonEvent()`)
- Переходы экранов NIXIE_6_SPI:
  - `src/display/nixie_6_spi.cpp` (`trigger1()`, `trigger2()`, `tick()`)

---

## 2) Условия, при которых нажатия обрабатываются

Нажатия применяются к дисплею только если одновременно выполняются условия:

1. `controls_enabled == true`
2. `button_enabled == true` (для кнопок)
3. `inMenuMode == false` (не в режиме меню)
4. `display_navigation_enabled == true`

Для NIXIE_6_SPI это обычно:
- `clock_type = CLOCK_TYPE_NIXIE`
- `clock_digits = 6`
- `ui_control_mode` допускает кнопку

---

## 3) События кнопок

Из `input_handler` приходят события:
- `BUTTON_PRESSED` (короткое)
- `BUTTON_LONG` (~1s)
- `BUTTON_VERY_LONG` (~3s)

Обе кнопки (`ENC_BTN`, `ALARM_BTN`) сейчас используют один и тот же маппинг действий дисплея.

Дополнительно для `ALARM_BTN` печатается лог в терминал:
- `[ALARM_BTN] Short press - <view> active`
- `[ALARM_BTN] Long press (1s) - <view> active`
- `[ALARM_BTN] Very long press (3s) - <view> active`

---

## 4) Маппинг кнопка -> действие

| Событие | Действие |
|---|---|
| `BUTTON_PRESSED` | `DisplayAction::NextMainView` |
| `BUTTON_LONG` | `DisplayAction::NextAuxView` |
| `BUTTON_VERY_LONG` | toggle `EnterEditMode` / `ExitEditMode` |

---

## 5) Переходы экранов NIXIE_6_SPI

### 5.1 Основная ветка (`trigger1`, `NextMainView`)

Если НЕ активна вспомогательная ветка:

`Time -> Date -> Alarm1 -> Alarm2 -> Time`

Если аудио отключено (`alarm_enabled == false`), переходы на alarm-экраны игнорируются:

`Time -> Date -> Time`

Если активна вспомогательная ветка, то `trigger1` сначала возвращает в `Time`:

`(Pressure/Humidity/Temperature) --trigger1--> Time`

### 5.2 Вспомогательная ветка (`trigger2`, `NextAuxView`)

- Первый `trigger2` из основной ветки:

`Time/Date/Alarm1/Alarm2 --trigger2--> Pressure`

- Далее цикл:

`Pressure -> Humidity -> Temperature -> Time`

(на `Temperature` следующий `trigger2` выключает вспомогательную ветку и возвращает `Time`)

### 5.3 Режим редактирования (заглушка)

`BUTTON_VERY_LONG` переключает флаг `editPlaceholder_`:
- OFF -> ON (`EnterEditMode`)
- ON -> OFF (`ExitEditMode`)

Пока `editPlaceholder_ == true`, `trigger1/trigger2` игнорируются.

### 5.4 Автовозврат по таймауту

Для основной ветки есть автосброс в `Time`:
- если экран не `Time`
- и прошло `mainModeTimeoutMs` (по умолчанию 5000 мс)

Таймаут не действует при активной вспомогательной ветке.

---

## 6) Энкодер (дополнительно)

Сейчас энкодер дублирует навигацию:

- `delta > 0` -> `NextMainView`
- `delta < 0` -> `NextAuxView`

То есть логика переходов для энкодера использует те же `trigger1/trigger2`.

---

## 7) Что с NIXIE_4_SPI

На текущий момент отдельного драйвера `NIXIE_4_SPI` с собственной FSM переходов в проекте нет.

Т.е. этот документ описывает **фактически реализованную** таблицу для `NIXIE_6_SPI`.  
Для `NIXIE_4_SPI` таблицу можно создать после выбора/реализации драйвера и списка экранов.

---

## 8) Строгая таблица переходов (State + Event -> NextState + Action)

Обозначения:
- `S` — текущее состояние экрана
- `E` — событие (`Short`, `Long`, `VeryLong`)
- `A` — действие
- `S'` — следующее состояние

### 8.1 Основная ветка (`secondaryBranchActive == false`, `edit == false`)

| S | E | Условие | A | S' |
|---|---|---|---|---|
| Time | Short | `alarm_enabled=true` | `trigger1` | Date |
| Date | Short | `alarm_enabled=true` | `trigger1` | Alarm1 |
| Alarm1 | Short | `alarm_enabled=true` | `trigger1` | Alarm2 |
| Alarm2 | Short | `alarm_enabled=true` | `trigger1` | Time |
| Time | Short | `alarm_enabled=false` | `trigger1(skip alarms)` | Date |
| Date | Short | `alarm_enabled=false` | `trigger1(skip alarms)` | Time |
| Pressure/Humidity/Temperature | Short | (вторичная ветка активна) | `trigger1` | Time |
| Time/Date/Alarm1/Alarm2 | Long | всегда | `trigger2` | Pressure |
| any | VeryLong | `edit=false` | `EnterEditMode` | Edit |

### 8.2 Вспомогательная ветка (`secondaryBranchActive == true`, `edit == false`)

| S | E | A | S' |
|---|---|---|---|
| Pressure | Long | `trigger2` | Humidity |
| Humidity | Long | `trigger2` | Temperature |
| Temperature | Long | `trigger2` | Time |

### 8.3 Режим редактирования (`edit == true`)

| S | E | A | S' |
|---|---|---|---|
| Edit | Short | игнор | Edit |
| Edit | Long | игнор | Edit |
| Edit | VeryLong | `ExitEditMode` | предыдущее активное отображение |

### 8.4 Таймауты

| S | Событие времени | Условие | S' |
|---|---|---|---|
| Date/Alarm1/Alarm2 | `tick(nowMs)` | прошло `mainModeTimeoutMs` | Time |
| Pressure/Humidity/Temperature | `tick(nowMs)` | secondary branch active | без автосброса |
