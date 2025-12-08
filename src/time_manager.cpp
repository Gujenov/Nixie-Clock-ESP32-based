#include "time_manager.h"
#include "dst_handler.h"
#include <sys/time.h>

extern Config config;
extern bool ds3231_available;
TimeManager timeManager;  // Определяем глобальный объект

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
    
    // 3. Выводим информацию о начальном состоянии
    printInitialState();
    
    // 4. Пропуск строки и сообщение о синхронизации
    Serial.println();
    
    // 5. Попытка синхронизации (если включена)
    if (config.time_config.auto_sync_enabled) {
        attemptInitialSync();
    } else {
        Serial.println("Auto-sync disabled on startup");
    }
    
    Serial.println("=== TimeManager Ready ===");
}

void TimeManager::detectTimeSource() {
    // Используем существующую проверку
    externalRTCAvailable = ds3231_available;
    
    if (externalRTCAvailable) {
        currentSource = SOURCE_EXTERNAL_RTC;
    } else {
        currentSource = SOURCE_INTERNAL_RTC;
    }
}

void TimeManager::setTimezoneFromConfig() {
    // Применяем часовой пояс
    ::setTimeZone(
        config.time_config.timezone_offset,
        config.time_config.dst_enabled,
        config.time_config.dst_preset_index
    );
}

void TimeManager::printInitialState() {
    Serial.println("--- Initial State ---");
    
    // 1. Источник времени
    Serial.print("Time source: ");
    switch(currentSource) {
        case SOURCE_EXTERNAL_RTC:
            Serial.println("External RTC (DS3231)");
            break;
        case SOURCE_INTERNAL_RTC:
            Serial.println("Internal RTC (ESP32)");
            break;
        default:
            Serial.println("Unknown");
    }
    
    // 2. Часовой пояс (красиво)
    Serial.print("Timezone: UTC");
    if (config.time_config.timezone_offset >= 0) {
        Serial.print("+");
    }
    Serial.print(config.time_config.timezone_offset);
    
    // 3. DST статус
    if (config.time_config.dst_enabled) {
        Serial.print(" (DST: preset ");
        Serial.print(config.time_config.dst_preset_index);
        Serial.print(")");
    } else {
        Serial.print(" (DST: disabled)");
    }
    Serial.println();
    
    // 4. Начальное время из текущего источника
    time_t initialTime = getUTCTime();
    if (initialTime > 0) {
        struct tm* tm_utc = gmtime(&initialTime);
        char buffer[32];
        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", tm_utc);
        Serial.print("Initial UTC: ");
        Serial.println(buffer);
    } else {
        Serial.println("Initial UTC: Not available");
    }
}

void TimeManager::attemptInitialSync() {
    Serial.println("--- Synchronization ---");
    
    // Проверяем WiFi
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi: Not connected, sync skipped");
        return;
    }
    
    Serial.println("WiFi: Connected, attempting NTP sync...");
    
    // Пробуем синхронизироваться
    if (syncWithNTP()) {
        Serial.println("Result: Synchronization successful");
    } else {
        Serial.println("Result: Synchronization failed");
    }
}

time_t TimeManager::getUTCTime() {
    // Используем существующую функцию из time_utils
    return ::getCurrentTime();
}

time_t TimeManager::getLocalTime() {
    time_t utc = getUTCTime();
    
    // Если часовой пояс UTC или не настроен DST, возвращаем как есть
    if (config.time_config.timezone_offset == 0 && !config.time_config.dst_enabled) {
        return utc;
    }
    
    // Конвертируем с учетом часового пояса
    struct tm* timeinfo = localtime(&utc);
    return mktime(timeinfo);
}

bool TimeManager::syncWithNTP() {
    // Используем существующую функцию
    bool result = ::syncTime();
    
    if (result) {
        lastNTPSync = ::getCurrentTime();
        currentSource = SOURCE_NTP;
        
        // Обновляем RTC
        updateAllRTCs(lastNTPSync);
        
        return true;
    }
    
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
    Serial.println("\n=== TimeManager Debug ===");
    
    Serial.print("External RTC available: ");
    Serial.println(externalRTCAvailable ? "Yes" : "No");
    
    Serial.print("Current source: ");
    switch(currentSource) {
        case SOURCE_EXTERNAL_RTC: Serial.println("External RTC"); break;
        case SOURCE_INTERNAL_RTC: Serial.println("Internal RTC"); break;
        case SOURCE_NTP: Serial.println("NTP"); break;
        default: Serial.println("Unknown"); break;
    }
    
    if (lastNTPSync > 0) {
        struct tm* timeinfo = gmtime(&lastNTPSync);
        char buffer[32];
        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
        Serial.print("Last NTP sync: ");
        Serial.println(buffer);
    }
    
    Serial.print("Config timezone: UTC");
    if (config.time_config.timezone_offset >= 0) Serial.print("+");
    Serial.println(config.time_config.timezone_offset);
    
    Serial.print("Config DST: ");
    Serial.println(config.time_config.dst_enabled ? "Enabled" : "Disabled");
    
    time_t utc = getUTCTime();
    time_t local = getLocalTime();
    
    struct tm* tm_utc = gmtime(&utc);
    struct tm* tm_local = localtime(&local);
    
    char buf1[32], buf2[32];
    strftime(buf1, sizeof(buf1), "%Y-%m-%d %H:%M:%S", tm_utc);
    strftime(buf2, sizeof(buf2), "%Y-%m-%d %H:%M:%S %Z", tm_local);
    
    Serial.print("Current UTC: ");
    Serial.println(buf1);
    Serial.print("Current local: ");
    Serial.println(buf2);
    
    Serial.println("========================");
}

void TimeManager::printTimeInfo() {
    time_t utc = getUTCTime();
    time_t local = getLocalTime();
    
    char buffer[64];
    struct tm timeinfo;
    
    Serial.println("\n=== Current Time ===");
    
    // UTC время
    gmtime_r(&utc, &timeinfo);
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
    Serial.print("UTC:    ");
    Serial.print(buffer);
    Serial.print(" (");
    Serial.print(utc);
    Serial.println(")");
    
    // Локальное время
    localtime_r(&local, &timeinfo);
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S %Z", &timeinfo);
    Serial.print("Local:  ");
    Serial.println(buffer);
    
    // Пустая строка для разделения
    Serial.println();
    
    // Информация об источнике
    Serial.print("Source: ");
    switch(currentSource) {
        case SOURCE_EXTERNAL_RTC: 
            Serial.print("DS3231 RTC");
            if (externalRTCAvailable) {
                Serial.println(" (available)");
            } else {
                Serial.println(" (not available)");
            }
            break;
        case SOURCE_INTERNAL_RTC: 
            Serial.println("ESP32 Internal RTC");
            break;
        case SOURCE_NTP: 
            Serial.println("NTP Server");
            break;
        default: 
            Serial.println("Unknown");
            break;
    }
    
    // Часовой пояс
    Serial.print("Timezone: UTC");
    if (config.time_config.timezone_offset >= 0) {
        Serial.print("+");
    }
    Serial.print(config.time_config.timezone_offset);
    
    if (config.time_config.dst_enabled) {
        Serial.print(" (DST: ");
        Serial.print(config.time_config.dst_preset_index);
        Serial.print(")");
    }
    Serial.println();
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