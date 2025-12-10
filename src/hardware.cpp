#include "hardware.h"
#include "config.h"

// Инициализация объектов
ESP32Encoder encoder;
RTC_DS3231 *rtc = nullptr;
hw_timer_t *timer = NULL;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;
volatile bool timeUpdated = false;
HardwareSource currentTimeSource = INTERNAL_RTC;
bool ds3231_available = false;

void initHardware() {
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
}

void setupInterrupts() {
  if(currentTimeSource == EXTERNAL_DS3231 && rtc) {
    rtc->writeSqwPinMode(DS3231_SquareWave1Hz);
    pinMode(SQW_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(SQW_PIN), onTimeInterrupt, FALLING);
    Serial.println("Используется прерывание от DS3231");
  } else {
    // Настройка таймера для ESP32-S3 (240 MHz)
    timer = timerBegin(0, TIMER_DIVIDER, true);  // Таймер 0, делитель 80, счет вверх
    timerAttachInterrupt(timer, onTimeInterrupt, true);
    timerAlarmWrite(timer, TIMER_INTERVAL, true);  // 3,000,000 тиков, автоповтор
    timerAlarmEnable(timer);  // Включаем таймер
    Serial.println("Прерывания от внутреннего таймера настроены (DS3231 не доступен)");
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


void updateDisplay(time_t now) {
    digitalWrite(LED_PIN, LOW);
}