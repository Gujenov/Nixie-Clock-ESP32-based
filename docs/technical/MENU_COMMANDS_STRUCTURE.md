# Дерево меню и команда прошивки Nixie Clock

## Описание
Этот файл содержит человекочитаемое дерево меню и полный набор команд, используемых в режиме настройки прошивки.

## Структура меню

1. Главное меню
   - 1 — Настройки времени и часовых поясов
   - 2 — Управление будильниками
   - 3 — Звук и дисплей
   - 4 — WI-FI и NTP
   - 5 — Информация о системе
   - 6 — Конфигурация

### Общие команды меню
- `menu` / `m` — вернуться в главное меню
- `back` / `b` — вернуться на один уровень выше
- `out` / `exit` / `o` — выйти из меню
- `help` / `?` — повторно показать текущее меню

## 1. Меню времени и часовых поясов

`MENU_TIME`
- `time` / `t` — показать текущее время (UTC и локальное)
- `sync` — запустить синхронизацию времени через NTP
- `set UTC T HH:MM:SS` / `SUT HH:MM:SS` / `sut HH:MM:SS` — установить UTC время
- `set UTC D DD.MM.YY` / `SUD DD.MM.YY` / `sud DD.MM.YY` — установить UTC дату
- `set local T HH:MM:SS` / `SLT HH:MM:SS` / `slt HH:MM:SS` — установить локальное время
- `set local D DD.MM.YY` / `SLD DD.MM.YY` / `sld DD.MM.YY` — установить локальную дату
- `auto sync en` / `ase` — включить автосинхронизацию UTC
- `auto sync dis` / `asd` — отключить автосинхронизацию UTC
- `tz list` / `tzl` — показать список доступных зон и перейти в режим выбора зоны
- `tz auto` / `tza` — включить автоматическое определение часового пояса
- `tz manual` / `tzm` — отключить автоматическое определение часового пояса
- `tz check` / `tzc` — сравнить правила DST между ezTime и локальной таблицей

### Режим выбора часового пояса
- после `tz list` / `tzl` система ожидает выбор по номеру или ввод имени зоны
- команда `100` запускает ручную настройку смещения через `setupManualOffset()`
- ввод имени зоны формата `Region/City` устанавливает соответствующий пояс

## 2. Меню будильников

`MENU_ALARMS`
- `ref` / `refresh` / `al` / `status` — показать статус будильников
- `set al 1 HH:MM` / `sal1 HH:MM` — установить время будильника 1
- `set al 2 HH:MM` / `sal2 HH:MM` — установить время будильника 2
- `al 1 sound N` / `a1s N` — установить номер мелодии для будильника 1
- `al 2 sound N` / `a2s N` — установить номер мелодии для будильника 2
- `al 1 mode once` / `a1m once` — установить будильник 1 как одноразовый
- `al 1 mode daily` / `a1m daily` — установить будильник 1 как ежедневный
- `al 2 days 1,2,3` / `a2d 1,2,3` — установить дни недели для будильника 2
- `al 2 list workdays` / `a2l workdays` — установить маску будних дней (Пн-Пт)
- `al 2 list weekends` / `a2l weekends` — установить маску выходных дней (Сб-Вс)
- `al 2 list all` / `a2l all` — установить маску для всех дней
- `dis al 1` / `da1` — отключить будильник 1
- `dis al 2` / `da2` — отключить будильник 2
- `al 1 enable` / `al 1 on` — включить будильник 1
- `al 1 disable` / `al 1 off` — отключить будильник 1
- `al 2 enable` / `al 2 on` — включить будильник 2
- `al 2 disable` / `al 2 off` — отключить будильник 2

## 3. Меню звука и дисплея

`MENU_DISPLAY`
- `set alarm volume 0...100` / `sav 0...100` — громкость будильника
- `set bell volume 0...100` / `sbv 0...100` — громкость боя часов
- `set notify volume 0...100` / `snv 0...100` — громкость уведомлений и SFX
- `bells per hour 0|1|2|4` / `bph 0|1|2|4` — частота боя
- `bells time activity HH-HH` / `bta HH-HH` — интервал активности боя

