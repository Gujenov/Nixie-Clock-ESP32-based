#pragma once

#include <Preferences.h>
#include <Arduino.h>
#include <NTPClient.h>

// Версии и размеры буферов
// MCU.HW_VARIANT.RELEASE_TYPE.BUILD_DATE
#define FIRMWARE_VERSION "1.A0.3.260104"

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
#define DEFAULT_DST_ENABLED true     // Использовать летнее время

struct AlarmSettings {
    uint8_t hour;
    uint8_t minute;
    bool enabled;
};

struct TimeConfig {
    // Флаги управления
    bool manual_time_set;        // Время установлено вручную
    bool auto_timezone;          // Автоопределение пояса
    bool auto_sync_enabled;      // Автосинхронизация разрешена
    bool dcf77_enabled;          // Использовать DCF77
    
    // Часовой пояс
    int8_t timezone_offset;      // Смещение от UTC в часах
    bool dst_enabled;            // Использовать летнее время
        
    // Синхронизация
    uint8_t sync_interval_hours; // Интервал синхронизации (часы)
    uint32_t last_ntp_sync;      // Время последней NTP синхронизации (UNIX time)
    uint32_t last_dcf77_sync;    // Время последней DCF77 синхронизации
    uint8_t sync_failures;       // Счётчик неудачных синхронизаций
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