#pragma once

#include <WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Arduino.h>

extern WiFiUDP ntpUDP;
extern NTPClient *timeClient;

time_t getCurrentTime();
bool syncTime();
void setTimeZone(int8_t offset, bool dst_enabled, uint8_t preset_index);
bool printTime();
bool setManualTime(const String &timeStr);
bool setManualDate(const String &dateStr);

int calculateDayOfWeek(int year, int month, int day);  // 0=Sunday, 1=Monday, etc.
void setDefaultTime();  // Установка 9:00 6.07.1990 Пятница
void updateDayOfWeekInRTC();
void checkAndSyncTime(); // Проверка необходимости синхронизации (12:00 и 00:00)
bool syncWithDCF77();    // Синхронизация через DCF77
void handleManualTimeSetting(); // Обработка ручной установки времени