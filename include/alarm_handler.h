#pragma once
#include <Arduino.h>

bool setAlarm(uint8_t alarmNum, const String &timeStr);
void checkAlarms();
bool disableAlarm(uint8_t alarmNum);
bool enableAlarm(uint8_t alarmNum);
void printAlarmStatus();
uint8_t getMinutesToNextAlarm();  // Опционально