#pragma once
#include <Arduino.h>
#include "config.h"
#include "time_utils.h"
#include "hardware.h"

class TimeManager {
public:
    TimeManager();

    enum TimeSource {
        SOURCE_NONE,
        SOURCE_EXTERNAL_RTC,
        SOURCE_INTERNAL_RTC,
        SOURCE_NTP,
        SOURCE_DCF77,
        SOURCE_MANUAL
    };

    // Инициализация
    void init();
    
    // Основные методы времени
    time_t getUTCTime();
    time_t getLocalTime();
    time_t getCurrentTime() { return getUTCTime(); } 
    
    // Информация о источнике
    TimeSource getCurrentSource() const { return currentSource; }
    bool isExternalRTCAvailable() const { return externalRTCAvailable; }
    
    // Синхронизация
    bool syncWithNTP();
    void updateAllRTCs(time_t utcTime);
    
    // Информация для отладки
    void printDebugInfo();
    void printTimeInfo();
    
    // Управление часовым поясом
    void setTimezoneFromConfig();
    void setTimezone(int8_t offset, bool dst_enabled, uint8_t preset_index);
    
private:
    TimeManager(const TimeManager&) = delete;
    TimeManager& operator=(const TimeManager&) = delete;
    
    // Новые приватные методы
    void detectTimeSource();
    void printInitialState();
    void attemptInitialSync();
    
    // Вспомогательные методы
    time_t readInternalRTC();
    void writeInternalRTC(time_t utcTime);
    
    bool externalRTCAvailable;
    TimeSource currentSource;
    time_t lastNTPSync;
};

extern TimeManager timeManager;  // Объявляем глобальный объект