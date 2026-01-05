#pragma once

#include <Arduino.h>

enum MenuState {
    MENU_MAIN,
    MENU_TIME,
    MENU_WIFI,
    MENU_INFO,
    MENU_CONFIG,
    MENU_EXIT
};

extern MenuState currentMenuState;
extern bool inMenuMode;

// Управление режимом меню
void enterMenuMode();
void exitMenuMode();
bool isInMenuMode();

// Функции отображения меню
void printMainMenu();
void printTimeMenu();
void printWifiMenu();
void printInfoMenu();
void printConfigMenu();
void printQuickHelp();

// Обработчики меню
void handleMainMenu(String command);
void handleTimeMenu(String command);
void handleWifiMenu(String command);
void handleInfoMenu(String command);
void handleConfigMenu(String command);

// Вспомогательные
void printSystemInfo();
void printSettings();
//void handleTimezoneCommand(String args);