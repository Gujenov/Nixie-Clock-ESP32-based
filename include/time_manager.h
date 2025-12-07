// time_manager.h
#pragma once
#include <Arduino.h>
#include "config.h"
#include "time_utils.h"
#include "hardware.h"

class TimeManager {
public:
    enum TimeSource {
        SOURCE_NONE,
        SOURCE_EXTERNAL_RTC,
        SOURCE_INTERNAL_RTC,
        SOURCE_NTP,
        SOURCE_DCF77,
        SOURCE_MANUAL
    };

// Макрос для удобного доступа к синглтону
#define timeManager TimeManager::getInstance()

    // Singleton доступ
    static TimeManager& getInstance() {
        static TimeManager instance;
        return instance;
    }

    // Инициализация (вместо begin)
    void init();
    
    // Основные методы времени (через time_utils)
    time_t getCurrentTime() { return ::getCurrentTime(); }
    time_t getUTC() { return getCurrentTime(); } // Алиас для совместимости
    
    // Локальное время (с учетом часового пояса)
    time_t getLocalTime();
    
    // Информация о источнике
    TimeSource getCurrentSource() const { return currentSource; }
    bool isExternalRTCAvailable() const { return externalRTCAvailable; }
    
    // Синхронизация
    bool syncWithNTP();
    void updateAllRTCs(time_t utcTime);
    
    // Информация для отладки
    void printDebugInfo();
    void printTimeInfo();
    
    // Управление часовым поясом (через существующую систему)
    void setTimezoneFromConfig();
    void setTimezone(int8_t offset, bool dst_enabled, uint8_t preset_index);
    
private:
    TimeManager(); // Приватный конструктор для Singleton
    TimeManager(const TimeManager&) = delete;
    TimeManager& operator=(const TimeManager&) = delete;
    
    bool externalRTCAvailable;
    TimeSource currentSource;
    time_t lastNTPSync;
    
    // Вспомогательные методы
    void detectTimeSource();
    time_t readInternalRTC();
    void writeInternalRTC(time_t utcTime);
};