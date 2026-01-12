#pragma once

#include <Arduino.h>
#include <time.h>

// ========== СТРУКТУРА ПРЕСЕТА ЧАСОВОГО ПОЯСА ==========
struct TimezonePreset {
    const char* zone_name;       // "Europe/Moscow"
    const char* display_name;    // "Москва (MSK)"
    int8_t std_offset;           // Стандартное смещение в часах (UTC+3 = 3)
    int8_t dst_offset;           // DST смещение в часах (UTC+4 = 4), 0 если нет DST
    
    // Правила DST (если используются)
    uint8_t dst_start_month;     // Месяц начала DST (1-12), 0 если нет DST
    uint8_t dst_start_week;      // Неделя месяца (1-5, где 5 = последняя)
    uint8_t dst_start_dow;       // День недели (0=Sun, 1=Mon, ..., 6=Sat)
    uint8_t dst_start_hour;      // Час перехода (местное время)
    
    uint8_t dst_end_month;       // Месяц окончания DST
    uint8_t dst_end_week;        // Неделя месяца
    uint8_t dst_end_dow;         // День недели
    uint8_t dst_end_hour;        // Час перехода (местное время)
};

// ========== ФУНКЦИИ РАБОТЫ С ЧАСОВЫМИ ПОЯСАМИ ==========

// Инициализация системы часовых поясов
bool initTimezone();

// Конвертация времени
time_t utcToLocal(time_t utc);          // UTC → локальное время
time_t localToUtc(time_t local);        // Локальное → UTC

// Установка часового пояса
bool setTimezone(const char* tz_name);  // Установить по имени локации
bool setTimezoneOffset(int8_t offset);  // Установить ручное смещение (deprecated)

// Поиск пресетов
const TimezonePreset* findPresetByLocation(const char* location);
const TimezonePreset* getPresetByIndex(uint8_t index);
uint8_t getPresetsCount();

// Вычисление DST
bool calculateDSTStatus(time_t utc, const TimezonePreset* preset);
time_t calculateDSTTransition(int year, uint8_t month, uint8_t week, uint8_t dow, uint8_t hour, int8_t offset);

// Отладка
void printTimezoneInfo();
void listAvailableTimezones();
