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
    // Флаги управления
    bool manual_time_set;        // Время установлено вручную
    bool auto_timezone;          // Автоопределение пояса (true = разрешить менять)
    bool auto_sync_enabled;      // Автосинхронизация разрешена
    bool auto_dst;               // Автоопределение DST (true = разрешить менять)
    bool dcf77_enabled;          // Включить/выключить DCF77 приемник
    
    // Настройки
    char timezone_name[32];      // "Europe/Moscow", "Asia/Vladivostok"
    int8_t manual_offset;        // Ручное смещение (часы)
    bool dst_enabled;            // Включить DST (если auto_dst = false)
    
    // Флаги состояния
    bool location_detected;      // Пояс определён автоматически
    char detected_tz[32];        // Определённый пояс (для отладки)
    bool dst_active;             // Текущий статус DST (только для чтения)

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

// Функции для работы с часовыми поясами
bool initTimezone();             // Инициализация Timezone
time_t utcToLocal(time_t utc);   // Конвертация UTC → Local
time_t localToUtc(time_t local); // Конвертация Local → UTC
bool setTimezone(const char* tz_name); // Установка пояса по имени
bool setTimezoneOffset(int8_t offset); // Установка по смещению
void updateDSTStatus();          // Обновление статуса DST