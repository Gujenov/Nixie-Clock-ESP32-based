#include "menu_manager.h"
#include "config.h"
#include "time_utils.h"
#include "timezone_manager.h"
#include "hardware.h"
#include "alarm_handler.h"
#include "platform_profile.h"
#include <Arduino.h> 

extern bool printEnabled;

MenuState currentMenuState = MENU_MAIN;
bool inMenuMode = false;

// Прототипы вспомогательных функций
// printTimezoneInfo и listAvailableTimezones теперь в timezone_manager.h
void enableAutoTimezone();
void disableAutoTimezone();
void enableAutoSync();
void disableAutoSync();

static bool isAlarmFeatureEnabled() {
    return platformGetCapabilities().alarm_enabled;
}

static bool isNixClockForUserMenu() {
    return (config.clock_type == CLOCK_TYPE_NIXIE || config.clock_type == CLOCK_TYPE_NIXIE_HAND);
}

static void onTimezoneActivated() {
    config.time_config.automatic_localtime = true;
    config.time_config.auto_sync_enabled = true;
    saveConfig();
    Serial.print("\n[TZ] Автоопределение часового пояса ВКЛЮЧЕНО");
    Serial.print("\n[SYNC] Автосинхронизация ВКЛЮЧЕНА");
    syncTimeAsync();
}

// ======================= УПРАВЛЕНИЕ РЕЖИМОМ МЕНЮ =======================

void enterMenuMode() {
    
    // ======================= ГЛАВНОЕ МЕНЮ (уровень 1) =======================

    inMenuMode = true;
    printEnabled = false;  // Останавливаем авто-вывод времени
    Serial.println("\n====================");
    Serial.println("ВНИМАНИЕ!");
    Serial.println("Устройство в режиме настройки.");
    Serial.println("Вывод времени в терминал остановлен.");
    Serial.println("Для выхода введите 'o'.");

    Serial.println("\nВыбор подменю 1-6:");
    Serial.println("\n1  Настройки времени и часовых поясов");
    Serial.println("2  Управление будильниками");
    Serial.println("3  Звук и дисплей");
    Serial.println("4  Настройки WI-FI и NTP");
    Serial.println("5  Информация о системе");
    Serial.println("6  Конфигурация");
    Serial.println();
}

void exitMenuMode() {
    inMenuMode = false;
    printEnabled = true;   // Возобновляем авто-вывод
    currentMenuState = MENU_MAIN;
    Serial.println("\n=== ВЫХОД ИЗ МЕНЮ ===");
}

bool isInMenuMode() {
    return inMenuMode;
}

// ======================= ГЛАВНОЕ МЕНЮ (уровень 1) =======================

void handleMainMenu(String command) {
    if (command.equals("1")) {
        currentMenuState = MENU_TIME;
        printTimeMenu();
    }
    else if (command.equals("2")) {
        if (!isAlarmFeatureEnabled()) {
            Serial.println("Устройство не оборудовано функциями аудио");
            return;
        }

        currentMenuState = MENU_ALARMS;
        printAlarmMenu();
    }
    else if (command.equals("3")) {
        currentMenuState = MENU_DISPLAY;
        printDisplayMenu();
    }
    else if (command.equals("4")) {
        currentMenuState = MENU_WIFI;
        printWifiMenu();
    }
    else if (command.equals("5")) {
        currentMenuState = MENU_INFO;
        printInfoMenu();
    }
    else if (command.equals("6")) {
        currentMenuState = MENU_CONFIG;
        printConfigMenu();
    }
    else if (command.equals("7") || command.equals("out") || command.equals("exit")|| command.equals("o")) {
        exitMenuMode();
    }
    else if (command.equals("help") || command.equals("?")) {
        enterMenuMode();
    }
    else if (command.equals("time") || command.equals("t")) {
        // Даже в меню можно посмотреть время
        printTime();
    }
    else {
        Serial.println("Неизвестная команда. Введите 'help' для списка");
    }
}

// ======================= МЕНЮ ВРЕМЕНИ (уровень 2) =======================

