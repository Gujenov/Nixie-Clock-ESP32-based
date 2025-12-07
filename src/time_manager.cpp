// time_manager.cpp
#include "time_manager.h"
#include "dst_handler.h"
#include <sys/time.h>

// Объявляем внешние переменные из других модулей
extern Config config;
extern bool ds3231_available;
extern WiFiUDP ntpUDP;
extern NTPClient *timeClient;

// Singleton конструктор
TimeManager::TimeManager() : 
    externalRTCAvailable(false),
    currentSource(SOURCE_NONE),
    lastNTPSync(0) {
}

void TimeManager::init() {
    Serial.println("\n=== Initializing TimeManager ===");
    
    // 1. Определяем доступный источник времени
    detectTimeSource();
    
    // 2. Устанавливаем часовой пояс из конфигурации
    setTimezoneFromConfig();
    
    // 3. Выводим информацию
    printDebugInfo();
    
    Serial.println("=== TimeManager Ready ===\n");
}

void TimeManager::detectTimeSource() {
    // Используем существующую проверку из hardware
    externalRTCAvailable = ds3231_available;
    
    if (externalRTCAvailable) {
        currentSource = SOURCE_EXTERNAL_RTC;
        Serial.println("Time source: External RTC (DS3231)");
    } else {
        currentSource = SOURCE_INTERNAL_RTC;
        Serial.println("Time source: Internal RTC (ESP32)");
        
        // Устанавливаем внутренние часы если они пустые
        struct timeval tv;
        gettimeofday(&tv, NULL);
        if (tv.tv_sec < 1609459200) { // Если время до 2021 года
            Serial.println("Internal RTC has invalid time, setting default");
            writeInternalRTC(0); // Установим позже при синхронизации
        }
    }
}

void TimeManager::setTimezoneFromConfig() {
    // Используем существующую функцию из time_utils
    ::setTimeZone(
        config.time_config.timezone_offset,
        config.time_config.dst_enabled,
        config.time_config.dst_preset_index
    );
    
    Serial.print("Timezone configured: UTC");
    Serial.print(config.time_config.timezone_offset);
    Serial.print(", DST ");
    Serial.println(config.time_config.dst_enabled ? "enabled" : "disabled");
}

void TimeManager::setTimezone(int8_t offset, bool dst_enabled, uint8_t preset_index) {
    // Обновляем конфигурацию
    config.time_config.timezone_offset = offset;
    config.time_config.dst_enabled = dst_enabled;
    config.time_config.dst_preset_index = preset_index;
    
    // Применяем
    setTimezoneFromConfig();
    saveConfig();
}

time_t TimeManager::getLocalTime() {
    time_t utc = getCurrentTime();
    
    // Если часовой пояс UTC или не настроен DST, возвращаем как есть
    if (config.time_config.timezone_offset == 0 && !config.time_config.dst_enabled) {
        return utc;
    }
    
    // Конвертируем с учетом часового пояса
    // Используем системную функцию localtime
    struct tm* timeinfo = localtime(&utc);
    return mktime(timeinfo);
}

bool TimeManager::syncWithNTP() {
    Serial.println("\n=== NTP Synchronization ===");
    
    // Используем существующую функцию syncTime() из time_utils
    if (::syncTime()) {
        lastNTPSync = ::getCurrentTime();
        currentSource = SOURCE_NTP;
        
        // Обновляем RTC если доступны
        updateAllRTCs(lastNTPSync);
        
        Serial.println("NTP sync successful");
        return true;
    }
    
    Serial.println("NTP sync failed");
    return false;
}

void TimeManager::updateAllRTCs(time_t utcTime) {
    // Обновляем внешние RTC если доступны
    if (externalRTCAvailable) {
        // Используем существующую функцию или напрямую RTC
        extern RTC_DS3231 *rtc;
        if (rtc) {
            DateTime dt(utcTime);
            rtc->adjust(dt);
            Serial.println("External RTC updated from NTP");
        }
    }
    
    // Обновляем внутренние RTC
    writeInternalRTC(utcTime);
    Serial.println("Internal RTC updated from NTP");
}

time_t TimeManager::readInternalRTC() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec;
}

void TimeManager::writeInternalRTC(time_t utcTime) {
    struct timeval tv;
    tv.tv_sec = utcTime;
    tv.tv_usec = 0;
    settimeofday(&tv, NULL);
}

void TimeManager::printDebugInfo() {
    Serial.println("=== TimeManager Debug Info ===");
    Serial.print("External RTC: ");
    Serial.println(externalRTCAvailable ? "Available" : "Not available");
    
    Serial.print("Current source: ");
    switch(currentSource) {
        case SOURCE_EXTERNAL_RTC: Serial.println("External RTC"); break;
        case SOURCE_INTERNAL_RTC: Serial.println("Internal RTC"); break;
        case SOURCE_NTP: Serial.println("NTP"); break;
        default: Serial.println("Unknown"); break;
    }
    
    if (lastNTPSync > 0) {
        Serial.print("Last NTP sync: ");
        Serial.println(ctime(&lastNTPSync));
    }
    
    Serial.print("Timezone offset: UTC");
    Serial.println(config.time_config.timezone_offset);
    Serial.print("DST: ");
    Serial.println(config.time_config.dst_enabled ? "Enabled" : "Disabled");
    
    time_t utc = getUTC();
    time_t local = getLocalTime();
    
    Serial.print("UTC time: ");
    Serial.println(ctime(&utc));
    Serial.print("Local time: ");
    Serial.println(ctime(&local));
    
    Serial.println("==============================");
}

void TimeManager::printTimeInfo() {
    time_t utc = getUTC();
    time_t local = getLocalTime();
    
    Serial.println("\n=== Current Time ===");
    Serial.print("UTC:   ");
    Serial.println(ctime(&utc));
    Serial.print("Local: ");
    Serial.println(ctime(&local));
    
    Serial.print("Source: ");
    switch(currentSource) {
        case SOURCE_EXTERNAL_RTC: Serial.println("DS3231 RTC"); break;
        case SOURCE_INTERNAL_RTC: Serial.println("ESP32 Internal RTC"); break;
        case SOURCE_NTP: Serial.println("NTP Server"); break;
        default: Serial.println("Unknown"); break;
    }
    
    Serial.print("Timezone: UTC");
    Serial.print(config.time_config.timezone_offset);
    if (config.time_config.dst_enabled) {
        Serial.print(" (DST enabled)");
    }
    Serial.println();
}