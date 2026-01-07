#include "command_handler.h"
#include "config.h"
#include "time_utils.h"
#include "alarm_handler.h"
#include "hardware.h"
#include "menu_manager.h"

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
        Serial.println("Неизвестная команда. Введите 'help' для справки");
        Serial.println("Или введите 'menu' для входа в режим настройки");
    }
}