void printTimeMenu() {
    Serial.println("\n=== НАСТРОЙКИ ВРЕМЕНИ И ЧАСОВЫХ ПОЯСОВ ===");
    
    printTimezoneInfo();
    
    Serial.println("\nУстановки времени:");
    Serial.println("  time / t                   - Текущее время (UTC и локальное)");
    Serial.println("  sync                       - Синхронизировать с NTP");
    Serial.println("  set UTC T / sut HH:MM:SS   - Установить UTC время");
    Serial.println("  set UTC D / sud DD.MM.YY   - Установить UTC дату");
    Serial.println("  set local T / slt HH:MM:SS - Установить локальное время");
    Serial.println("  set local D / sld DD.MM.YY - Установить локальную дату");
    
    Serial.println("\nАвтоматическая синхронизация времени по UTC:");
    Serial.println("  auto sync en / ase   - Включить автосинхронизацию");
    Serial.println("  auto sync dis / asd - Отключить автосинхронизацию");
    
    Serial.println("\nЧасовые пояса:");
    
    Serial.println("  tz list / tzl      - Выбор доступных поясов (пресеты и именованные зоны)");
    Serial.println("  tz auto / tza      - Автоматическое определение пояса");
    Serial.println("  tz manual / tzm    - Отключить автоопределение");
    Serial.println("  tz check / tzc     - Сравнить правила DST (ezTime vs таблица)");

    printMappingMenuCommands();  //Управление меню
}

bool handleCommonMenuCommands(const String &command, void (*printMenu)()) {
    if (command.equals("menu") || command.equals("m")) {
        // Переход в главное меню
        currentMenuState = MENU_MAIN;
        enterMenuMode();
        return true;
    }
    else if (command.equals("back") || command.equals("b")) {
        // Универсальный возврат на уровень выше (в текущей архитектуре — в главное меню)
        currentMenuState = MENU_MAIN;
        enterMenuMode();
        return true;
    }
    else if (command.equals("out") || command.equals("exit") || command.equals("o")) {
        exitMenuMode();
        return true;
    }
    else if (command.equals("help") || command.equals("?")) {
        if (printMenu) printMenu();
        return true;
    }
    return false;
}

// ======================= ОБРАБОТКА ЧАСОВЫХ ПОЯСОВ =======================

// Единственный источник данных по часовым поясам — таблица TIMEZONE_PRESETS[]
// в timezone_manager.cpp. Здесь она НЕ дублируется: выбор зоны идёт через
// listAvailableTimezones() / getPresetByIndex() / setTimezone(), а ручное
// смещение — через setupManualOffset().

// State for interactive timezone listing
static int tz_list_state = 0; // 0 = normal, 1 = режим выбора зоны из списка

