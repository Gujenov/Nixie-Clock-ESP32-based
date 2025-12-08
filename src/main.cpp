#include <Arduino.h>
#include "config.h"
#include "hardware.h"
#include "command_handler.h"
#include "button_handler.h"
#include "alarm_handler.h"
#include "time_utils.h"

// Для периодической синхронизации
unsigned long lastWiFiSyncCheck = 0;
#define WIFI_SYNC_INTERVAL (12 * 3600 * 1000) // 12 часов

void processTimeUpdate();

void setup() {

    // 1. Инициализация железа (энкодер, кнопки, пины)
    initHardware();
    
    // 2. Загрузка конфигурации
    initConfiguration();
    
    // 3. Проверка источников времени
    initTimeSource(); 
    
    // 4. Установка часового пояса
    setTimeZone(config.time_config.timezone_offset, 
                config.time_config.dst_enabled,
                config.time_config.dst_preset_index);
    
    // 5. Попытка NTP синхронизации (если разрешено)
    if(config.time_config.auto_sync_enabled && strlen(config.wifi_ssid) > 0) {
        Serial.println("Попытка NTP синхронизации...");
        syncTime();  // Существующая функция из time_utils.cpp
    }
    
    // 6. Инициализация DCF77 (если включено)
    #ifdef ENABLE_DCF77
    if(config.time_config.dcf77_enabled) {
        initDCF77();
        // Запуск таймера ожидания
    }
    #endif
    
    // 7. Настройка прерываний
    setupInterrupts();
    
    Serial.println("\n=== Система готова ===");
    printTime();
}

void loop() {
    static unsigned long lastLoop = 0;
    static unsigned long lastSecondTick = 0;
    static unsigned long lastSyncCheck = 0;
    static bool syncAttemptedThisHour = false;
    unsigned long currentMillis = millis();
    
    // === БЫСТРЫЕ ОПЕРАЦИИ (выполняются часто) ===
    
    // 1. Обработка кнопки энкодера (каждые 20ms)
    if (currentMillis - lastLoop >= 20) {
        lastLoop = currentMillis;
        uint8_t buttonEvent = CheckButton();
        // Обработка кнопки...
    }
    
    // 2. Обработка Serial команд (если есть данные)
    if (Serial.available()) {
        handleSerialCommands();
    }
    
    // === ОБНОВЛЕНИЕ ВРЕМЕНИ ===
    
    if (timeUpdated) {
        // Прерывание от внешних часов (DS3231)
        portENTER_CRITICAL(&timerMux);
        timeUpdated = false;
        portEXIT_CRITICAL(&timerMux);
        
        processTimeUpdate();
    } else if (currentTimeSource == INTERNAL_RTC) {
        // Для внутренних часов - имитируем прерывание каждую секунду
        if (currentMillis - lastSecondTick >= 1000) {
            lastSecondTick = currentMillis;
            processTimeUpdate();
        }
    }
    
    // === ПРОВЕРКА СИНХРОНИЗАЦИИ (12:00 и 00:00) ===
    
    if (currentMillis - lastSyncCheck >= 60000) { // Проверяем раз в минуту
        lastSyncCheck = currentMillis;
        
        time_t now = getCurrentTime();
        struct tm* tm_now = localtime(&now);
        
        // Проверяем полночь и полдень
        if ((tm_now->tm_hour == 0 || tm_now->tm_hour == 12) && 
            tm_now->tm_min == 0 && 
            tm_now->tm_sec < 10) { // В первые 10 секунд часа
            
            if (!syncAttemptedThisHour) {
                syncAttemptedThisHour = true;
                
                // NTP синхронизация (если разрешена и WiFi есть)
                if (config.time_config.auto_sync_enabled && 
                    strlen(config.wifi_ssid) > 0) {
                    
                    Serial.println("[Auto-sync] Scheduled NTP sync (12:00/00:00)");
                    syncTime();
                }
                
                // DCF77 синхронизация (если включена)
                #ifdef ENABLE_DCF77
                if (config.time_config.dcf77_enabled) {
                    Serial.println("[Auto-sync] Attempting DCF77 sync");
                    // Здесь будет вызов syncWithDCF77();
                }
                #endif
            }
        } else {
            // Сбрасываем флаг после часа
            syncAttemptedThisHour = false;
        }
    }
    
    // === ОБРАБОТКА DCF77 (если включено) ===
    #ifdef ENABLE_DCF77
    if (config.time_config.dcf77_enabled) {
        static unsigned long lastDCFCheck = 0;
        if (currentMillis - lastDCFCheck >= 100) { // Проверяем каждые 100ms
            lastDCFCheck = currentMillis;
            handleDCF77();
        }
    }
    #endif
    
    // === ПРОВЕРКА БУДИЛЬНИКОВ ===
    checkAlarms();
    
    // === ОБРАБОТКА WIFI (если используется) ===
    #ifdef ENABLE_WIFI
    // Здесь может быть поддержание соединения, если нужно
    #endif
    
    delay(1);
}

// Вспомогательная функция для обработки обновления времени
void processTimeUpdate() {
    time_t currentTime = getCurrentTime();
    
    // Выводим в Serial каждые 20 секунд
    if (printEnabled) {
        struct tm *timeinfo = localtime(&currentTime);
        if (timeinfo->tm_sec % 20 == 0) {
            printTime();
        }
    }
    
    // Здесь можно добавить обновление дисплея (когда будет)
    // updateDisplay(currentTime);
    
    // Проверяем, не пора ли отправить heartbeat или другую периодическую задачу
    static time_t lastMinuteCheck = 0;
    if (currentTime / 60 != lastMinuteCheck / 60) {
        lastMinuteCheck = currentTime;
        // Каждую минуту можно что-то делать
        // например, проверять температуру с DS3231
    }
}