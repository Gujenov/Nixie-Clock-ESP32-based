#include <Arduino.h>
#include "config.h"
#include "hardware.h"
#include "command_handler.h"
#include "input_handler.h"
#include "alarm_handler.h"
#include "time_utils.h"

// Для периодической синхронизации
unsigned long lastWiFiSyncCheck = 0;
#define WIFI_SYNC_INTERVAL (12 * 3600 * 1000) // 12 часов

extern bool printEnabled;

void setup() {

    // 1. Инициализация железа (энкодер, кнопки, пины)
    initHardware();
    
    // 2. Загрузка конфигурации
    initConfiguration();
    
    //3.Инициализация NTP клиента
    initNTPClient();
    
    // 4. Проверка источников времени
    initTimeSource(); 
    
    // 5. Установка часового пояса
    /*setTimeZone(config.time_config.timezone_offset, 
                config.time_config.dst_enabled,
                config.time_config.dst_preset_index);
    */
    // 6. Попытка NTP синхронизации (если разрешено)
    if(config.time_config.auto_sync_enabled && strlen(config.wifi_ssid) > 0) {
        syncTime();  // Существующая функция из time_utils.cpp
    }
    
    // 7. Инициализация DCF77 (если включено)
    #ifdef ENABLE_DCF77
    if(config.time_config.dcf77_enabled) {
        initDCF77();
        // Запуск таймера ожидания
    }
    #endif
    
    Serial.println("\n=== Система готова ===");
    // 8. Настройка прерываний
    setupInterrupts();
}

void loop() {
    static unsigned long lastInputCheck = 0;
    static unsigned long lastSyncCheck = 0;
    static unsigned long lastSecondTick = 0;
    unsigned long currentMillis = millis();
    
    // === 1. SERIAL КОМАНДЫ (приоритет) ===
    if (Serial.available()) {
        handleSerialCommands();
    }
    
    // === 2. ВВОД С КНОПКИ И ЭНКОДЕРА (каждые 20ms) ===
    if (currentMillis - lastInputCheck >= 20) {
        lastInputCheck = currentMillis;
        processAllInputs();  // Всё в одной функции!
    }
    
    // === 3. ОБНОВЛЕНИЕ ВРЕМЕНИ ===
    if (timeUpdated) {
        portENTER_CRITICAL(&timerMux);
        timeUpdated = false;
        portEXIT_CRITICAL(&timerMux);
        
        static time_t lastTime = 0;
        
        if (now != lastTime) {
            lastTime = now;
            
            // Вывод каждые 20 секунд
            struct tm* tm_info = gmtime(&now);
            if (tm_info->tm_sec % 20 == 0 && printEnabled) {
                printTime();
            }
        }
    }
    // Или секундный тик для внутренних часов
    else if (currentTimeSource == INTERNAL_RTC) {
        if (currentMillis - lastSecondTick >= 1000) {
            lastSecondTick = currentMillis;
            
            time_t now = getCurrentUTCTime();
            static time_t lastTime = 0;
            
            if (now != lastTime) {
                lastTime = now;
                
                struct tm* tm_info = gmtime(&now);
                if (tm_info->tm_sec % 20 == 0 && printEnabled) {
                    printTime();
                }
                
                checkAlarms();  // Проверка будильников раз в секунду
            }
        }
    }
    
    // === 4. ПЕРИОДИЧЕСКАЯ СИНХРОНИЗАЦИЯ ===
    if (currentMillis - lastSyncCheck >= 60000) { // Каждую минуту
        lastSyncCheck = currentMillis;
        
        if (config.time_config.auto_sync_enabled && 
            strlen(config.wifi_ssid) > 0 &&
            currentMillis - lastWiFiSyncCheck >= WIFI_SYNC_INTERVAL) {
            
            lastWiFiSyncCheck = currentMillis;
            Serial.println("[SYNC] Periodic NTP sync");
            syncTime();
        }
    }
    
    delay(1);
}