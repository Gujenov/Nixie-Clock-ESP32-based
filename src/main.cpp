#include <Arduino.h>
#include "config.h"
#include "hardware.h"
#include "time_utils.h"
#include "dst_handler.h"
#include "command_handler.h"
#include "alarm_handler.h"
#include "button_handler.h"

// НЕТ объявлений глобальных переменных - они уже в .cpp файлах!

void setup() {
    // Настройка пинов Энкодера
    pinMode(ENC_A, INPUT_PULLUP);
    pinMode(ENC_B, INPUT_PULLUP);
    pinMode(ENC_BTN, INPUT_PULLUP);

    // Инициализация энкодера
    encoder.attachSingleEdge(ENC_A, ENC_B);
    encoder.setFilter(5000);
    encoder.setCount(0);

    Serial.begin(115200);
    delay(500);
    
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);

    initConfiguration();
    timeClient = new NTPClient(ntpUDP, config.ntp_server, 0);
    initTimeSource();
    syncTime();
    setTimeZone(config.timezone_offset, config.dst_enabled, config.dst_preset_index);
    
    setupInterrupts();
    printSystemInfo();
    printHelp();
    printEnabled = true;
}

void loop() {
    static time_t lastDisplayTime = 0;
    static int32_t lastPos = 0;

    // Обработка кнопки
    uint8_t buttonEvent = CheckButton();
    switch (buttonEvent) {
        case BUTTON_PRESSED:
            Serial.println("Короткое нажатие");
            break;
        case BUTTON_LONG:
            Serial.println("Длинное нажатие");
            break;
        case BUTTON_VERY_LONG:
            Serial.println("Очень длинное нажатие");
            break;
    }

    // Обработка энкодера
    int32_t currentPos = encoder.getCount();
    if (currentPos != lastPos) {
        Serial.printf("%d\n", currentPos);
        lastPos = currentPos;
    }

    // Обновление времени
    if(timeUpdated) {
        portENTER_CRITICAL(&timerMux);
        timeUpdated = false;
        portEXIT_CRITICAL(&timerMux);
        
        time_t currentTime = getCurrentTime();
        if(currentTime != lastDisplayTime) {
            lastDisplayTime = currentTime;
            updateDisplay(currentTime);
            
            struct tm *timeinfo = localtime(&currentTime);
            uint8_t seconds = timeinfo->tm_sec;
            
            if((seconds % 20 == 0) && (printEnabled)) {
                printTime();
            }
        }
    }

    // Обработка команд и будильников
    if(Serial.available()) {
        handleSerialCommands();
    }
    
    checkAlarms();
    delay(10);
}