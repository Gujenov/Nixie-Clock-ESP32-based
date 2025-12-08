#include <Arduino.h>
#include "config.h"
#include "hardware.h"
#include "time_manager.h"
#include "command_handler.h"
#include "button_handler.h"
#include "alarm_handler.h"


// Для периодической синхронизации
unsigned long lastWiFiSyncCheck = 0;
#define WIFI_SYNC_INTERVAL (12 * 3600 * 1000) // 12 часов

void setup() {
    // Настройка пинов Энкодера
    pinMode(ENC_A, INPUT_PULLUP);
    pinMode(ENC_B, INPUT_PULLUP);
    pinMode(ENC_BTN, INPUT_PULLUP);

    // Инициализация энкодера
    encoder.attachSingleEdge(ENC_A, ENC_B);
    encoder.setFilter(15000);
    encoder.setCount(0);

    Serial.begin(115200);
    delay(300);

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);

    // 1. Загрузка конфигурации
    initConfiguration();
    
    // 2. Инициализация аппаратных источников времени
    initTimeSource();
    
    // 3. Инициализация TimeManager (делает setTimeZone из конфига)
    timeManager.init();
    
    // 4. Синхронизация времени (если автосинхронизация включена)
    if (config.time_config.auto_sync_enabled && WiFi.status() == WL_CONNECTED) {
        timeManager.syncWithNTP();
    }
    
    // 5. Настройка прерываний
    setupInterrupts();
    
   
    Serial.println("\n=== Система готова ===");
    timeManager.printTimeInfo();
}

void loop() {
    static unsigned long lastLoop = 0;
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
    
    // === ОБНОВЛЕНИЕ ВРЕМЕНИ (по прерыванию) ===
    if (timeUpdated) {
        portENTER_CRITICAL(&timerMux);
        timeUpdated = false;
        portEXIT_CRITICAL(&timerMux);
        
        // Получаем текущее время
        time_t currentTime = timeManager.getUTCTime();
        
        // Выводим в Serial каждые 20 секунд
        struct tm *timeinfo = localtime(&currentTime);
        if ((timeinfo->tm_sec % 20 == 0) && printEnabled) {
            printTime();
        }
    }
    
    // === МЕДЛЕННЫЕ ОПЕРАЦИИ (раз в секунду или реже) ===
    
    // 1. Синхронизация по WiFi (раз в 12 часов)
    if (currentMillis - lastWiFiSyncCheck >= 1000) { // Проверяем каждую секунду
        lastWiFiSyncCheck = currentMillis;
        
        // Если автосинхронизация включена и прошло 12 часов
        if (config.time_config.auto_sync_enabled && 
            WiFi.status() == WL_CONNECTED &&
            currentMillis - lastWiFiSyncCheck >= WIFI_SYNC_INTERVAL) {
            
            Serial.println("[Auto-sync] Starting periodic NTP sync");
            timeManager.syncWithNTP();
            lastWiFiSyncCheck = currentMillis;
        }
    }
    
    // 2. Проверка будильников
    checkAlarms();
    
    // Небольшая задержка для стабильности
    delay(1);
}

// Пример добавления команды в command_handler для TimeManager
// В command_handler.cpp можно добавить:
/*
void handleTimeCommand() {
    timeManager.printTimeInfo();
}

void handleSyncCommand() {
    timeManager.syncWithNTP();
}
*/