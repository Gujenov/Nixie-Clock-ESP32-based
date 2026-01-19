#pragma once

#include <Arduino.h>

enum MenuState {
    MENU_MAIN,
    MENU_TIME,
    MENU_ALARMS,
    MENU_WIFI,
    MENU_INFO,
    MENU_CONFIG,
    MENU_ENGINEERING,
    MENU_EXIT
};

extern MenuState currentMenuState;
extern bool inMenuMode;

// Управление режимом меню
void enterMenuMode();
void exitMenuMode();
bool isInMenuMode();

// Функции отображения меню
void printMappingMenuCommands();
void printTimeMenu();
void printAlarmMenu();
void printWifiMenu();
void printInfoMenu();
void printConfigMenu();
void printEngineeringMenu();
void printQuickHelp();

// Обработчики меню
void handleMainMenu(String command);
void handleTimeMenu(String command);
void handleAlarmMenu(String command);
void handleWifiMenu(String command);
void handleInfoMenu(String command);
void handleConfigMenu(String command);
void handleEngineeringMenu(String command);

// Общая обработка часто повторяющихся команд: back, out, help
// Возвращает true если команда обработана и дальнейшая логика не нужна
bool handleCommonMenuCommands(const String &command, void (*printMenu)());

// Вспомогательные функции
void printSettings();
void handleTimezoneCommand(String args);
void enterEngineeringMenu();