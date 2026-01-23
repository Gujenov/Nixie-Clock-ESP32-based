#pragma once
#include <Arduino.h>

bool setAlarm(uint8_t alarmNum, const String &timeStr);
bool setAlarmMelody(uint8_t alarmNum, uint8_t melody);
bool setAlarmOnceMode(uint8_t alarmNum, bool once);
bool setAlarmDaysMask(uint8_t alarmNum, uint8_t daysMask);
void checkAlarms();
bool disableAlarm(uint8_t alarmNum);
bool enableAlarm(uint8_t alarmNum);
void printAlarmStatus();
uint16_t getMinutesToNextAlarm();  // Опционально