#include <Arduino.h>
#include "config.h"
#include "hardware.h"
#include "command_handler.h"
#include "input_handler.h"
#include "alarm_handler.h"
#include "time_utils.h"
#include "dcf77_handler.h"


extern bool printEnabled;


void setup() {

    // 1. Инициализация железа (энкодер, кнопки, пины)
    initHardware();
    
    // 2. Загрузка конфигурации
    initConfiguration();
    
    //3.Инициализация NTP клиента
    initNTPClient();
    
    // 4. Проверка источников времени
    checkTimeSource(); 
    
    // 5. Установка часового пояса
    /*setTimeZone(config.time_config.timezone_offset, 
                config.time_config.dst_enabled,
                config.time_config.dst_preset_index);
    */
    // 6. Попытка NTP синхронизации (если разрешено)
    if(config.time_config.auto_sync_enabled && strlen(config.wifi_ssid) > 0) {
        syncTime();  // Существующая функция из time_utils.cpp
    }
    
    // 7. Инициализация DCF77
    if(config.time_config.dcf77_enabled) {
    initDCF77();
    updateDCF77(); // Начинаем приём DCF77
    }
  
    Serial.println("\n=== Система готова ===");
    
}

void loop() {
    static unsigned long lastInputCheck = 0;
    static unsigned long lastSyncCheck = 0;
    static unsigned long lastSecondTick = 0;
    unsigned long currentMillis = millis();
    static time_t lastTime = 0;
    
    // === 1. SERIAL КОМАНДЫ ===
    if (Serial.available()) {
        handleSerialCommands();
    }
    
    // === 2. ВВОД С КНОПКИ И ЭНКОДЕРА ===
    if (currentMillis - lastInputCheck >= 20) {
        lastInputCheck = currentMillis;
        processAllInputs();
    }
    
    // === 3. СБРОС ФЛАГА ПРЕРЫВАНИЯ ===
    if (timeUpdated) {
        portENTER_CRITICAL(&timerMux);
        timeUpdated = false;
        portEXIT_CRITICAL(&timerMux);
    }
    
    // === 4. СЕКУНДНЫЕ ОПЕРАЦИИ (ЕДИНЫЕ ДЛЯ ВСЕХ ИСТОЧНИКОВ) ===
    if (currentMillis - lastSecondTick >= 1000) {
        lastSecondTick = currentMillis;
        
        time_t currentTime = getCurrentUTCTime();
        
        if (currentTime != lastTime) {
            lastTime = currentTime;
            
            struct tm* tm_info = gmtime(&currentTime);
            uint8_t currentHour = tm_info->tm_hour;
            uint8_t currentMinute = tm_info->tm_min;
            uint8_t currentSecond = tm_info->tm_sec;
            
            // 1. Будильники
            checkAlarms();
            
            // 2. Вывод времени
            if (printEnabled && currentSecond % 20 == 0) {
                printTime();
            }
                            
                if((currentHour == 0 || currentHour == 12) && 
                                  currentMinute == 0 && 
                                  currentSecond == 0){
                                    syncTime();}
                
                    
                    
            
        }
    }
    
    // === 6. ОБРАБОТКА DCF77 ===

    delay(10);
}