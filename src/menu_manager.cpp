#include "menu_manager.h"
#include "config.h"
#include "time_utils.h"
#include "timezone_manager.h"
#include "hardware.h"
#include "alarm_handler.h"
#include <Arduino.h> 

extern bool printEnabled;

MenuState currentMenuState = MENU_MAIN;
bool inMenuMode = false;

// Прототипы вспомогательных функций
// printTimezoneInfo и listAvailableTimezones теперь в timezone_manager.h
void setManualTimezone(String tz_name);
void setManualTimezoneOffset(int offset);
void enableAutoTimezone();
void disableAutoTimezone();
void enableAutoSync();
void disableAutoSync();

static void onTimezoneActivated() {
    config.time_config.automatic_localtime = true;
    config.time_config.auto_sync_enabled = true;
    saveConfig();
    Serial.print("\n[TZ] Автоопределение часового пояса ВКЛЮЧЕНО");
    Serial.print("\n[SYNC] Автосинхронизация ВКЛЮЧЕНА");
    syncTime();
}

// ======================= УПРАВЛЕНИЕ РЕЖИМОМ МЕНЮ =======================

void enterMenuMode() {
    
    // ======================= ГЛАВНОЕ МЕНЮ (уровень 1) =======================

    inMenuMode = true;
    printEnabled = false;  // Останавливаем авто-вывод времени
    Serial.println("\n====================");
    Serial.println("ВНИМАНИЕ!");
    Serial.println("Устройство в режиме настройки.");
    Serial.println("Автоматический вывод времени остановлен.");
    Serial.println("Для выхода введите 'o'.");
    Serial.println("\nВыбор подменю 1-5:");
    Serial.println("\n1  Настройки времени и часовых поясов");
    Serial.println("2  Управление будильниками");
    Serial.println("3  Настройки WI-FI и NTP");
    Serial.println("4  Информация о системе");
    Serial.println("5  Конфигурация");
    Serial.println();
}

