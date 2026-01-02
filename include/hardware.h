#pragma once

#include <Wire.h>
#include <RTClib.h>
#include <ESP32Encoder.h>
#include <Arduino.h>
#include <config.h>

// Конфигурация пинов
#define I2C_SDA 4
#define I2C_SCL 5
#define SQW_PIN 6
#define ENC_A 15
#define ENC_B 16
#define ENC_BTN 17

extern ESP32Encoder encoder;
extern RTC_DS3231 *rtc;
extern hw_timer_t *timer;
extern portMUX_TYPE timerMux;
extern bool ds3231_available;
extern volatile bool timeUpdatedFromSQW;

void setupInterrupts();
void blinkError(int count);
void initHardware();
void IRAM_ATTR onTimeInterrupt();
void initTimeSource();
void updateDisplay(time_t now);
time_t getRTCTime();
void updateDayOfWeekInRTC();
float getDS3231Temperature();
void printDS3231Temperature();