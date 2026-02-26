#pragma once

#include <Wire.h>
#include <RTClib.h>
#include <ESP32Encoder.h>
#include <Arduino.h>
#include <config.h>

// Конфигурация пинов
#define I2C_SDA 39
#define I2C_SCL 40
#define SQW_PIN 42
#define ENC_A 7
#define ENC_B 15
#define ENC_BTN 18

// Линии для 74HC595 (SPI-подобный интерфейс, bit-bang)
#define SR595_DATA_PIN 5   // SER / DS
#define SR595_CLK_PIN 6    // SRCLK / SHCP
#define SR595_LATCH_PIN 4  // RCLK / STCP

// Алиасы под SPI-терминологию (удобно для будущего перехода)
#define HSPI_MOSI_PIN SR595_DATA_PIN
#define HSPI_SCK_PIN SR595_CLK_PIN
#define HSPI_CS_PIN SR595_LATCH_PIN

// GPIO, задействованные у части ESP32-S3 модулей под внешнюю память (Flash/PSRAM)
#define MEM_GPIO_36 36
#define MEM_GPIO_37 37
#define MEM_GPIO_38 38
#define IS_MEM_GPIO(pin) ((pin) == MEM_GPIO_36 || (pin) == MEM_GPIO_37 || (pin) == MEM_GPIO_38)

// Проверки конфликтов пинов на этапе компиляции
#if (SR595_DATA_PIN == SR595_CLK_PIN) || (SR595_DATA_PIN == SR595_LATCH_PIN) || (SR595_CLK_PIN == SR595_LATCH_PIN)
#error "SR595 pin conflict: DATA/CLK/LATCH must be different GPIOs"
#endif

#if (ENC_A == ENC_B) || (ENC_A == ENC_BTN) || (ENC_B == ENC_BTN)
#error "Encoder pin conflict: ENC_A/ENC_B/ENC_BTN must be different GPIOs"
#endif

#if (ENC_BTN == DFPLAYER_TX_PIN) || (ENC_BTN == DFPLAYER_RX_PIN)
#warning "Pin conflict: ENC_BTN conflicts with DFPlayer UART pin (fix before enabling DFPlayer)"
#endif

// Жёсткая защита от использования memory GPIO в пользовательской обвязке
#if IS_MEM_GPIO(I2C_SDA) || IS_MEM_GPIO(I2C_SCL) || IS_MEM_GPIO(SQW_PIN) || \
	IS_MEM_GPIO(ENC_A) || IS_MEM_GPIO(ENC_B) || IS_MEM_GPIO(ENC_BTN) || \
	IS_MEM_GPIO(SR595_DATA_PIN) || IS_MEM_GPIO(SR595_CLK_PIN) || IS_MEM_GPIO(SR595_LATCH_PIN) || \
	IS_MEM_GPIO(LED_PIN) || IS_MEM_GPIO(DFPLAYER_TX_PIN) || IS_MEM_GPIO(DFPLAYER_RX_PIN)
#error "Forbidden GPIO assignment: GPIO36/37/38 are reserved for memory lines on this ESP32-S3 build"
#endif

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