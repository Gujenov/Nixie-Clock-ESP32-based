#include "hardware.h"
#include "config.h"

#include <math.h>

// Инициализация объектов
ESP32Encoder encoder;
RTC_DS3231 *rtc = nullptr;
hw_timer_t *timer = NULL;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;
HardwareSource currentTimeSource = INTERNAL_RTC;
bool ds3231_available = false;
volatile bool timeUpdatedFromSQW = false;
static bool sqwInterruptAttached = false;
static bool displayOutputEnabled = true;

static constexpr uint8_t SR595_OE_ACTIVE_LEVEL = LOW;
static constexpr uint8_t SR595_OE_INACTIVE_LEVEL = HIGH;

void initHardware() {
// Настройка пинов Энкодера и кнопок
    pinMode(ENC_A, INPUT_PULLUP);
    pinMode(ENC_B, INPUT_PULLUP);
    pinMode(ENC_BTN, INPUT_PULLUP);
    
    pinMode(ALARM_BTN, INPUT_PULLUP);
    pinMode(LIGHT_SENSOR_PIN, INPUT);
    analogSetPinAttenuation(LIGHT_SENSOR_PIN, ADC_11db);

    // Линии 74HC595: сразу переводим в выход и удерживаем в лог.0
    pinMode(SR595_DATA_PIN, OUTPUT);
    pinMode(SR595_CLK_PIN, OUTPUT);
    pinMode(SR595_LATCH_PIN, OUTPUT);
    pinMode(SR595_OE_PIN, OUTPUT);
    digitalWrite(SR595_DATA_PIN, LOW);
    digitalWrite(SR595_CLK_PIN, LOW);
    digitalWrite(SR595_LATCH_PIN, LOW);
    digitalWrite(SR595_OE_PIN, SR595_OE_ACTIVE_LEVEL);
    displayOutputEnabled = true;

    // Инициализация энкодера
    encoder.attachSingleEdge(ENC_A, ENC_B);
    encoder.setFilter(15000);
    encoder.setCount(0);

    Serial.begin(115200);
    delay(300);

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);
    
}

void setDisplayOutputEnabled(bool enabled) {
    displayOutputEnabled = enabled;
    digitalWrite(SR595_OE_PIN, enabled ? SR595_OE_ACTIVE_LEVEL : SR595_OE_INACTIVE_LEVEL);
}

bool isDisplayOutputEnabled() {
    return displayOutputEnabled;
}

uint16_t readLightSensorFiltered(uint8_t samples, uint8_t adcResolutionBits) {
    if (samples == 0) samples = 1;
    if (samples > 64) samples = 64;

    if (adcResolutionBits < 9) adcResolutionBits = 9;
    if (adcResolutionBits > 12) adcResolutionBits = 12;

    analogReadResolution(adcResolutionBits);

    uint32_t acc = 0;
    for (uint8_t i = 0; i < samples; ++i) {
        acc += static_cast<uint32_t>(analogRead(LIGHT_SENSOR_PIN));
        delayMicroseconds(250);
    }

    uint16_t avg = static_cast<uint16_t>(acc / samples);

    const uint8_t shift = static_cast<uint8_t>(adcResolutionBits - 10);
    if (shift > 0) {
        avg = static_cast<uint16_t>(avg >> shift);
    }
    if (avg > 1023) avg = 1023;
    return avg;
}

void IRAM_ATTR onSQWInterrupt() {
    timeUpdatedFromSQW = true;
}

void setupInterrupts() {
    
    // Всегда отключаем старые прерывания перед настройкой новых
    if (currentTimeSource == EXTERNAL_DS3231 && ds3231_available) {
        // Работаем от DS3231 - настраиваем SQW
        pinMode(SQW_PIN, INPUT_PULLUP);
        if (!sqwInterruptAttached) {
            attachInterrupt(digitalPinToInterrupt(SQW_PIN), onSQWInterrupt, FALLING);
            sqwInterruptAttached = true;
        }
        Serial.print("\n[SYSTEM] Используется прерывание от DS3231");
    } else {
        // Работаем от внутреннего RTC - НЕ используем прерывания
        // Отключаем SQW только если ранее действительно подключали ISR
        if (sqwInterruptAttached) {
            detachInterrupt(digitalPinToInterrupt(SQW_PIN));
            sqwInterruptAttached = false;
        }
        Serial.print("\n[SYSTEM] Отчет прерываний по внутреннему счёту ESP32 [мсек]");
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

float getDS3231Temperature() {
    if (!rtc || !ds3231_available) {
        return -999.0; // Код ошибки
    }
    
    try {
        return rtc->getTemperature();
    } catch (...) {
        return -999.0;
    }
}

// Форматированный вывод
void printDS3231Temperature() {
    if (!ds3231_available) {
        Serial.print("\n[ERR] Температура DS3231 недоступна");
        return;
    }
    
    float temp = getDS3231Temperature();
    if (temp > -100.0) { // Проверяем не код ошибки ли
        Serial.printf("\n[DS3231] Температура: %.1f°C", temp);
    } else {
        Serial.print("\n[ERR] Ошибка чтения температуры DS3231");
    }
}

float getESP32Temperature() {
    const float t = temperatureRead();
    if (isnan(t) || isinf(t)) {
        return -999.0f;
    }
    return t;
}

void printESP32Temperature() {
    float t = getESP32Temperature();
    if (t > -100.0f) {
        Serial.printf("\n[ESP32] Температура: %.1f°C", t);
    } else {
        Serial.print("\n[ESP32] Температура: недоступна");
    }
}
