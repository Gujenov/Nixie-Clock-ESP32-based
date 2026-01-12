#pragma once

#include <Preferences.h>
#include <Arduino.h>
#include <NTPClient.h>

// Версии и размеры буферов
// MCU.HW_VARIANT.RELEASE_TYPE.BUILD_DATE
#define FIRMWARE_VERSION "1.A0.3.260109"

#define TIME_BUF_SIZE 64
#define TZ_BUF_SIZE 60
#define NTP_SERVER_SIZE 32

// Конфигурация пинов
#define LED_PIN 48

// Настройки таймера
#define TIMER_DIVIDER 80
#define TIMER_INTERVAL 3000000

// Статусы кнопки
#define BUTTON_NONE     0
#define BUTTON_PRESSED  1
#define BUTTON_LONG     2
#define BUTTON_VERY_LONG 3

// Часовые пояса по умолчанию
#define DEFAULT_TIMEZONE_OFFSET 3    // UTC+3 (Москва)
#define DEFAULT_TIMEZONE_NAME "Europe/Moscow"  //

struct AlarmSettings {
    uint8_t hour;
    uint8_t minute;
    bool enabled;
};


struct TimeConfig {
    // === ОСНОВНАЯ НАСТРОЙКА ===
    char timezone_name[32];           // "Europe/Moscow" - ОБЯЗАТЕЛЬНО! Локация часового пояса
    bool automatic_localtime;         // true = ezTime (online), false = локальная таблица (offline)
    
    // === ВЫЧИСЛЯЕМЫЕ ЗНАЧЕНИЯ (автоматически обновляются) ===
    int8_t current_offset;            // Текущее смещение UTC в часах (напр. +3, +4)
    bool current_dst_active;          // Текущий статус DST (true/false)
    
    // === СИНХРОНИЗАЦИЯ ===
    bool auto_sync_enabled;           // Автосинхронизация с NTP разрешена
    uint8_t sync_interval_hours;      // Интервал синхронизации (часы)
    uint32_t last_ntp_sync;           // Время последней NTP синхронизации (UNIX time)
    uint32_t last_dcf77_sync;         // Время последней DCF77 синхронизации
    uint8_t sync_failures;            // Счётчик неудачных синхронизаций
    
    // === ДОПОЛНИТЕЛЬНЫЕ НАСТРОЙКИ ===
    bool manual_time_set;             // Время было установлено вручную
    bool dcf77_enabled;               // Включить/выключить DCF77 приёмник
    
    // === УСТАРЕВШИЕ ПОЛЯ (для совместимости, будут удалены) ===
    int8_t manual_offset;             // Deprecated: использовать timezone_name + preset
    bool dst_enabled;                 // Deprecated: DST определяется автоматически
    bool dst_active;                  // Deprecated: использовать current_dst_active
    bool auto_timezone;               // Deprecated: переименовано в automatic_localtime
    bool auto_dst;                    // Deprecated: DST теперь всегда автоматический
    bool location_detected;           // Deprecated: больше не используется
    char detected_tz[32];             // Deprecated: больше не используется
};

struct Config {
    // Настройки подключения
    char wifi_ssid[32];
    char wifi_pass[32];
    char ntp_server[NTP_SERVER_SIZE];
    
    // Настройки времени
    TimeConfig time_config;
    
    // Системные настройки
    char serial_number[12];
    
    // Будильники
    AlarmSettings alarm1;
    AlarmSettings alarm2;
};

enum HardwareSource { INTERNAL_RTC, EXTERNAL_DS3231 };

extern HardwareSource currentTimeSource;
extern Config config;
extern Preferences preferences;
extern NTPClient *timeClient;

void initConfiguration();
void setDefaultConfig();
void saveConfig();
void initNTPClient();
void updateNTPServer(const char* server);

// Функции для работы с часовыми поясами
bool initTimezone();             // Инициализация Timezone
time_t utcToLocal(time_t utc);   // Конвертация UTC → Local
time_t localToUtc(time_t local); // Конвертация Local → UTC
bool setTimezone(const char* tz_name); // Установка пояса по имени
bool setTimezoneOffset(int8_t offset); // Установка по смещению
void updateDSTStatus();          // Обновление статуса DST