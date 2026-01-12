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

    printMappingMenuCommands();  //Управление меню
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
    Serial.println("  time, t           - Текущее время (UTC и локальное)");
    Serial.println("  sync              - Синхронизировать с NTP");
    Serial.println("  set T HH:MM:SS    - Установить время");
    Serial.println("  set D DD.MM.YYYY  - Установить дату");
    
    Serial.println("\nУстановки поясов:");
    //Serial.println("  tz                - Информация о текущем поясе и настройке (ручной/авто)");
    Serial.println("  tz list, tzl      - Выбор доступных поясов");
    Serial.println("  tz auto, tza      - Автоматическое определение пояса");
    Serial.println("  tz manual, tzm    - Отключить автоопределение");

    printMappingMenuCommands();  //Управление меню
}

bool handleCommonMenuCommands(const String &command, void (*printMenu)()) {
    if (command.equals("back") || command.equals("b")) {
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
        syncTime();
    }
    else if (command.startsWith("set T ")) {
        String timeStr = command.substring(6);
        setManualTime(timeStr);
    }
    else if (command.startsWith("set D ")) {
        String dateStr = command.substring(6);
        setManualDate(dateStr);
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
                uint8_t count = getPresetsCount();
                
                if (num >= 1 && num <= count) {
                    // Получаем preset по индексу (индекс = номер - 1)
                    const TimezonePreset* preset = getPresetByIndex(num - 1);
                    if (preset) {
                        // Устанавливаем часовой пояс
                        if (setTimezone(preset->zone_name)) {
                            Serial.printf("\n[TZ] Установлена зона: %s\n", preset->display_name);
                            saveConfig();
                        } else {
                            Serial.printf("\n[TZ] Ошибка при установке зоны: %s\n", preset->zone_name);
                        }
                        tz_list_state = 0;
                        return;
                    }
                } else {
                    Serial.printf("\nНеверный номер. Введите число от 1 до %d\n", count);
                    tz_list_state = 0;
                    return;
                }
            }
            
            // Разрешаем ввод имени зоны напрямую (например, 'Europe/Warsaw')
            if (command.indexOf('/') >= 0 || command.equals("CET")) {
                if (setTimezone(command.c_str())) {
                    Serial.printf("\n[TZ] Установлена зона: %s\n", command.c_str());
                    saveConfig();
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
        if (cmdLower.equals("tza")) {
            enableAutoTimezone();
            tz_list_state = 0;
            return;
        }
        if (cmdLower.equals("tzm")) {
            disableAutoTimezone();
            tz_list_state = 0;
            return;
        }

        Serial.println("Неизвестная команда. Введите 'help' для справки");
    }
}

// ======================= МЕНЮ Будильников (уровень 2) =======================

void printAlarmMenu() {
    //Serial.println("\n=== СОСТОЯНИЕ БУДИЛЬНИКОВ ===");
    // Показываем текущее состояние будильников
    printAlarmStatus();
    
    Serial.println("  set al 1 [HH:MM] - установить время будильника 1");
    Serial.println("  al 1 sound [num] - установить номер мелодии для будильника 1");
    Serial.println("\n  set al 2 [HH:MM] - установить время будильника 2");
    Serial.println("  al 2 sound [num] - установить номер мелодии для будильника 2");
    
    printMappingMenuCommands();  //Управление меню
}

void handleAlarmMenu(String command) {
    if (handleCommonMenuCommands(command, printAlarmMenu)) return;
    else if (command.equalsIgnoreCase("al") || command.equalsIgnoreCase("AL") || command.equals("status")) {
        // Показать состояние будильников
        printAlarmStatus();
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
                    Serial.printf("Будильник %d установлен на %s\n", alarmNum, timeStr.c_str());
                }
                else {
                    Serial.println("Неверный формат времени. Используйте HH:MM");
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
            if (action.startsWith("sound ")) {
                Serial.println("Установка мелодии ещё не реализована");
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
    Serial.printf("  WiFi SSID: %s\n", config.wifi_ssid);
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
    Serial.println("  wifi [SSID] [PASSWORD] - Ввести данные для подключения к WiFi");
    Serial.println("  set ntp <SERVER> - Установить NTP сервер");

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
            Serial.printf("Данные для подключения к WiFi установлены: SSID='%s'\n", config.wifi_ssid);
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
            if (syncTime()) {
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
    Serial.printf("Источник времени: %s\n", 
               currentTimeSource == EXTERNAL_DS3231 ? "DS3231" : "Внутренний RTC");
   
    Serial.printf("\nWiFi SSID: %s\n", config.wifi_ssid);
    Serial.printf("NTP сервер: %s\n", config.ntp_server);

    printDS3231Temperature();   // Температура DS3231

    Serial.println("\n\n=== Информация о ESP32 ===");
    
    // Информация о чипе
    Serial.printf("\nESP-ROM: %s", ESP.getChipModel());
    Serial.printf("\nCPU Частота: %d MHz\n", ESP.getCpuFreqMHz());

    Serial.printf("IDF версия: %s\n", esp_get_idf_version());
    Serial.printf("Cores: %d\n", ESP.getChipCores());
    Serial.printf("Revision: %d\n", ESP.getChipRevision());
    
    // Информация о флеш памяти
    Serial.printf("Flash Size: %d MB\n", ESP.getFlashChipSize() / (1024 * 1024));
    Serial.printf("Flash usage: %.1f%%\n", 
              (ESP.getSketchSize() * 100.0) / ESP.getFlashChipSize());
    double sketchMB = ESP.getSketchSize() / (1024.0 * 1024.0);
    double freeMB = ESP.getFreeSketchSpace() / (1024.0 * 1024.0);
    Serial.printf("Sketch size: %.2f MB\n", sketchMB);
    Serial.printf("Free space: %.2f MB\n", freeMB);

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
    Serial.println("\n=== УПРАВЛЕНИЕ КОНФИГУРАЦИЕЙ ===");
    Serial.println("\nКоманды:");
    Serial.println("   show     - Показать текущую конфигурацию");
    Serial.println("   save     - Сохранить конфигурацию");
    Serial.println("   default  - Сбросить к настройкам по умолчанию");

    printMappingMenuCommands();  //Управление меню
}

void handleConfigMenu(String command) {
    if (handleCommonMenuCommands(command, printConfigMenu)) return;
    else if (command.equals("default")) {
        setDefaultConfig();
        saveConfig();
    }
    else if (command.equals("save")) {
        saveConfig();
        Serial.println("Конфигурация сохранена");
    }
    else if (command.equals("show")) {
        printSettings();
    }
    else {
        Serial.println("Неизвестная команда. Введите 'help' для справки");
    }
}

// ======================= БАЗОВЫЕ КОМАНДЫ (работают всегда) =======================

void printQuickHelp() {
    Serial.println("\n=== ОСНОВНЫЕ КОМАНДЫ ===");

    Serial.println("  time, t     - Текущее время");
    Serial.println("  sync        - Синхронизировать с NTP");
    Serial.println("  menu, m     - Войти в режим настройки");
    Serial.println("  help, ?     - Это сообщение");
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

// printTimezoneInfo() теперь определена в timezone_manager.cpp

/* СТАРАЯ ФУНКЦИЯ - ЗАКОММЕНТИРОВАНА
void listNonDSTPresets() {
    // Build non-DST index if not built
    non_dst_count = 0;
    for (int i = 0; i < tz_preset_count; ++i) {
        if (tz_presets[i].is_offset) {
            non_dst_indices[non_dst_count++] = i;
        }
    }

    Serial.println("\n=== СПИСОК ДОСТУПНЫХ ЧАСОВЫХ ПОЯСОВ БЕЗ ПЕРЕХОДА НА ЛЕТНЕЕ/ЗИМНЕЕ ВРЕМЯ ===\n");
    // Print offsets without the word 'Offset'
    for (int k = 0; k < non_dst_count; ++k) {
        int idx = non_dst_indices[k];
        const char* disp = tz_presets[idx].display;
        String s = disp;
        // strip prefix "Offset: " if present
        if (s.startsWith("Offset:")) {
            s = s.substring(7);
            s.trim();
        }
        Serial.printf("%d. %s\n", k+1, s.c_str());
    }

    Serial.println("\nВыбор пояса осуществляется по номеру.");
    Serial.println();
    Serial.println("Навигация:");
    Serial.println("  back, b      - Назад в главное меню");
    Serial.println("  help, ?      - Показать это сообщение");
    Serial.println("  out, o       - Выход из режима настройки");
    Serial.println("==========================");

    tz_list_state = 2; // now in non-DST submenu
}
*/  // КОНЕЦ ЗАКОММЕНТИРОВАННОЙ listNonDSTPresets

/* СТАРАЯ ФУНКЦИЯ - ЗАКОММЕНТИРОВАНА
void listAvailableTimezones() {
    // Build indexes
    dst_zone_count = 0;
    non_dst_count = 0;
    for (int i = 0; i < tz_preset_count; ++i) {
        if (tz_presets[i].is_offset) {
            non_dst_indices[non_dst_count++] = i;
        } else if (tz_presets[i].has_dst) {
            dst_zone_indices[dst_zone_count++] = i;
        }
    }

    Serial.println("\n=== СПИСОК ДОСТУПНЫХ ПРЕСЕТОВ ЧАСОВЫХ ПОЯСОВ ===\n");
    Serial.println("1. Список поясов без перехода на летнее/зимнее время (без DST).\n");
    Serial.println("Пояса локального времени с переходом на летнее/зимнее время");

    // Numbering: 2.. for DST zones
    for (int k = 0; k < dst_zone_count; ++k) {
        int idx = dst_zone_indices[k];
        Serial.printf("%d. %s\n", k + 2, tz_presets[idx].zone_name ? tz_presets[idx].zone_name : tz_presets[idx].display);
    }

    Serial.println("\nВыбор пояса осуществляется по номеру либо по имени.");
    Serial.println();
    Serial.println("Навигация:");
    Serial.println("  back, b      - Назад в главное меню");
    Serial.println("  help, ?      - Показать это сообщение");
    Serial.println("  out, o       - Выход из режима настройки");
    Serial.println("==========================");

    tz_list_state = 1; // waiting for top-level selection
}
*/  // КОНЕЦ ЗАКОММЕНТИРОВАННОЙ listAvailableTimezones

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
                        saveConfig();
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

        if (idx < 1 || idx > tz_preset_count) {
            Serial.println("Неверный номер пресета");
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
                saveConfig();
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
        saveConfig();
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
    config.time_config.automatic_localtime = true;
    initTimezone(); // попытаться загрузить ezTime
    saveConfig();
    Serial.println("Режим ezTime (online) включен");
}

void disableAutoTimezone() {
    config.time_config.automatic_localtime = false;
    initTimezone(); // переключиться на локальную таблицу
    saveConfig();
    Serial.println("Режим локальной таблицы (offline) включен");
}

void printMappingMenuCommands() {
    Serial.println("\nНавигация:");
    Serial.println("  back, b      - Назад в главное меню");
    Serial.println("  help, ?      - Показать это сообщение");
    Serial.println("  out, o       - Выход из режима настройки");
    Serial.println("==========================\n");
}