Если устройство поддерживает Nixie-дисплей:
- `brightness control on` / `bc1` — включить автоконтроль яркости
- `brightness control off` / `bc0` — отключить автоконтроль яркости
- `max brightness learning` / `mbe` — сохранить текущий уровень как максимальный порог
- `smallest brightness learning` / `sbe` — сохранить текущий уровень как минимальный порог
- `display activity workdays HH-HH HH2-HH2` / `daw HH-HH HH2-HH2` — активность дисплея в будни
- `display activity holidays HH-HH HH2-HH2` / `dah HH-HH HH2-HH2` — активность дисплея в выходные
- `display activity copy w2h` / `dacw` — скопировать будние настройки на выходные
- `display activity copy h2w` / `dach` — скопировать выходные настройки на будни

## 4. Меню WI-FI и NTP

`MENU_WIFI`
- `wifi scan` — запустить сканирование доступных сетей
- `wifi SSID PASSWORD` — сохранить данные основной сети WiFi
- `wifi2 SSID PASSWORD` — сохранить данные резервной сети WiFi
- `set ntp1 <SERVER>` — задать первый NTP сервер
- `set ntp2 <SERVER>` — задать второй NTP сервер
- `set ntp3 <SERVER>` — задать третий NTP сервер

## 5. Меню информации

`MENU_INFO`
- выводит информацию о версии прошивки, серийном номере, типе часов, состоянии RTC, WiFi/NTP и состоянии ESP32
- дополнительных команд внутри меню нет, доступен только `help` / `back` / `out`

## 6. Меню конфигурации

`MENU_CONFIG`
- `default` — инициировать сброс конфигурации к заводским настройкам
- после `default` требуется подтверждение `y` / `yes` или `n` / `no`

## 7. Инженерное меню

`MENU_ENGINEERING`
- содержит отдельный набор команд для отладки и управления платформой
- реализация находится в `src/engineering_menu.cpp`
- команды не перечислены здесь, но используются по тому же общему принципу навигации меню

## 8. Команды вне режима меню

Некоторые команды доступны напрямую в командной строке, даже если устройство не находится в режиме меню:
- `time` / `t` — вывести текущее время
- `menu` / `m` — войти в главное меню
- `sync` — синхронизировать время по NTP
- `antipoison` / `a` — запустить процедуру anti-poison (для Nixie)
- `reset` / `rst` — перезагрузить устройство
- `ota on` / `ota off` — включить/выключить OTA окно
- `ota status` — показать статус OTA
- `bon` / `boff` — включить/выключить BLE терминал
- `bdbg on` / `bdbg off` — включить/выключить отладку BLE приема
- `help` / `?` — показать быстрый список команд

## 9. Структура дерева

Главное меню
├─ Время и часовые пояса
│  ├─ time / t
│  ├─ sync
│  ├─ set UTC T ...
│  ├─ set UTC D ...
│  ├─ set local T ...
│  ├─ set local D ...
│  ├─ auto sync en / ase
│  ├─ auto sync dis / asd
│  ├─ tz list / tzl
│  ├─ tz auto / tza
│  ├─ tz manual / tzm
│  └─ tz check / tzc
├─ Управление будильниками
│  ├─ ref / refresh / al / status
│  ├─ set al 1 ... / sal1
│  ├─ set al 2 ... / sal2
│  ├─ al 1 sound ... / a1s
│  ├─ al 2 sound ... / a2s
│  ├─ al 1 mode once/daily / a1m
│  ├─ al 2 days ... / a2d
│  ├─ al 2 list ... / a2l
│  ├─ dis al 1 / da1
│  ├─ dis al 2 / da2
│  ├─ al 1 enable/off
│  └─ al 2 enable/off
├─ Звук и дисплей
│  ├─ set alarm volume / sav
│  ├─ set bell volume / sbv
│  ├─ set notify volume / snv
│  ├─ bells per hour / bph
│  ├─ bells time activity / bta
│  ├─ brightness control on/off / bc1/bc0
│  ├─ max brightness learning / mbe
│  ├─ smallest brightness learning / sbe
│  ├─ display activity workdays / daw
│  ├─ display activity holidays / dah
│  ├─ display activity copy w2h / dacw
│  └─ display activity copy h2w / dach
├─ WI-FI и NTP
│  ├─ wifi scan
│  ├─ wifi SSID PASSWORD
│  ├─ wifi2 SSID PASSWORD
│  ├─ set ntp1 <SERVER>
│  ├─ set ntp2 <SERVER>
│  └─ set ntp3 <SERVER>
├─ Информация о системе
│  └─ (только просмотр, нет дополнительных команд)
├─ Конфигурация
│  └─ default → подтверждение y/n
└─ Инженерное меню
   └─ отдельные команды отладки и платформенных настроек
