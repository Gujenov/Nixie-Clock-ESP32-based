#pragma once

#include <WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Arduino.h>
#include <RTClib.h>

// Существующие объявления
extern WiFiUDP ntpUDP;
extern NTPClient *timeClient;

// ===== НОВЫЕ ФУНКЦИИ (единая система времени) =====

// 1. Конвертация между форматами
time_t convertDateTimeToTimeT(const DateTime& dt);
DateTime convertTimeTToDateTime(time_t utcTime);

time_t getCurrentUTCTime();

// 2. Единые операции с временем
time_t getCurrentUTCTime();                    // Основная: всегда возвращает UTC
void setTimeToAllSources(time_t utcTime);      // Установка во все источники
void setDefaultTimeToAllSources();             // Сброс к 9:00 6.07.1990 UTC

// 3. Функции для печати времени
bool printTime();                              // Обновленная: UTC + локальное
void printTimeFromTimeT(time_t utcTime);       // Печать из time_t
//void printLocalTimeFromUTC(time_t utcTime);    // Только локальное время

// 4. Утилиты
bool isDateValid(int day, int month, int year); // Проверка корректности даты
const char* getWeekdayName(int wday);          // Имя дня недели (рус)
time_t manualTimeToUnix(struct tm* tm);        // Ручной расчет Unix time

// 5. Отладка
void debugTimeSources();                       // Информация об источниках времени
void checkRTCTime();                           // Проверка времени в RTC

// ===== СУЩЕСТВУЮЩИЕ ФУНКЦИИ (обновленные) =====

// Обновить сигнатуры существующих функций:
bool syncTime();                               // Обновленная (работает с UTC)
bool printTime();                              // Обновленная версия
bool setManualTime(const String &timeStr);     // Обновленная (работает с UTC)
bool setManualDate(const String &dateStr);     // Обновленная (работает с UTC)