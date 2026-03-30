#pragma once
#include <Arduino.h>
#include <time.h>

bool setAlarm(uint8_t alarmNum, const String &timeStr);
bool setAlarmMelody(uint8_t alarmNum, uint8_t melody);
bool setAlarmOnceMode(uint8_t alarmNum, bool once);
bool setAlarmDaysMask(uint8_t alarmNum, uint8_t daysMask);
void checkAlarms();
void checkAlarmsAtTick(time_t now_utc, const tm& local_timeinfo);
bool disableAlarm(uint8_t alarmNum);
bool enableAlarm(uint8_t alarmNum);
void printAlarmStatus();
uint16_t getMinutesToNextAlarm();  // Опционально