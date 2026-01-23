#include <Arduino.h>
#include "config.h"
#include "hardware.h"
#include "command_handler.h"
#include "menu_manager.h"  // Теперь меню отдельно
#include "time_utils.h"
#include "alarm_handler.h"
#include "dfplayer_manager.h"

static bool sqwFailed = false;
extern bool printEnabled;

void processSecondTick();

void setup() {
    initHardware();
    initConfiguration();
    initNTPClient();
    checkTimeSource(); 
    printDS3231Temperature();

    initDFPlayer();
    
    syncTime();
    
    Serial.print("\n\n=== Система готова ===");
    Serial.println("\n\nhelp / ? - Перечень доступных команд");
    
    // Инициализация меню (флаги уже инициализированы в menu_manager.cpp)
    printEnabled = true;
}

void loop() {
    static unsigned long lastSecondCheck = 0;
    static unsigned long lastSQWCheck = 0;
    unsigned long currentMillis = millis();
    
    // Обработка команд
    if (Serial.available()) {
        String command = Serial.readStringUntil('\n');
        command.trim();
        handleCommand(command);
    }
 
    // === ОБРАБОТКА СЕКУНДНЫХ СОБЫТИЙ ===
    // (ТОЛЬКО если не в режиме меню)
    if (!inMenuMode) {
        if (ds3231_available) {
            if (timeUpdatedFromSQW) {
                portENTER_CRITICAL(&timerMux);
                timeUpdatedFromSQW = false;
                portEXIT_CRITICAL(&timerMux);
                lastSQWCheck = currentMillis;
                sqwFailed = false;
                processSecondTick();
            }
            else if (!sqwFailed && (currentMillis - lastSQWCheck >= 3000)) {
                sqwFailed = true;
                Serial.print("\n[WARN] SQW не поступает 3 сек, переход на millis!");
                lastSecondCheck = currentMillis;
                processSecondTick();
            }
            else if (sqwFailed && (currentMillis - lastSecondCheck >= 1000)) {
                lastSecondCheck = currentMillis;
                processSecondTick();
            }
        }
        else {
            if (currentMillis - lastSecondCheck >= 1000) {
                lastSQWCheck = currentMillis;
                lastSecondCheck = currentMillis;
                processSecondTick();
            }
        }
    }
    
    delay(10);
}

void processSecondTick() {
    time_t currentTime = getCurrentUTCTime();
    time_t localTime = utcToLocal(currentTime);
    
    struct tm* tm_info = gmtime(&currentTime);
    struct tm local_tm_info;
    gmtime_r(&localTime, &local_tm_info);
    uint8_t currentSecond = tm_info->tm_sec;
    
    // Индикация работы
    if (printEnabled) {
        if (ds3231_available && !sqwFailed) {
            Serial.print(".");
        } else {
            Serial.print("*");
        }
        
        if (currentSecond % 20 == 0) {
            if(ds3231_available && sqwFailed) {
                Serial.print("\n[WARN] SQW не доступен");    
            }
            printTime();
        }
    }
    
    checkAlarms();
    
    // Синхронизация
    static uint8_t lastSyncHour = 255;
    if ((local_tm_info.tm_hour == 3 || local_tm_info.tm_hour == 15) && local_tm_info.tm_min == 5) {
        if (local_tm_info.tm_hour != lastSyncHour) {
            syncTime();
            lastSyncHour = local_tm_info.tm_hour;
        }
    }
}