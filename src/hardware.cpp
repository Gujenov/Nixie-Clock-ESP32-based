#include "hardware.h"
#include "config.h"

// Инициализация объектов
ESP32Encoder encoder;
RTC_DS3231 *rtc = nullptr;
hw_timer_t *timer = NULL;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;
HardwareSource currentTimeSource = INTERNAL_RTC;
bool ds3231_available = false;
volatile bool timeUpdatedFromSQW = false;

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

void IRAM_ATTR onSQWInterrupt() {
    timeUpdatedFromSQW = true;
}

void setupInterrupts() {
    static bool interruptsConfigured = false;
    
    // Если уже настроены для текущего источника - ничего не делаем
    if (interruptsConfigured) {
        return;
    }
    
    // Всегда отключаем старые прерывания перед настройкой новых
    if (currentTimeSource == EXTERNAL_DS3231 && ds3231_available) {
        // Работаем от DS3231 - настраиваем SQW
        pinMode(SQW_PIN, INPUT_PULLUP);
        attachInterrupt(digitalPinToInterrupt(SQW_PIN), onSQWInterrupt, FALLING);
        Serial.println("Используется прерывание от DS3231");
    } else {
        // Работаем от внутреннего RTC - НЕ используем прерывания
        // Просто отключаем SQW если был подключен
        detachInterrupt(digitalPinToInterrupt(SQW_PIN));
        Serial.println("Используется внутренний счёт в мсекундах ESP32  (без прерываний)");
    }
    
    interruptsConfigured = true;
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


