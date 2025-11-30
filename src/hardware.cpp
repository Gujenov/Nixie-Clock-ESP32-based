#include "hardware.h"
#include "config.h"

// Инициализация объектов
ESP32Encoder encoder;
RTC_DS3231 *rtc = nullptr;
hw_timer_t *timer = NULL;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;
volatile bool timeUpdated = false;
TimeSource currentTimeSource = INTERNAL_RTC;

void setupInterrupts() {
  if(currentTimeSource == EXTERNAL_DS3231 && rtc) {
    rtc->writeSqwPinMode(DS3231_SquareWave1Hz);
    pinMode(SQW_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(SQW_PIN), onTimeInterrupt, FALLING);
    Serial.println("Используются аппаратные прерывания SQW");
  } else {
    // Настройка таймера для ESP32-S3 (240 MHz)
    timer = timerBegin(0, TIMER_DIVIDER, true);  // Таймер 0, делитель 80, счет вверх
    timerAttachInterrupt(timer, onTimeInterrupt, true);
    timerAlarmWrite(timer, TIMER_INTERVAL, true);  // 3,000,000 тиков, автоповтор
    timerAlarmEnable(timer);  // Включаем таймер
    Serial.println("Используется программный таймер");
  }
}

void blinkError(int count) {
  pinMode(LED_PIN, OUTPUT);
  for(int i = 0; i < count; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(200);
    digitalWrite(LED_PIN, LOW);
    delay(200);
  }
}

void IRAM_ATTR onTimeInterrupt() {
    portENTER_CRITICAL_ISR(&timerMux);
    timeUpdated = true;
    digitalWrite(LED_PIN, HIGH);
    portEXIT_CRITICAL_ISR(&timerMux);
}

void initTimeSource() {
    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(100000);
    
    Wire.beginTransmission(0x68);
    if(Wire.endTransmission() == 0) {
        rtc = new RTC_DS3231();
        if(rtc && rtc->begin()) {
            currentTimeSource = EXTERNAL_DS3231;
            Serial.println("Используется внешний RTC (DS3231)");
            return;
        }
        if(rtc) delete rtc;
    }
    
    currentTimeSource = INTERNAL_RTC;
    Serial.println("Используется внутренний RTC");
}

void updateDisplay(time_t now) {
    digitalWrite(LED_PIN, LOW);
}

time_t getRTCTime() {
    if(currentTimeSource == EXTERNAL_DS3231 && rtc) {
        return rtc->now().unixtime();
    } else {
        struct tm timeinfo;
        getLocalTime(&timeinfo);
        return mktime(&timeinfo);
    }
}