void exitMenuMode() {
    inMenuMode = false;
    printEnabled = true;   // Возобновляем авто-вывод
    currentMenuState = MENU_MAIN;
    Serial.println("\n=== РЕЖИМ НАСТРОЙКИ ЗАВЕРШЕН ===");
    Serial.println("Автоматический вывод времени возобновлён\n");
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
        currentMenuState = MENU_ALARMS;
        printAlarmMenu();
    }
    else if (command.equals("3")) {
        currentMenuState = MENU_WIFI;
        printWifiMenu();
    }
    else if (command.equals("4")) {
        currentMenuState = MENU_INFO;
        printInfoMenu();
    }
    else if (command.equals("5")) {
        currentMenuState = MENU_CONFIG;
        printConfigMenu();
    }
    else if (command.equals("6") || command.equals("out") || command.equals("exit")|| command.equals("o")) {
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

// Общий список пресетов часовых поясов (гибрид: offset-пресеты + именованные зоны)
// offset: стандартное смещение (используется как fallback если ezTime не доступен)
struct TZPreset { const char* display; const char* zone_name; int8_t offset; bool is_offset; bool has_dst; };
static const TZPreset tz_presets[] = {
    {"Offset: UTC-12", nullptr, -12, true, false},
    {"Offset: UTC-11", nullptr, -11, true, false},
    {"Offset: UTC-10 (Hawaii)", nullptr, -10, true, false},
    {"Offset: UTC-9 (Alaska)", nullptr, -9, true, false},
    {"Offset: UTC-8 (Pacific US)", nullptr, -8, true, false},
    {"Offset: UTC-7 (Mountain US)", nullptr, -7, true, false},
    {"Offset: UTC-6 (Central US)", nullptr, -6, true, false},
    {"Offset: UTC-5 (Eastern US)", nullptr, -5, true, false},
    {"Offset: UTC-4 (Atlantic)", nullptr, -4, true, false},
    {"Offset: UTC", nullptr, 0, true, false},
    {"Offset: UTC+1 (Central Europe)", nullptr, 1, true, false},
    {"Offset: UTC+2 (Eastern Europe)", nullptr, 2, true, false},
    {"Offset: UTC+3", nullptr, 3, true, false},
    {"Asia/Tokyo (Japan)", "Asia/Tokyo", 9, false, false},
    {"Asia/Shanghai (China)", "Asia/Shanghai", 8, false, false},
    {"Asia/Seoul (Korea)", "Asia/Seoul", 9, false, false},
    {"Asia/Singapore", "Asia/Singapore", 8, false, false},
    {"Asia/Kolkata (India)", "Asia/Kolkata", 5, false, false},  // UTC+5:30 -> округлено до 5
    {"Asia/Bangkok (Thailand)", "Asia/Bangkok", 7, false, false},
    {"Europe/Warsaw (Poland) - DST", "Europe/Warsaw", 1, false, true},  // CET = UTC+1 (зимой)
    {"Europe/Kyiv (Ukraine) - DST", "Europe/Kyiv", 2, false, true},    // EET = UTC+2 (зимой)
    {"Europe/Berlin (Germany) - DST", "Europe/Berlin", 1, false, true},
    {"Europe/Paris (France) - DST", "Europe/Paris", 1, false, true},
    {"Europe/London (UK) - DST", "Europe/London", 0, false, true},     // GMT = UTC+0 (зимой)
    {"Europe/Moscow (Russia)", "Europe/Moscow", 3, false, false},
    {"Europe/Madrid (Spain) - DST", "Europe/Madrid", 1, false, true},
    {"Europe/Rome (Italy) - DST", "Europe/Rome", 1, false, true},
    {"America/New_York (USA-East) - DST", "America/New_York", -5, false, true},  // EST = UTC-5 (зимой)
    {"America/Chicago (USA-Central) - DST", "America/Chicago", -6, false, true},
    {"America/Denver (USA-Mountain) - DST", "America/Denver", -7, false, true},
    {"America/Los_Angeles (USA-West) - DST", "America/Los_Angeles", -8, false, true},
    {"America/Anchorage (Alaska)", "America/Anchorage", -9, false, true},
    {"America/Halifax (Canada-Atlantic) - DST", "America/Halifax", -4, false, true},
    {"America/Toronto (Canada-East) - DST", "America/Toronto", -5, false, true},
    {"America/Vancouver (Canada-West) - DST", "America/Vancouver", -8, false, true},
    {"America/Sao_Paulo (Brazil)", "America/Sao_Paulo", -3, false, false},
    {"America/Buenos_Aires (Argentina)", "America/Argentina/Buenos_Aires", -3, false, false},
    {"Africa/Cairo (Egypt)", "Africa/Cairo", 2, false, false},
    {"Africa/Johannesburg (South Africa)", "Africa/Johannesburg", 2, false, false},
    {"Australia/Sydney (Australia) - DST", "Australia/Sydney", 10, false, true},
    {"Pacific/Auckland (New Zealand) - DST", "Pacific/Auckland", 12, false, true},
};
static const int tz_preset_count = sizeof(tz_presets)/sizeof(tz_presets[0]);

// State for interactive timezone listing
static int tz_list_state = 0; // 0 = normal, 1 = top-level list shown, 2 = non-DST submenu shown
static int dst_zone_indices[sizeof(tz_presets)/sizeof(tz_presets[0])];
static int dst_zone_count = 0;
static int non_dst_indices[sizeof(tz_presets)/sizeof(tz_presets[0])];
static int non_dst_count = 0;

void handleTimeMenu(String command) {
    if (handleCommonMenuCommands(command, printTimeMenu)) { tz_list_state = 0; return; }
    else if (command.equals("time") || command.equals("t")) {
        printTime();
    }
    else if (command.equals("sync")) {
        syncTime(true);
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
        } else if (tz_list_state == 2) {
            // non-DST submenu: expect numeric selection relative to that submenu
            bool isNum = true;
            for (size_t i = 0; i < command.length(); ++i) if (!isDigit(command[i])) { isNum = false; break; }
            if (isNum && command.length() > 0) {
                int sel = command.toInt();
                if (sel >= 1 && sel <= non_dst_count) {
                    int presetIndex = non_dst_indices[sel-1];
                    if (setTimezoneOffset(tz_presets[presetIndex].offset)) {
                        Serial.printf("\nРучное смещение установлено: UTC%+d\n", tz_presets[presetIndex].offset);
                        saveConfig();
                    } else {
                        Serial.println("\nОшибка при установке смещения");
                    }
                    tz_list_state = 0;
                    return;
                }
                Serial.printf("\nНеверный номер. Введите число от 1 до %d\n", non_dst_count);
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

// ======================= МЕНЮ Будильников (уровень 2) =======================

static uint8_t parseDaysMask(const String &input) {
    String s = input;
    s.trim();
    s.toLowerCase();

    if (s == "weekdays" || s == "wd") return 0x1F; // Пн-Пт
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
    return (s == "weekdays" || s == "weekends" || s == "all" || s == "wd" || s == "we");
}

void printAlarmMenu() {
    //Serial.println("\n=== СОСТОЯНИЕ БУДИЛЬНИКОВ ===");
    // Показываем текущее состояние будильников
    printAlarmStatus();
    
    Serial.println("\n  set al 1 [HH:MM] / sal1 [HH:MM] - время будильника 1");
    Serial.println("  al 1 sound [num] / a1s [num] - номер мелодии для будильника 1");
    Serial.println("  al 1 mode [once/daily] / a1m [o/d] - режим будильника 1 (один раз/ежедневно)");
        Serial.println("  dis al 1 / da1 - отключить будильник 1");
    Serial.println("\n  set al 2 [HH:MM] / sal2 [HH:MM] - время будильника 2");
    Serial.println("  al 2 sound [num] / a2s [num] - номер мелодии для будильника 2");
    Serial.println("  al 2 days [1,2,3...7] / a2d [1,2,3...7] - срабатывание буд. 2, начиная с Пн=1 по Вс=7");
    Serial.println("  al 2 list [weekdays|weekends|all] / a2l [...] - набор дней будильника 2 (по будням, выходным, все)");
        Serial.println("  dis al 2 / da2 - отключить будильник 2");
    
    printMappingMenuCommands();  //Управление меню
}

void handleAlarmMenu(String command) {
    if (handleCommonMenuCommands(command, printAlarmMenu)) return;
    else if (command.equalsIgnoreCase("al") || command.equalsIgnoreCase("AL") || command.equals("status")) {
        // Показать состояние будильников
        printAlarmStatus();
    }
    else if (command.startsWith("sal1") || command.startsWith("sal2")) {
        int alarmNum = command.startsWith("sal1") ? 1 : 2;
        String timeStr = command.substring(4);
        timeStr.trim();
        if (timeStr.length() == 0) {
            Serial.println("Нужно указать время в формате HH:MM");
        } else {
            if (setAlarm(alarmNum, timeStr)) {
                // Сообщение уже выводится в setAlarm()
            } else {
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
            Serial.println("Нужно указать: weekdays, weekends или all");
        } else if (!isDaysKeyword(daysStr) && !containsDayDigits(daysStr)) {
            Serial.println("Неверный формат. Используйте weekdays/weekends/all или список 1-7");
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
            if (disableAlarm(alarmNum)) {
                // Сообщение уже выводится в disableAlarm()
            } else {
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
                if (disableAlarm(alarmNum)) {
                    // Сообщение уже выводится в disableAlarm()
                } else {
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
                if (setAlarm(alarmNum, timeStr)) {
                    // Сообщение уже выводится в setAlarm()
                }
                else {
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
                String numStr = action.substring(5);
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
                    Serial.println("Нужно указать: weekdays, weekends или all");
                } else if (!isDaysKeyword(daysStr) && !containsDayDigits(daysStr)) {
                    Serial.println("Неверный формат. Используйте weekdays/weekends/all или список 1-7");
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
                if (enableAlarm(alarmNum)) Serial.printf("Будильник %d включён\n", alarmNum);
                else Serial.println("Не удалось включить будильник");
            }
            else if (action.equals("disable") || action.equals("off")) {
                if (disableAlarm(alarmNum)) Serial.printf("Будильник %d отключён\n", alarmNum);
                else Serial.println("Не удалось отключить будильник");
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

// ======================= МЕНЮ WIFI/NTP (уровень 2) =======================

void printWifiMenu() {
    Serial.println("\n=== WI-FI И NTP ===");

    Serial.println("\nТекущие параметры:");
    Serial.printf("  WiFi SSID (сеть 1): %s\n", strlen(config.wifi_ssid) > 0 ? config.wifi_ssid : "(не установлено)");
    Serial.printf("  WiFi SSID (сеть 2, резервная): %s\n", strlen(config.wifi_ssid_2) > 0 ? config.wifi_ssid_2 : "(не установлено)");
    Serial.printf("  NTP сервер: %s\n", config.ntp_server);
    if (config.time_config.last_ntp_sync) {
        time_t t = (time_t)config.time_config.last_ntp_sync;
        struct tm tm;
        localtime_r(&t, &tm);
        Serial.printf("  Время последней синхронизации: %02d:%02d\n", tm.tm_hour, tm.tm_min);
    } else {
        Serial.println("  Время последней синхронизации: (нет данных)");
    }

    Serial.println("\nКоманды:");
    Serial.println("  wifi scan     - Сканировать доступные сети");
    Serial.println("  wifi [SSID] [PASSWORD] - Данные основной сети WIFI (сеть 1)");
    Serial.println("  wifi2 [SSID] [PASSWORD] - Данные резервной сети WIFI (сеть 2)");
    Serial.println("  set ntp <SERVER> - Задать NTP сервер");

    printMappingMenuCommands();  //Управление меню
}


void handleWifiMenu(String command) {
    if (handleCommonMenuCommands(command, printWifiMenu)) return;
    else if (command.equals("wifi scan")) {
        Serial.println("\nСканирование доступных WiFi сетей...");
        int n = WiFi.scanNetworks();
        if (n == 0) {
            Serial.println("Нет доступных сетей");
        } else {
            Serial.printf("Найдено %d сетей:\n", n);
            for (int i = 0; i < n; ++i) {
                Serial.printf("%d: %s (RSSI: %d dBm) %s\n", i + 1, WiFi.SSID(i).c_str(), WiFi.RSSI(i),
                              (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "Open" : "");
            }
        }
    }
    else if (command.startsWith("wifi ")) {
        String args = command.substring(5);
        int spaceIdx = args.indexOf(' ');
        if (spaceIdx == -1) {
            Serial.println("Нужно указать SSID и пароль");
        }
        else {
            String ssid = args.substring(0, spaceIdx);
            String password = args.substring(spaceIdx + 1);
            ssid.trim();
            password.trim();
            strncpy(config.wifi_ssid, ssid.c_str(), sizeof(config.wifi_ssid) - 1);
            strncpy(config.wifi_pass, password.c_str(), sizeof(config.wifi_pass) - 1);
            config.wifi_ssid[sizeof(config.wifi_ssid) - 1] = '\0';
            config.wifi_pass[sizeof(config.wifi_pass) - 1] = '\0';
            Serial.printf("Данные для подключения к WiFi (сеть 1) установлены: SSID='%s'\n", config.wifi_ssid);
            saveConfig();
            Serial.println("WiFi настройки сохранены во flash");
        }
    }
    else if (command.startsWith("wifi2 ")) {
        String args = command.substring(6);
        int spaceIdx = args.indexOf(' ');
        if (spaceIdx == -1) {
            Serial.println("Нужно указать SSID и пароль");
        }
        else {
            String ssid = args.substring(0, spaceIdx);
            String password = args.substring(spaceIdx + 1);
            ssid.trim();
            password.trim();
            strncpy(config.wifi_ssid_2, ssid.c_str(), sizeof(config.wifi_ssid_2) - 1);
            strncpy(config.wifi_pass_2, password.c_str(), sizeof(config.wifi_pass_2) - 1);
            config.wifi_ssid_2[sizeof(config.wifi_ssid_2) - 1] = '\0';
            config.wifi_pass_2[sizeof(config.wifi_pass_2) - 1] = '\0';
            Serial.printf("Данные для подключения к WiFi (сеть 2, резервная) установлены: SSID='%s'\n", config.wifi_ssid_2);
            saveConfig();
            Serial.println("WiFi настройки сохранены во flash");
        }
    }
    else if (command.startsWith("set ntp ")) {
        String server = command.substring(8);
        server.trim();
        strncpy(config.ntp_server, server.c_str(), sizeof(config.ntp_server) - 1);
        config.ntp_server[sizeof(config.ntp_server) - 1] = '\0';
        saveConfig();
        Serial.printf("NTP сервер установлен на: %s\n", config.ntp_server);

        // Если WiFi ещё не настроен, уведомляем и не пытаемся синхронизировать
        if (strlen(config.wifi_ssid) == 0) {
            Serial.println("\nWiFi не настроен, синхронизация NTP невозможна (установите WiFi)");
        } else {
            Serial.print("Пытаюсь синхронизироваться с новым NTP сервером...");
            if (syncTime(true)) {
                Serial.println("\nСинхронизация NTP выполнена успешно\n");
            } else {
                Serial.println("\nНе удалось синхронизироваться с NTP сервером\n");
            }
        }
    }
    else {
        Serial.println("Неизвестная команда. Введите 'help' для справки");
    }
}

// ======================= МЕНЮ ИНФОРМАЦИИ (уровень 2) =======================

void printInfoMenu() {
    
    Serial.println("\n=== Системная информация ===");
    Serial.printf("Версия ПО: %s\n", FIRMWARE_VERSION);
    Serial.printf("Серийный номер устройства: %s\n", config.serial_number);
//  Serial.printf("Часовой пояс: UTC%+d\n", config.time_config.timezone_offset);
    Serial.printf("Источник времени: %s", 
               currentTimeSource == EXTERNAL_DS3231 ? "DS3231" : "Внутренний RTC");
        printDS3231Temperature();   // Температура DS3231

    Serial.printf("\nWiFi SSID: %s\n", config.wifi_ssid);
    Serial.printf("NTP сервер: %s\n", config.ntp_server);

    

    Serial.println("\n\n=== Информация о ESP32 ===");
    
    // Информация о чипе
    Serial.printf("\nESP-ROM: %s", ESP.getChipModel());
    Serial.printf("\nCPU Частота: %d MHz\n", ESP.getCpuFreqMHz());

    Serial.printf("IDF версия: %s\n", esp_get_idf_version());
    Serial.printf("Cores: %d\n", ESP.getChipCores());
    Serial.printf("Revision: %d\n", ESP.getChipRevision());
    
    // Информация о флеш памяти
    uint32_t flashSize = ESP.getFlashChipSize();
    uint32_t sketchSize = ESP.getSketchSize();
    uint32_t freeSketch = ESP.getFreeSketchSpace();
    uint32_t appPartition = sketchSize + freeSketch;

    Serial.printf("\nFlash Size: %d MB\n", flashSize / (1024 * 1024));
    Serial.printf("App partition size: %.2f MB\n", appPartition / (1024.0 * 1024.0));
    Serial.printf("Sketch size: %.2f MB\n", sketchSize / (1024.0 * 1024.0));
    Serial.printf("App free space: %.2f MB\n", freeSketch / (1024.0 * 1024.0));

    Serial.printf("App usage: %.1f%%\n", (sketchSize * 100.0) / appPartition);
    
    printMappingMenuCommands();  //Управление меню    
    
}

void handleInfoMenu(String command) {
    if (handleCommonMenuCommands(command, printInfoMenu)) return;
    else {
        Serial.println("Неизвестная команда. Введите 'help' для справки");
    }
}

// ======================= МЕНЮ КОНФИГУРАЦИИ (уровень 2) =======================

void printConfigMenu() {
    Serial.println("\n=== ТЕКУЩАЯ КОНФИГУРАЦИЯ ===");
    auto formatDays = [](uint8_t mask) {
        if (mask == 0) return String("нет");
        const char* names[7] = {"Пн", "Вт", "Ср", "Чт", "Пт", "Сб", "Вс"};
        String out;
        for (uint8_t i = 0; i < 7; ++i) {
            if (mask & (1 << i)) {
                out += names[i];
                if (out.length() > 1) out += ", ";
                
            }
        }
        return out;
    };

    Serial.println();
    Serial.printf("  • Сеть 1: %s\n", strlen(config.wifi_ssid) ? config.wifi_ssid : "(не установлена)");
    Serial.printf("  • Сеть 2: %s\n", strlen(config.wifi_ssid_2) ? config.wifi_ssid_2 : "(не установлена)");
    Serial.printf("  • NTP сервер: %s\n", strlen(config.ntp_server) ? config.ntp_server : "(не установлен)");
    Serial.printf("  • Часовой пояс: %s\n", strlen(config.time_config.timezone_name) ? config.time_config.timezone_name : "(не установлен)");
        Serial.printf("  • Автосинхронизация по UTC: %s\n", 
                      config.time_config.auto_sync_enabled ? "ВКЛЮЧЕНА" : "ОТКЛЮЧЕНА");
    if (config.time_config.automatic_localtime && config.time_config.auto_sync_enabled) {
        Serial.print("  • Локальное время: ИНТЕРНЕТ + проверка актуальности таблицы\n");
    } else if (config.time_config.automatic_localtime && !config.time_config.auto_sync_enabled) {
        Serial.print("  • Локальное время: ТАБЛИЦА (т.к. автосинхронизация отключена)\n");
    } else {
        Serial.print("  • Локальное время: ТАБЛИЦА / НОВОЕ ПРАВИЛО DST - если есть\n");
    }
    Serial.printf("  • Будильник 1: %s %02d:%02d, мелодия %d, %s\n",
                  config.alarm1.enabled ? "ВКЛ" : "ВЫКЛ",
                  config.alarm1.hour,
                  config.alarm1.minute,
                  config.alarm1.melody,
                  config.alarm1.once ? "once" : "daily");
    Serial.printf("  • Будильник 2: %s %02d:%02d, мелодия %d, дни: %s\n",
                  config.alarm2.enabled ? "ВКЛ" : "ВЫКЛ",
                  config.alarm2.hour,
                  config.alarm2.minute,
                  config.alarm2.melody,
                  formatDays(config.alarm2.days_mask).c_str());

    Serial.println("\n   default  - Сбросить к настройкам по умолчанию");

    printMappingMenuCommands();  //Управление меню
}

void handleConfigMenu(String command) {
    if (handleCommonMenuCommands(command, printConfigMenu)) return;
    else if (command.equals("default")) {
        setDefaultConfig();
        saveConfig();
        printConfigMenu();
    }
    else {
        Serial.println("Неизвестная команда. Введите 'help' для справки");
    }
}

// ======================= БАЗОВЫЕ КОМАНДЫ (работают всегда) =======================

void printQuickHelp() {
    Serial.println("\n=== ОСНОВНЫЕ КОМАНДЫ ===");

    Serial.println("  time / t     - Текущее время");
    Serial.println("  sync        - Синхронизировать с NTP");
    Serial.println("  menu / m     - Главное меню");
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

void setManualTimezone(String tz_name) {
    tz_name.trim();
    if (tz_name.length() == 0) {
        Serial.println("Укажите имя часового пояса либо номер пресета, например '1' или 'Europe/Moscow'");
        return;
    }

    // If we're in top-level list mode and user entered a plain number, handle selection
    if (tz_list_state == 1) {
        bool isNumber = true;
        for (size_t i = 0; i < tz_name.length(); ++i) if (!isDigit(tz_name[i])) isNumber = false;
        if (isNumber) {
            int num = tz_name.toInt();
            if (num == 1) {
                // Показываем список из новой системы
                ::listAvailableTimezones();
                return;
            }
            if (num >= 2) {
                int idx = num - 2;
                if (idx >= 0 && idx < dst_zone_count) {
                    int presetIndex = dst_zone_indices[idx];
                    if (setTimezone(tz_presets[presetIndex].zone_name)) {
                        Serial.printf("Пояс установлен: %s\n", tz_presets[presetIndex].zone_name);
                        onTimezoneActivated();
                        tz_list_state = 0;
                        return;
                    } else {
                        Serial.println("Не удалось установить выбранный пояс");
                        tz_list_state = 0;
                        return;
                    }
                }
            }
        }
    }

    // Try to parse as an integer preset index (legacy behaviour 'tz set <N>')
    bool isNumber = true;
    for (size_t i = 0; i < tz_name.length(); ++i) if (!isDigit(tz_name[i])) isNumber = false;

    if (isNumber) {
        int idx = tz_name.toInt();
        
        // Специальная обработка для номера 100 - ручная настройка
        if (idx == 100) {
            setupManualOffset();
            return;
        }

        if (idx < 1 || idx > tz_preset_count) {
            Serial.println("Неверный номер. Введите число от 1 до 42 или 100");
            return;
        }
        const TZPreset &p = tz_presets[idx-1];
        if (p.is_offset) {
            if (setTimezoneOffset(p.offset)) {
                Serial.printf("Смещение установлено: UTC%+d\n", p.offset);
                Serial.println("Если требуется DST — включите ('tz dst on') вручную");
                saveConfig();
            } else {
                Serial.println("Ошибка при установке смещения пресета");
            }
        } else {
            // Именованная зона - пытаемся через ezTime
            if (setTimezone(p.zone_name)) {
                Serial.printf("Пояс установлен через ezTime: %s\n", p.zone_name);
                Serial.printf("Поддержка DST: %s\n", p.has_dst ? "YES" : "NO");
                onTimezoneActivated();
            } else {
                // Fallback на ручное смещение из пресета
                Serial.printf("[TZ] ezTime недоступен, используем fallback смещение UTC%+d для %s\n", 
                             p.offset, p.zone_name);
                if (setTimezoneOffset(p.offset)) {
                    Serial.printf("Установлено ручное смещение: UTC%+d (%s)\n", p.offset, p.zone_name);
                    // Сохраняем имя зоны для информации
                    strncpy(config.time_config.timezone_name, p.zone_name, 
                           sizeof(config.time_config.timezone_name));
                    config.time_config.timezone_name[sizeof(config.time_config.timezone_name)-1] = '\0';
                    Serial.printf("Поддержка DST: %s (управление вручную через 'tz dst on/off')\n", 
                                 p.has_dst ? "YES" : "NO");
                    saveConfig();
                } else {
                    Serial.println("Ошибка при установке fallback смещения");
                }
            }
        }
        return;
    }

    // Otherwise treat as a direct zone name
    if (setTimezone(tz_name.c_str())) {
        Serial.print("Пояс установлен: ");
        Serial.println(tz_name);
        onTimezoneActivated();
    } else {
        // Also allow formats like 'UTC+3' or numeric offsets prefixed with +/−
        if ((tz_name.startsWith("UTC") || tz_name.startsWith("utc")) && tz_name.length() > 3) {
            String off = tz_name.substring(3);
            off.trim();
            int val = off.toInt();
            if (setTimezoneOffset((int8_t)val)) {
                Serial.printf("Смещение установлено: UTC%+d\n", val);
                saveConfig();
                return;
            }
        }

        Serial.print("Не удалось установить пояс '");
        Serial.print(tz_name);
        Serial.println("'. Можно задать смещение вручную: 'tz offset N' или выбрать из списка: 'tz list'");
    }
}

void setManualTimezoneOffset(int offset) {
    if (setTimezoneOffset((int8_t)offset)) {
        Serial.print("Ручное смещение установлено: ");
        Serial.print(offset);
        Serial.println(" часов");
        saveConfig();
    } else {
        Serial.println("Ошибка при установке смещения");
    }
}

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
    Serial.println("  menu / m      - Главное меню");
    Serial.println("  out / o       - Выход из режима настройки");
    Serial.println("==========================\n");
}