void handleTimeMenu(String command) {
    if (handleCommonMenuCommands(command, printTimeMenu)) { tz_list_state = 0; return; }
    else if (command.equals("time") || command.equals("t")) {
        printTime();
    }
    else if (command.equals("sync")) {
        syncTimeAsync(true);
    }
    // Команды установки UTC времени и даты
    else if (command.startsWith("set UTC T ") || command.startsWith("SUT ") || command.startsWith("sut ") || command.startsWith("set utc t ")) {
        String timeStr = (command.startsWith("set UTC T ") || command.startsWith("set utc t ")) ? command.substring(10) : command.substring(4);
        setManualTime(timeStr);
    }
    else if (command.startsWith("set UTC D ") || command.startsWith("SUD ") || command.startsWith("sud ") || command.startsWith("set utc d ")) {
        String dateStr = (command.startsWith("set UTC D ") || command.startsWith("set utc d ")) ? command.substring(10) : command.substring(4);
        setManualDate(dateStr);
    }
    // Команды установки локального времени и даты
    else if (command.startsWith("set local T ") || command.startsWith("set local t ") || command.startsWith("SLT ") || command.startsWith("slt ")) {
        String timeStr = (command.startsWith("set local T ") || command.startsWith("set local t ")) ? command.substring(12) : command.substring(4);
        setManualLocalTime(timeStr);
    }
    else if (command.startsWith("set local D ") || command.startsWith("set local d ") || command.startsWith("SLD ") || command.startsWith("sld ")) {
        String dateStr = (command.startsWith("set local D ") || command.startsWith("set local d ")) ? command.substring(12) : command.substring(4);
        setManualLocalDate(dateStr);
    }
    else {
        // If we're in an interactive timezone list mode, handle numeric or direct selections first
        if (tz_list_state == 1) {
            // Проверяем, является ли ввод числом
            bool isNum = true;
            for (size_t i = 0; i < command.length(); ++i) {
                if (!isDigit(command[i])) { 
                    isNum = false; 
                    break; 
                }
            }
            
            if (isNum && command.length() > 0) {
                int num = command.toInt();
                
                // Специальная обработка для номера 100 - ручная настройка
                if (num == 100) {
                    setupManualOffset();
                    return;
                }
                
                uint8_t count = getPresetsCount();
                
                if (num >= 1 && num <= count) {
                    // Получаем preset по индексу (индекс = номер - 1)
                    const TimezonePreset* preset = getPresetByIndex(num - 1);
                    if (preset) {
                        // Устанавливаем часовой пояс
                        if (setTimezone(preset->zone_name)) {
                            Serial.printf("\n[TZ] Установлена зона: %s\n", preset->display_name);
                            onTimezoneActivated();
                        } else {
                            Serial.printf("\n[TZ] Ошибка при установке зоны: %s\n", preset->zone_name);
                        }
                        tz_list_state = 0;
                        return;
                    }
                } else {
                    Serial.printf("\nНеверный номер. Введите число от 1 до %d\n", count);
                    return;
                }
            }
            
            // Разрешаем ввод имени зоны напрямую (например, 'Europe/Warsaw')
            if (command.indexOf('/') >= 0 || command.equals("CET")) {
                if (setTimezone(command.c_str())) {
                    Serial.printf("\n[TZ] Установлена зона: %s\n", command.c_str());
                    onTimezoneActivated();
                } else {
                    Serial.printf("\n[TZ] Зона не найдена: %s\n", command.c_str());
                }
                tz_list_state = 0;
                return;
            }
        }

        // case-insensitive helpers and aliases
        String cmdLower = command;
        cmdLower.toLowerCase();

        if (cmdLower.equals("tz list") || cmdLower.equals("tzl")) {
            // Используем новую функцию из timezone_manager
            ::listAvailableTimezones();
            tz_list_state = 1;  // Активируем режим выбора зоны
            return;
        }
        if (cmdLower.equals("tz auto") || cmdLower.equals("tza")) {
            enableAutoTimezone();
            tz_list_state = 0;
            return;
        }
        if (cmdLower.equals("tz manual") || cmdLower.equals("tzm")) {
            disableAutoTimezone();
            tz_list_state = 0;
            return;
        }
        if (cmdLower.equals("tz check") || cmdLower.equals("tzc")) {
            compareDSTRules();
            tz_list_state = 0;
            return;
        }
        if (cmdLower.equals("auto sync en") || cmdLower.equals("ase")) {
            enableAutoSync();
            return;
        }
        if (cmdLower.equals("auto sync dis") || cmdLower.equals("asd")) {
            disableAutoSync();
            return;
        }

        Serial.println("Неизвестная команда. Введите 'help' для справки");
    }
}

// ======================= МЕНЮ ЗВУКА/БУДИЛЬНИКОВ (уровень 2) =======================

static uint8_t parseDaysMask(const String &input) {
    String s = input;
    s.trim();
    s.toLowerCase();

    if (s == "workdays" || s == "wd") return 0x1F; // Пн-Пт
    if (s == "weekends" || s == "we") return 0x60; // Сб-Вс
    if (s == "all") return 0x7F;

    String buf = s;
    buf.replace(',', ' ');

    uint8_t mask = 0;
    int start = 0;
    while (start < buf.length()) {
        while (start < buf.length() && buf[start] == ' ') start++;
        if (start >= buf.length()) break;
        int end = buf.indexOf(' ', start);
        if (end == -1) end = buf.length();
        String token = buf.substring(start, end);
        token.trim();
        int day = token.toInt();
        if (day >= 1 && day <= 7) {
            uint8_t bit = (day == 7) ? 6 : static_cast<uint8_t>(day - 1);
            mask |= (1 << bit);
        }
        start = end + 1;
    }

    return mask;
}

static bool containsDayDigits(const String &input) {
    for (size_t i = 0; i < input.length(); ++i) {
        if (input[i] >= '1' && input[i] <= '7') return true;
    }
    return false;
}

static bool isDaysKeyword(const String &input) {
    String s = input;
    s.trim();
    s.toLowerCase();
    return (s == "workdays" || s == "weekends" || s == "all" || s == "wd" || s == "we");
}

