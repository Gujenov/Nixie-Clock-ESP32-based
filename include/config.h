#pragma once

#include <Preferences.h>
#include <Arduino.h>

// Версии и размеры буферов
#define FIRMWARE_VERSION "1.0"
#define TIME_BUF_SIZE 64
#define TZ_BUF_SIZE 60
#define NTP_SERVER_SIZE 32

// Конфигурация пинов (только общие, специфичные - в hardware.h)
#define LED_PIN 48

// Настройки таймера
#define TIMER_DIVIDER 80
#define TIMER_INTERVAL 3000000

// Статусы кнопки
#define BUTTON_NONE     0
#define BUTTON_PRESSED  1
#define BUTTON_LONG     2
#define BUTTON_VERY_LONG 3

struct AlarmSettings {
    uint8_t hour;
    uint8_t minute;
    bool enabled;
};

struct Config {
    // Настройки подключения
    char wifi_ssid[32];
    char wifi_pass[32];
    char ntp_server[NTP_SERVER_SIZE];
    
    // Настройки времени
    int8_t timezone_offset;
    bool dst_enabled;
    uint8_t dst_preset_index;
    char dst_rule[64];
    
    // Системные
    char serial_number[12];
    
    // Будильники
    AlarmSettings alarm1;
    AlarmSettings alarm2;
};

enum TimeSource { INTERNAL_RTC, EXTERNAL_DS3231 };

extern TimeSource currentTimeSource;
extern Config config;
extern Preferences preferences;

void initConfiguration();
void setDefaultConfig();
void saveConfig();