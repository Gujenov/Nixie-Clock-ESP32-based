#pragma once

#include <Preferences.h>
#include <Arduino.h>
#include <NTPClient.h>

// Версии и размеры буферов
// MCU.HW_VARIANT.RELEASE_TYPE.BUILD_DATE
#define FIRMWARE_VERSION "1.A0.3.260312"

#define TIME_BUF_SIZE 64
#define TZ_BUF_SIZE 60
#define NTP_SERVER_SIZE 32

// OTA (локальная сеть)
// Пароль обязательно поменять перед production.
#define OTA_PASSWORD "nixie-ota"
#define OTA_WINDOW_MS 300000UL   // 5 минут
#define OTA_CONNECT_TIMEOUT_MS 12000UL

// Конфигурация пинов
#define LED_PIN 48

// Audio I2S (ESP32-S3 -> MAX98357A)
#define AUDIO_I2S_BCLK_PIN 16   // I2S BCLK
#define AUDIO_I2S_LRCLK_PIN 17  // I2S WS/LRCLK
#define AUDIO_I2S_DOUT_PIN 9    // I2S DOUT (data to DAC)

// microSD (SPI mode)
#define SD_SPI_SCK_PIN 12
#define SD_SPI_MOSI_PIN 11
#define SD_SPI_MISO_PIN 13
#define SD_SPI_CS_PIN 10

// Настройки таймера
#define TIMER_DIVIDER 80
#define TIMER_INTERVAL 3000000

// Статусы кнопки
#define BUTTON_NONE     0
#define BUTTON_PRESSED  1
#define BUTTON_LONG     2
#define BUTTON_VERY_LONG 3

// Часовые пояса по умолчанию
#define DEFAULT_TIMEZONE_OFFSET 1    // UTC+1 (Варшава)
#define DEFAULT_TIMEZONE_NAME "Europe/Warsaw"  //

struct AlarmSettings {
    uint8_t hour;
    uint8_t minute;
    bool enabled;
    uint8_t melody;  // Номер мелодии (индекс трека)
    uint8_t days_mask; // Биты дней недели для будильника 2 (Пн=0 .. Вс=6)
    bool once;         // Для будильника 1: одноразовый
};

enum ClockType : uint8_t {
    CLOCK_TYPE_NIXIE = 0,
    CLOCK_TYPE_NIXIE_HAND,
    CLOCK_TYPE_CYCLOTRON,
    CLOCK_TYPE_VERTICAL,
    CLOCK_TYPE_MECH_2,
    CLOCK_TYPE_MECH_PEND
};

enum Nix6OutputMode : uint8_t {
    NIX6_OUTPUT_STD = 0,          // ЧАС-МИН-СЕК-СЛУЖ, прямые биты
    NIX6_OUTPUT_REVERSE_INVERT    // СЕК-МИН-ЧАС-СЛУЖ, инверсия нибблов
};

enum UiControlMode : uint8_t {
    UI_CONTROL_BUTTON_ONLY = 1,
    UI_CONTROL_ENCODER_ONLY = 2,
    UI_CONTROL_ENCODER_BUTTON = 3
};


struct TimeConfig {
    // === ОСНОВНАЯ НАСТРОЙКА ===
    char timezone_name[32];           // "Europe/Moscow" - ОБЯЗАТЕЛЬНО! Локация часового пояса
    bool automatic_localtime;         // true = ezTime (online), false = локальная таблица (offline)
    
    // === ВЫЧИСЛЯЕМЫЕ ЗНАЧЕНИЯ (автоматически обновляются) ===
    int8_t current_offset;            // Текущее смещение UTC в часах (напр. +3, +4)
    bool current_dst_active;          // Текущий статус DST (true/false)
    
    // === РУЧНАЯ НАСТРОЙКА TIMEZONE (для option 100) ===
    int8_t manual_std_offset;         // Стандартное смещение для ручной зоны
    int8_t manual_dst_offset;         // DST смещение для ручной зоны
    uint8_t manual_dst_start_month;   // DST start: месяц (0 = DST отключен)
    uint8_t manual_dst_start_week;    // DST start: неделя
    uint8_t manual_dst_start_dow;     // DST start: день недели
    uint8_t manual_dst_start_hour;    // DST start: час
    uint8_t manual_dst_end_month;     // DST end: месяц
    uint8_t manual_dst_end_week;      // DST end: неделя
    uint8_t manual_dst_end_dow;       // DST end: день недели
    uint8_t manual_dst_end_hour;      // DST end: час
    
    // === СИНХРОНИЗАЦИЯ ===
    bool auto_sync_enabled;           // Автосинхронизация с NTP разрешена
    uint32_t last_ntp_sync;           // Время последней NTP синхронизации (UNIX time)
    uint8_t sync_failures;            // Счётчик неудачных синхронизаций

    // === ОФЛАЙН ПРАВИЛА (POSIX от ezTime) ===
    char tz_posix[64];                // POSIX-строка для локального времени
    char tz_posix_zone[32];           // Зона, для которой сохранены правила
    uint32_t tz_posix_updated;        // Время обновления (UNIX time)
    
    // === ДОПОЛНИТЕЛЬНЫЕ НАСТРОЙКИ ===
    bool manual_time_set;             // Время было установлено вручную
    
};

struct Config {
    // Настройки подключения
    char wifi_ssid[32];
    char wifi_pass[32];
    char wifi_ssid_2[32];  // Вторая WiFi сеть (резервная)
    char wifi_pass_2[32];  // Пароль второй сети
    char ntp_server_1[NTP_SERVER_SIZE];
    char ntp_server_2[NTP_SERVER_SIZE];
    char ntp_server_3[NTP_SERVER_SIZE];
    
    // Настройки времени
    TimeConfig time_config;
    
    // Системные настройки
    char serial_number[12];

    // Тип часов и количество разрядов
    ClockType clock_type;
    uint8_t clock_digits;
    Nix6OutputMode nix6_output_mode;

    // Платформенные модули (включаются в инженерном меню)
    bool audio_module_enabled;      // Аудио / звук / будильник
    bool ir_sensor_enabled;         // Датчик движения (IR)
    UiControlMode ui_control_mode;  // Наличие и тип ручного управления
    
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
void updateNTPServer(uint8_t index, const char* server);

// Функции для работы с часовыми поясами
bool initTimezone();             // Инициализация Timezone
time_t utcToLocal(time_t utc);   // Конвертация UTC → Local
time_t localToUtc(time_t local); // Конвертация Local → UTC
bool setTimezone(const char* tz_name); // Установка пояса по имени
bool setTimezoneOffset(int8_t offset); // Установка по смещению
void updateDSTStatus();          // Обновление статуса DST