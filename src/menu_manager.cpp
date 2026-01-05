#include "menu_manager.h"
#include "config.h"
#include "time_utils.h"
#include "hardware.h"
#include <Arduino.h>

extern bool printEnabled;

MenuState currentMenuState = MENU_MAIN;
bool inMenuMode = false;

// ======================= УПРАВЛЕНИЕ РЕЖИМОМ МЕНЮ =======================

void enterMenuMode() {
    inMenuMode = true;
    printEnabled = false;  // Останавливаем авто-вывод времени
    Serial.println("\n\n=== РЕЖИМ НАСТРОЙКИ ===");
    Serial.println("Автоматический вывод времени остановлен");
    printMainMenu();
}

void exitMenuMode() {
    inMenuMode = false;
    printEnabled = true;   // Возобновляем авто-вывод
    currentMenuState = MENU_MAIN;
    Serial.println("\n=== РЕЖИМ НАСТРОЙКИ ЗАВЕРШЕН ===");
    Serial.println("Автоматический вывод времени возобновлён");
}

bool isInMenuMode() {
    return inMenuMode;
}

// ======================= ГЛАВНОЕ МЕНЮ (уровень 1) =======================

void printMainMenu() {
    Serial.println("\n=== ДОСТУПНЫЕ КОМАНДЫ ===");
    Serial.println("ВНИМАНИЕ! До подачи команды 'out' выдача времени остановлена");
    Serial.println("\nВыберите требуемый список команд 1-5:");
    Serial.println("\n1. time, T   - Настройки времени и часовых поясов");
    Serial.println("2. WF        - Настройки WI-FI и NTP");
    Serial.println("3. info      - Информация о системе");
    Serial.println("4. config    - Управление конфигурацией");
    Serial.println("5. out       - Выход из режима настройки");
    Serial.println("\nhelp, ?     - показать это сообщение");
    Serial.println("==========================");
}

