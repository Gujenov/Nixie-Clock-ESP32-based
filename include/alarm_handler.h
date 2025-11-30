#pragma once
#include <Arduino.h>

bool setAlarm(uint8_t alarmNum, const String &timeStr);
void checkAlarms();