void printAlarmMenu() {
    Serial.println("\n=== УПРАВЛЕНИЕ БУДИЛЬНИКАМИ ===");
    printAlarmStatus();

    Serial.println("\n  ref / refresh - Обновление информации о будильниках\n");

    Serial.println("\n  set al 1 [HH:MM] / sal1 [HH:MM] - время будильника 1");
    Serial.println("  al 1 sound [num] / a1s [num] - номер мелодии для будильника 1");
    Serial.println("  al 1 mode [once/daily] / a1m [o/d] - режим будильника 1 (один раз/ежедневно)");
    Serial.println("  dis al 1 / da1 - отключить будильник 1");
    Serial.println("\n  set al 2 [HH:MM] / sal2 [HH:MM] - время будильника 2");
    Serial.println("  al 2 sound [num] / a2s [num] - номер мелодии для будильника 2");
    Serial.println("  al 2 days [1,2,3...7] / a2d [1,2,3...7] - срабатывание буд. 2, начиная с Пн=1 по Вс=7");
    Serial.println("  al 2 list [workdays|weekends|all] / a2l [...] - набор дней будильника 2 (по будням, выходным, все)");
    Serial.println("  dis al 2 / da2 - отключить будильник 2");

    printMappingMenuCommands();
}

void handleAlarmMenu(String command) {
    if (!isAlarmFeatureEnabled()) {
        Serial.println("Устройство не оборудовано функциями аудио");
        currentMenuState = MENU_MAIN;
        return;
    }

    if (handleCommonMenuCommands(command, printAlarmMenu)) return;
    else if (command.equalsIgnoreCase("al") || command.equals("status") ||
             command.equalsIgnoreCase("ref") || command.equalsIgnoreCase("refresh")) {
        printAlarmStatus();
    }
    else if (command.startsWith("sal1") || command.startsWith("sal2")) {
        int alarmNum = command.startsWith("sal1") ? 1 : 2;
        String timeStr = command.substring(4);
        timeStr.trim();
        if (timeStr.length() == 0) {
            Serial.println("Нужно указать время в формате HH:MM");
        } else {
            if (!setAlarm(alarmNum, timeStr)) {
                Serial.println("Неверный формат времени.");
            }
        }
    }
    else if (command.startsWith("a1s") || command.startsWith("a2s")) {
        int alarmNum = command.startsWith("a1s") ? 1 : 2;
        String numStr = command.substring(3);
        numStr.trim();
        if (numStr.length() == 0) {
            Serial.println("Нужно указать номер мелодии");
        } else {
            int melody = numStr.toInt();
            if (melody <= 0) {
                Serial.println("Неверный номер мелодии (минимум 1)");
            } else {
                setAlarmMelody(alarmNum, static_cast<uint8_t>(melody));
            }
        }
    }
    else if (command.startsWith("a1m")) {
        String modeStr = command.substring(3);
        modeStr.trim();
        modeStr.toLowerCase();
        if (modeStr == "once") {
            setAlarmOnceMode(1, true);
        } else if (modeStr == "daily") {
            setAlarmOnceMode(1, false);
        } else {
            Serial.println("Нужно указать once или daily");
        }
    }
    else if (command.startsWith("a2d")) {
        String daysStr = command.substring(3);
        daysStr.trim();
        if (daysStr.length() == 0) {
            Serial.println("Нужно указать дни в виде списка (1-7), например: 1,3,5,7");
        } else {
            uint8_t mask = parseDaysMask(daysStr);
            if (!containsDayDigits(daysStr)) {
                Serial.println("Неверный формат. Используйте дни 1-7, например: 1,3,5,7");
            } else if (mask == 0) {
                Serial.println("Список дней пуст. Укажите дни 1-7");
            } else {
                setAlarmDaysMask(2, mask);
            }
        }
    }
    else if (command.startsWith("a2l")) {
        String daysStr = command.substring(3);
        daysStr.trim();
        if (daysStr.length() == 0) {
            Serial.println("Нужно указать: workdays, weekends или all");
        } else if (!isDaysKeyword(daysStr) && !containsDayDigits(daysStr)) {
            Serial.println("Неверный формат. Используйте workdays/weekends/all или список 1-7");
        } else {
            uint8_t mask = parseDaysMask(daysStr);
            if (mask == 0) {
                Serial.println("Список дней пуст. Укажите дни 1-7 или presets");
            } else {
                setAlarmDaysMask(2, mask);
            }
        }
    }
    else if (command.startsWith("da1") || command.startsWith("da2")) {
        int alarmNum = command.startsWith("da1") ? 1 : 2;
        if (!disableAlarm(alarmNum)) {
            Serial.println("Не удалось отключить будильник");
        }
    }
    else if (command.startsWith("dis al ")) {
        String numStr = command.substring(7);
        numStr.trim();
        int alarmNum = numStr.toInt();
        if (alarmNum < 1 || alarmNum > 2) {
            Serial.println("Неверный номер будильника (1 или 2)");
        } else {
            if (!disableAlarm(alarmNum)) {
                Serial.println("Не удалось отключить будильник");
            }
        }
    }
    else if (command.startsWith("set al ")) {
        String args = command.substring(7);
        args.trim();
        int spaceIdx = args.indexOf(' ');
        if (spaceIdx == -1) {
            Serial.println("Нужно указать номер будильника и время в формате HH:MM");
        }
        else {
            String numStr = args.substring(0, spaceIdx);
            String timeStr = args.substring(spaceIdx + 1);
            int alarmNum = numStr.toInt();
            timeStr.trim();
            if (alarmNum < 1 || alarmNum > 2) {
                Serial.println("Неверный номер будильника (1 или 2)");
            }
            else {
                if (!setAlarm(alarmNum, timeStr)) {
                    Serial.println("Неверный формат времени.");
                }
            }
        }
    }
    else if (command.startsWith("al ")) {
        String args = command.substring(3);
        args.trim();
        int spaceIdx = args.indexOf(' ');
        String numStr = (spaceIdx == -1) ? args : args.substring(0, spaceIdx);
        String action = (spaceIdx == -1) ? "" : args.substring(spaceIdx + 1);
        int alarmNum = numStr.toInt();
        if (alarmNum < 1 || alarmNum > 2) {
            Serial.println("Неверный номер будильника (1 или 2)");
        }
        else {
            action.trim();
            if (action.startsWith("sound")) {
                String numStr2 = action.substring(5);
                numStr2.trim();
                if (numStr2.length() == 0) {
                    Serial.println("Нужно указать номер мелодии");
                } else {
                    int melody = numStr2.toInt();
                    if (melody <= 0) {
                        Serial.println("Неверный номер мелодии (минимум 1)");
                    } else {
                        setAlarmMelody(alarmNum, static_cast<uint8_t>(melody));
                    }
                }
            }
            else if (action.startsWith("mode")) {
                String modeStr = action.substring(4);
                modeStr.trim();
                modeStr.toLowerCase();
                if (alarmNum != 1) {
                    Serial.println("Режим once/daily доступен только для будильника 1");
                } else if (modeStr == "once") {
                    setAlarmOnceMode(1, true);
                } else if (modeStr == "daily") {
                    setAlarmOnceMode(1, false);
                } else {
                    Serial.println("Нужно указать once или daily");
                }
            }
            else if (action.startsWith("days")) {
                String daysStr = action.substring(4);
                daysStr.trim();
                if (alarmNum != 2) {
                    Serial.println("Дни недели доступны только для будильника 2");
                } else if (daysStr.length() == 0) {
                    Serial.println("Нужно указать дни в виде списка (1-7), например: 1,3,5,7");
                } else {
                    uint8_t mask = parseDaysMask(daysStr);
                    if (!containsDayDigits(daysStr)) {
                        Serial.println("Неверный формат. Используйте дни 1-7, например: 1,3,5,7");
                    } else if (mask == 0) {
                        Serial.println("Список дней пуст. Укажите дни 1-7");
                    } else {
                        setAlarmDaysMask(2, mask);
                    }
                }
            }
            else if (action.startsWith("list")) {
                String daysStr = action.substring(4);
                daysStr.trim();
                if (alarmNum != 2) {
                    Serial.println("Дни недели доступны только для будильника 2");
                } else if (daysStr.length() == 0) {
                    Serial.println("Нужно указать: workdays, weekends или all");
                } else if (!isDaysKeyword(daysStr) && !containsDayDigits(daysStr)) {
                    Serial.println("Неверный формат. Используйте workdays/weekends/all или список 1-7");
                } else {
                    uint8_t mask = parseDaysMask(daysStr);
                    if (mask == 0) {
                        Serial.println("Список дней пуст. Укажите дни 1-7 или presets");
                    } else {
                        setAlarmDaysMask(2, mask);
                    }
                }
            }
            else if (action.equals("enable") || action.equals("on")) {
                if (!enableAlarm(alarmNum)) Serial.println("Не удалось включить будильник");
            }
            else if (action.equals("disable") || action.equals("off")) {
                if (!disableAlarm(alarmNum)) Serial.println("Не удалось отключить будильник");
            }
            else if (action.equals("status") || action.equals("")) {
                printAlarmStatus();
            }
            else {
                Serial.println("Неизвестная команда будильника. Введите 'help' для справки");
            }
        }
    }
    else {
        Serial.println("Неизвестная команда. Введите 'help' для справки");
    }
}