void handleMainMenu(String command) {
    if (command.equals("1")) {
        currentMenuState = MENU_TIME;
        printTimeMenu();
    }
    else if (command.equals("2")) {
        currentMenuState = MENU_WIFI;
        printWifiMenu();
    }
    else if (command.equals("3")) {
        currentMenuState = MENU_INFO;
        printInfoMenu();
    }
    else if (command.equals("4")) {
        currentMenuState = MENU_CONFIG;
        printConfigMenu();
    }
    else if (command.equals("5") || command.equals("out") || command.equals("exit")) {
        exitMenuMode();
    }
    else if (command.equals("help") || command.equals("?")) {
        printMainMenu();
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
    Serial.println("  sync         - Синхронизировать с NTP");
    Serial.println("  set time HH:MM:SS   - Установить время");
    Serial.println("  set date DD.MM.YYYY - Установить дату");
    Serial.println("\nЧасовые пояса:");
    Serial.println("  tz           - Информация о текущем поясе");
    Serial.println("  tz list      - Список доступных поясов");
    Serial.println("  tz set NAME  - Установить пояс (Europe/Moscow)");
    Serial.println("  tz offset +N - Ручное смещение (например: tz offset +3)");
    Serial.println("  tz auto      - Включить автоопределение");
    Serial.println("  tz manual    - Отключить автоопределение");
    Serial.println("\nУправление:");
    Serial.println("  back, b      - Назад в главное меню");
    Serial.println("  help, ?      - Показать это сообщение");
    Serial.println("  out          - Выход из режима настройки");
    Serial.println("==========================");
}

void handleTimeMenu(String command) {
    if (command.equals("back") || command.equals("b")) {
        currentMenuState = MENU_MAIN;
        printMainMenu();
    }
    else if (command.equals("out") || command.equals("exit")) {
        exitMenuMode();
    }
    else if (command.equals("help") || command.equals("?")) {
        printTimeMenu();
    }
    else if (command.equals("time") || command.equals("t")) {
        printTime();
    }
    else if (command.equals("sync")) {
        syncTime();
    }
    else if (command.startsWith("set time ")) {
        String timeStr = command.substring(9);
        setManualTime(timeStr);
    }
    else if (command.startsWith("set date ")) {
        String dateStr = command.substring(9);
        setManualDate(dateStr);
    }
    /*else if (command.equals("tz") || command.startsWith("tz ")) {
        handleTimezoneCommand(command.substring(3));
    }*/
    else {
        Serial.println("Неизвестная команда. Введите 'help' для справки");
    }
}

// ======================= МЕНЮ WIFI/NTP (уровень 2) =======================

void printWifiMenu() {
    Serial.println("\n=== НАСТРОЙКИ WI-FI И NTP ===");
    Serial.println("\nКоманды:");
    Serial.println("  wifi status   - Статус WiFi подключения");
    Serial.println("  wifi scan     - Сканировать доступные сети");
    Serial.println("  wifi connect SSID PASSWORD - Подключиться к WiFi");
    Serial.println("  wifi disconnect - Отключиться от WiFi");
    Serial.println("  ntp server    - Показать текущий NTP сервер");
    Serial.println("  ntp set SERVER - Установить NTP сервер");
    Serial.println("\nУправление:");
    Serial.println("  back, b      - Назад в главное меню");
    Serial.println("  help, ?      - Показать это сообщение");
    Serial.println("  out          - Выход из режима настройки");
    Serial.println("==========================");
}

void handleWifiMenu(String command) {
    if (command.equals("back") || command.equals("b")) {
        currentMenuState = MENU_MAIN;
        printMainMenu();
    }
    else if (command.equals("out") || command.equals("exit")) {
        exitMenuMode();
    }
    else if (command.equals("help") || command.equals("?")) {
        printWifiMenu();
    }
    else {
        // Здесь будет реализация WiFi команд
        Serial.println("WiFi/NTP функционал ещё не реализован");
    }
}

// ======================= МЕНЮ ИНФОРМАЦИИ (уровень 2) =======================

void printInfoMenu() {
    Serial.println("\n=== ИНФОРМАЦИЯ О СИСТЕМЕ ===");
    Serial.println("\nКоманды:");
    Serial.println("  system       - Общая информация о системе");
    Serial.println("  version      - Версия прошивки");
    Serial.println("  temp         - Температура DS3231");
    Serial.println("  memory       - Использование памяти");
    Serial.println("  uptime       - Время работы системы");
    Serial.println("\nУправление:");
    Serial.println("  back, b      - Назад в главное меню");
    Serial.println("  help, ?      - Показать это сообщение");
    Serial.println("  out          - Выход из режима настройки");
    Serial.println("==========================");
}

void handleInfoMenu(String command) {
    if (command.equals("back") || command.equals("b")) {
        currentMenuState = MENU_MAIN;
        printMainMenu();
    }
    else if (command.equals("out") || command.equals("exit")) {
        exitMenuMode();
    }
    else if (command.equals("help") || command.equals("?")) {
        printInfoMenu();
    }
    else if (command.equals("system")) {
        printSystemInfo();
    }
    else if (command.equals("version")) {
        Serial.printf("\nВерсия прошивки: %s\n", FIRMWARE_VERSION);
    }
    else if (command.equals("temp")) {
        printDS3231Temperature();
    }
    else {
        Serial.println("Неизвестная команда. Введите 'help' для справки");
    }
}

// ======================= МЕНЮ КОНФИГУРАЦИИ (уровень 2) =======================

void printConfigMenu() {
    Serial.println("\n=== УПРАВЛЕНИЕ КОНФИГУРАЦИЕЙ ===");
    Serial.println("\nКоманды:");
    Serial.println("  config show     - Показать текущую конфигурацию");
    Serial.println("  config save     - Сохранить конфигурацию");
    Serial.println("  config default  - Сбросить к настройкам по умолчанию");
    Serial.println("  config backup   - Создать резервную копию");
    Serial.println("  config restore  - Восстановить из резервной копии");
    Serial.println("\nУправление:");
    Serial.println("  back, b      - Назад в главное меню");
    Serial.println("  help, ?      - Показать это сообщение");
    Serial.println("  out          - Выход из режима настройки");
    Serial.println("==========================");
}

void handleConfigMenu(String command) {
    if (command.equals("back") || command.equals("b")) {
        currentMenuState = MENU_MAIN;
        printMainMenu();
    }
    else if (command.equals("out") || command.equals("exit")) {
        exitMenuMode();
    }
    else if (command.equals("help") || command.equals("?")) {
        printConfigMenu();
    }
    else if (command.equals("config default")) {
        setDefaultConfig();
        saveConfig();
        Serial.println("Конфигурация сброшена к значениям по умолчанию");
    }
    else if (command.equals("config show")) {
        printSettings();
    }
    else {
        Serial.println("Неизвестная команда. Введите 'help' для справки");
    }
}

// ======================= БАЗОВЫЕ КОМАНДЫ (работают всегда) =======================

void printQuickHelp() {
    Serial.println("\n=== БЫСТРЫЕ КОМАНДЫ ===");
    Serial.println("Команды, доступные без входа в меню:");
    Serial.println("  time, t     - Текущее время");
    Serial.println("  sync        - Синхронизировать с NTP");
    Serial.println("  menu, m     - Войти в режим настройки");
    Serial.println("  help, ?     - Эта справка");
    Serial.println("\nВ режиме настройки (команда 'menu'):");
    Serial.println("  1 - Настройки времени и часовых поясов");
    Serial.println("  2 - Настройки WiFi и NTP");
    Serial.println("  3 - Информация о системе");
    Serial.println("  4 - Управление конфигурацией");
    Serial.println("  5 - Выход из режима настройки");
    Serial.println("==========================");
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
void printSystemInfo() {
    Serial.println("\n=== ИНФОРМАЦИЯ О СИСТЕМЕ ===");
    Serial.println("(Функционал ещё не реализован)");
}

void printSettings() {
    Serial.println("\n=== ТЕКУЩАЯ КОНФИГУРАЦИЯ ===");
    Serial.println("(Функционал ещё не реализован)");
}