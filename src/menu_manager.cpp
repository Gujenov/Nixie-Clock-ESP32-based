#include "menu_manager.h"
#include "config.h"
#include "time_utils.h"
#include "hardware.h"
#include "alarm_handler.h"
#include <Arduino.h> 

extern bool printEnabled;

MenuState currentMenuState = MENU_MAIN;
bool inMenuMode = false;

// ======================= УПРАВЛЕНИЕ РЕЖИМОМ МЕНЮ =======================

void enterMenuMode() {
    
    // ======================= ГЛАВНОЕ МЕНЮ (уровень 1) =======================

    inMenuMode = true;
    printEnabled = false;  // Останавливаем авто-вывод времени
    Serial.println("\n====================");
    Serial.println("ВНИМАНИЕ!");
    Serial.println("Устройство в режиме настройки.");
    Serial.println("Автоматический вывод времени остановлен.");
    Serial.println("Для выхода введите 'exit'.");
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
    Serial.println("\nКоманды:");
    Serial.println("  time, t      - Текущее время (UTC и локальное)");
    Serial.println("  AL           - Показать состояние будильников");
    Serial.println("  sync         - Синхронизировать с NTP");
    Serial.println("  set T HH:MM:SS   - Установить время");
    Serial.println("  set D DD.MM.YYYY - Установить дату");
    
    Serial.println("\nЧасовые пояса:");
    Serial.println("  tz           - Информация о текущем поясе");
    Serial.println("  tz list      - Список доступных поясов");
    Serial.println("  tz set NAME  - Установить пояс (Europe/Moscow)");
    Serial.println("  tz auto      - Включить автоопределение");
    Serial.println("  tz manual    - Отключить автоопределение");

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

void handleTimeMenu(String command) {
    if (handleCommonMenuCommands(command, printTimeMenu)) return;
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
    /*else if (command.equals("tz") || command.startsWith("tz ")) {
        handleTimezoneCommand(command.substring(3));
    }*/
    else {
        Serial.println("Неизвестная команда. Введите 'help' для справки");
    }
}

// ======================= МЕНЮ Будильников (уровень 2) =======================

void printAlarmMenu() {
    Serial.println("\n=== СОСТОЯНИЕ БУДИЛЬНИКОВ ===");
    // Показываем текущее состояние будильников
    printAlarmStatus();
    
    Serial.println("  set al 1 [HH:MM] - установить время будильника 1");
    Serial.println("  al 1 sound [num] - установить номер мелодии для будильника 1");
    Serial.println("  set al 2 [HH:MM] - установить время будильника 2");
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

    Serial.println("\n\n=== ESP32-S3 Информация ===");
    
    // Информация о чипе
    Serial.printf("ESP-ROM: %s\n", ESP.getChipModel());
    Serial.printf("CPU Частота: %d MHz\n", ESP.getCpuFreqMHz());

    Serial.printf("IDF версия: %s\n", esp_get_idf_version());
    Serial.printf("Cores: %d\n", ESP.getChipCores());
    Serial.printf("Revision: %d\n", ESP.getChipRevision());
    
    // Информация о флеш памяти
    Serial.printf("Flash Size: %d MB\n", ESP.getFlashChipSize() / (1024 * 1024));
    Serial.printf("Flash usage: %.1f%%\n", 
              (ESP.getSketchSize() * 100.0) / ESP.getFlashChipSize());
    Serial.printf("Scetch size: %d bytes\n", ESP.getSketchSize());
    Serial.printf("Free space: %d bytes\n", ESP.getFreeSketchSpace());

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
        Serial.println("Конфигурация сброшена к значениям по умолчанию");
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

// ======================= ОБРАБОТКА ЧАСОВЫХ ПОЯСОВ =======================
/*
void handleTimezoneCommand(String args) {
    args.trim();
    
    if (args.equals("") || args.equals("info")) {
        printTimezoneInfo();
    }
    else if (args.equals("list")) {
        listAvailableTimezones();
    }
    else if (args.startsWith("set ")) {
        String tz_name = args.substring(4);
        setManualTimezone(tz_name);
    }
    else if (args.startsWith("offset ")) {
        String offset_str = args.substring(7);
        int offset = offset_str.toInt();
        setManualTimezoneOffset(offset);
    }
    else if (args.equals("auto")) {
        enableAutoTimezone();
    }
    else if (args.equals("manual")) {
        disableAutoTimezone();
    }
    else {
        Serial.println("Неизвестная команда часового пояса");
    }
}
*/
// Вспомогательные функции (заглушки, нужно реализовать)

void printSettings() {
    Serial.println("\n=== ТЕКУЩАЯ КОНФИГУРАЦИЯ ===");
    Serial.println("(Функционал ещё не реализован)");
}

void printMappingMenuCommands() {
    Serial.println("\nУправление:");
    Serial.println("  back, b      - Назад в главное меню");
    Serial.println("  help, ?      - Показать это сообщение");
    Serial.println("  out, o       - Выход из режима настройки");
    Serial.println("==========================\n");
}