// Реализации меню "Звук и дисплей" и "Wi‑Fi/NTP" вынесены в menu_display_wifi.cpp

// Реализации меню "Информация" и "Конфигурация" вынесены в menu_info_config.cpp

// ======================= БАЗОВЫЕ КОМАНДЫ (работают всегда) =======================

void printQuickHelp() {
    Serial.println("\n=== ОСНОВНЫЕ КОМАНДЫ ===");

    Serial.println("  time / t     - Текущее время");
    Serial.println("  menu / m     - Главное меню");
    Serial.println("  sync         - Синхронизировать с NTP");
    Serial.println("  antipoison / a - Антиотравление");
    Serial.println("  reset / rst  - Перезагрузить устройство");

    Serial.println("\n  Работа с беспроводными интерфейсами:\n");
    Serial.println("  ota on/off   - Вкл/выкл OTA окно");
    Serial.println("  ota status   - Статус OTA");
    Serial.println("  bon / boff   - Вкл/выкл BLE терминал");
    Serial.println("  bdbg on/off  - Отладка BLE приёма");
    Serial.println("  help / ?     - Это сообщение");
    Serial.println("==========================\n");
}

// Вспомогательные функции (реализация)

void printSettings() {
    Serial.println("\n=== ТЕКУЩАЯ КОНФИГУРАЦИЯ ===");
    Serial.println("(Небольшая часть параметров приведена ниже)");
    // Пример вывода основных настроек
    Serial.print("Локация: ");
    Serial.println(config.time_config.timezone_name[0] ? config.time_config.timezone_name : "(не установлена)");
    Serial.print("Режим: ");
    Serial.println(config.time_config.automatic_localtime ? "ezTime (online)" : "Таблица (offline)");
    Serial.print("Текущее смещение: UTC");
    Serial.println(config.time_config.current_offset >= 0 ? "+" : "");
    Serial.println(config.time_config.current_offset);
    Serial.print("DST активен: ");
    Serial.println(config.time_config.current_dst_active ? "YES" : "NO");
}

