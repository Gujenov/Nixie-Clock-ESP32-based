#pragma once

#include <Arduino.h>

// Только обработка базовых команд, меню отдельно
void handleCommand(String command);
void printHelp();
void printSystemInfo();
void printSettings();
void printESP32Info();