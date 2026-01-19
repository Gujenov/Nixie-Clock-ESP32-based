#include "command_handler.h"
#include "config.h"
#include "time_utils.h"
#include "alarm_handler.h"
#include "hardware.h"
#include "menu_manager.h"
#include "engineering_menu.h"

// Объявляем внешние переменные
extern WiFiUDP ntpUDP;
extern NTPClient *timeClient;
extern HardwareSource currentTimeSource;
extern bool ds3231_available;

void handleCommand(String command) {
    command.trim();
    
    // Если в режиме меню - передаём в менеджер меню
    if (inMenuMode) {
        switch (currentMenuState) {
            case MENU_MAIN: handleMainMenu(command); break;
            case MENU_TIME: handleTimeMenu(command); break;
            case MENU_ALARMS: handleAlarmMenu(command); break;
            case MENU_WIFI: handleWifiMenu(command); break;
            case MENU_INFO: handleInfoMenu(command); break;
            case MENU_CONFIG: handleConfigMenu(command); break;
            case MENU_ENGINEERING: handleEngineeringMenu(command); break;
            default: break;
        }
        return;
    }
    
    // Базовые команды (работают без меню)
    if (command.equals("help") || command.equals("?")) {
        printQuickHelp();
    }
    else if (command.equals("time") || command.equals("t")) {
        printTime();
    }
    else if (command.equals("menu") || command.equals("m")) {
        enterMenuMode();
    }
    else if (command.equals("d")) {
        enterEngineeringMenu();
    }
    else if (command.equals("sync")) {
        syncTime(true);
    }
    // Команды установки UTC времени и даты
    else if (command.startsWith("set UTC T ") || command.startsWith("SUT ")) {
        String timeStr = command.startsWith("set UTC T ") ? command.substring(10) : command.substring(4);
        setManualTime(timeStr);
    }
    else if (command.startsWith("set UTC D ") || command.startsWith("SUD ")) {
        String dateStr = command.startsWith("set UTC D ") ? command.substring(10) : command.substring(4);
        setManualDate(dateStr);
    }
    // Команды установки локального времени и даты
    else if (command.startsWith("set local T ") || command.startsWith("SLT ")) {
        String timeStr = command.startsWith("set local T ") ? command.substring(12) : command.substring(4);
        setManualLocalTime(timeStr);
    }
    else if (command.startsWith("set local D ") || command.startsWith("SLD ")) {
        String dateStr = command.startsWith("set local D ") ? command.substring(12) : command.substring(4);
        setManualLocalDate(dateStr);
    }
    // Команды автосинхронизации
    else if (command.equalsIgnoreCase("auto sync en") || command.equalsIgnoreCase("ASE")) {
        config.time_config.auto_sync_enabled = true;
        saveConfig();
        Serial.println("\nАвтоматическая синхронизация времени ВКЛЮЧЕНА");
        Serial.printf("Интервал синхронизации: %d часов\n", config.time_config.sync_interval_hours);
    }
    else if (command.equalsIgnoreCase("auto sync dis") || command.equalsIgnoreCase("ASD")) {
        config.time_config.auto_sync_enabled = false;
        saveConfig();
        Serial.println("\nАвтоматическая синхронизация времени ОТКЛЮЧЕНА");
        Serial.println("Время можно установить вручную или синхронизировать командой 'sync'");
    }
    else {
        Serial.println("Неизвестная команда. Введите 'help' для справки");
        Serial.println("Или введите 'menu' для входа в режим настройки");
    }
}