// Функции setManualTimezone() и setManualTimezoneOffset() удалены как мёртвый код:
// они нигде не вызывались и опирались на дублирующую таблицу tz_presets[].
// Выбор зоны выполняется в handleTimeMenu() через канонический путь
// (listAvailableTimezones / getPresetByIndex / setTimezone / setupManualOffset).

void enableAutoTimezone() {
    // Запрещаем переключение в автоматический режим для Manual preset
    if (strcmp(config.time_config.timezone_name, "MANUAL") == 0) {
        Serial.println("\n✖ Ошибка: Невозможно включить автоматический режим для ручной настройки.");
        Serial.println("Используйте команду 'tz list' для выбора часового пояса из списка.");
        return;
    }
    
    config.time_config.automatic_localtime = true;
    initTimezone(); // попытаться загрузить ezTime
    saveConfig();
    Serial.println("\nРежим ezTime (online) включен");
}

void disableAutoTimezone() {
    config.time_config.automatic_localtime = false;
    initTimezone(); // переключиться на локальную таблицу
    saveConfig();
    Serial.println("\nРежим локальной таблицы (offline) включен");
}

void enableAutoSync() {
    config.time_config.auto_sync_enabled = true;
    saveConfig();
    Serial.println("\nАвтоматическая синхронизация времени ВКЛЮЧЕНА");
}

void disableAutoSync() {
    config.time_config.auto_sync_enabled = false;
    saveConfig();
    Serial.println("\nАвтоматическая синхронизация времени ОТКЛЮЧЕНА");
    Serial.println("Время можно установить вручную или синхронизировать командой 'sync'");
}

void printMappingMenuCommands() {
    Serial.println("\nНавигация:");
    Serial.println("  back / b      - Назад в предыдущее меню");
    Serial.println("  menu / m      - Главное меню");
    Serial.println("  out / o       - Выход из режима настройки");
    Serial.println("==========================\n");
}
