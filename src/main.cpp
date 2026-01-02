#include <Arduino.h>
#include "config.h"
#include "hardware.h"
#include "command_handler.h"
#include "input_handler.h"
#include "alarm_handler.h"
#include "time_utils.h"
#include "dcf77_handler.h"

static bool sqwFailed = false;  // false = SQW работает, true = перешли на millis
extern bool printEnabled;        // из time_utils.cpp

void processSecondTick();

void setup() {

    // 1. Инициализация железа (энкодер, кнопки, пины)
    initHardware();
    
    // 2. Загрузка конфигурации
    initConfiguration();
    
    //3.Инициализация NTP клиента
    initNTPClient();
       
    // 4. Проверка источников времени
    checkTimeSource(); 
    
    // 5. Чтение температуры с DS3231 (если доступен)
    printDS3231Temperature();
    
    // 6. Попытка NTP синхронизации (если разрешено)
    if(config.time_config.auto_sync_enabled && strlen(config.wifi_ssid) > 0) {
        syncTime();  // Существующая функция из time_utils.cpp
    }
    
    /* 7. Инициализация DCF77
    if(config.time_config.dcf77_enabled) {
    initDCF77();
    updateDCF77(); // Начинаем приём DCF77
    }
  */
    Serial.print("\n\n=== Система готова ===");
    Serial.println("\n\nhelp,? - Перечень доступных команд");
    printEnabled = true; // Включаем вывод времени по секундам
}

void loop() {
    static unsigned long lastSecondCheck = 0;
    static unsigned long lastSQWCheck = 0;
    unsigned long currentMillis = millis();
    
    // Обработка команд и ввода
    if (Serial.available()) handleSerialCommands();
 
    // === ОБРАБОТКА СЕКУНДНЫХ СОБЫТИЙ ===
    
    // Если DS3231 доступен
    if (ds3231_available) {
        // Прерывание от SQW пришло
        if (timeUpdatedFromSQW) {
            portENTER_CRITICAL(&timerMux);
            timeUpdatedFromSQW = false;
            portEXIT_CRITICAL(&timerMux);
            lastSQWCheck = currentMillis;  // Сброс таймера
            sqwFailed = false;             // SQW снова работает
            //Serial.print("\nПрерывание от SQW получено"); // Отладка
            processSecondTick();
        }
        // SQW не пришло 3 секунды
        else if (!sqwFailed && (currentMillis - lastSQWCheck >= 3000)) {
            sqwFailed = true;
            Serial.print("\n[WARN] SQW не поступает 3 сек, переход на millis!");
            lastSecondCheck = currentMillis;
            processSecondTick();
        }
        // Работаем по millis после отказа SQW
        else if (sqwFailed && (currentMillis - lastSecondCheck >= 1000)) {
            lastSecondCheck = currentMillis;
            processSecondTick();
        }
    }
    // DS3231 не доступен
    else {
        if (currentMillis - lastSecondCheck >= 1000) {
            lastSQWCheck = currentMillis;  // Сброс таймера
            lastSecondCheck = currentMillis;
            processSecondTick();
        }
    }
    delay(10);
}

void processSecondTick() {
    time_t currentTime = getCurrentUTCTime();
    static time_t lastProcessedTime = 0;
    
    if (currentTime == lastProcessedTime) {
        return;  // Эту секунду уже обрабатывали
    }
    
    lastProcessedTime = currentTime;
    
    struct tm* tm_info = gmtime(&currentTime);
    uint8_t currentSecond = tm_info->tm_sec;
    
    if(printEnabled) {
         //Вывод символа каждую секунду для индикации работы
        if ((ds3231_available) && (!sqwFailed)) {
            Serial.print(".");
        } else {
            Serial.print("*");
        }
        // Только полезные действия
        if (currentSecond % 20 == 0) {
            if((ds3231_available) && (sqwFailed)){
                Serial.print("\n[WARN] SQW не доступен");    
            }
        printTime();
        }
    }

    
    
    checkAlarms();
    
    // Синхронизация в полночь и в полдень
    static uint8_t lastSyncHour = 255;

    if ((tm_info->tm_hour == 0 || tm_info->tm_hour == 12) && 
    tm_info->tm_min == 0) {
    
    // Проверяем, что ещё не синхронизировали в этот час
    if (tm_info->tm_hour != lastSyncHour ) {
        syncTime();
        lastSyncHour = tm_info->tm_hour;
    }
    }
}