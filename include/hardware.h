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
#define POWER_FAIL_PIN 9
#define ENC_A 1
#define ENC_B 2
#define ENC_BTN 8

#define ALARM_BTN 7

// Линии для 74HC595 (SPI-подобный интерфейс, bit-bang)
#define SR595_DATA_PIN 5   // SER / DS
#define SR595_CLK_PIN 6    // SRCLK / SHCP
#define SR595_LATCH_PIN 4  // RCLK / STCP
#define SR595_OE_PIN 45    // OE (active LOW): LOW=output enabled, HIGH=blank

// Алиасы под SPI-терминологию (удобно для будущего перехода)
#define HSPI_MOSI_PIN SR595_DATA_PIN
#define HSPI_SCK_PIN SR595_CLK_PIN
#define HSPI_CS_PIN SR595_LATCH_PIN

// GPIO, задействованные у части ESP32-S3 модулей под внешнюю память (Flash/PSRAM)
#define MEM_GPIO_36 36
#define MEM_GPIO_37 37
#define MEM_GPIO_38 38
#define IS_MEM_GPIO(pin) ((pin) == MEM_GPIO_36 || (pin) == MEM_GPIO_37 || (pin) == MEM_GPIO_38)

// ESP32-S3 USB OTG линии, зарезервированные под USB (D-/D+)
#define USB_GPIO_DM 19
#define USB_GPIO_DP 20
#define IS_USB_RESERVED_GPIO(pin) ((pin) == USB_GPIO_DM || (pin) == USB_GPIO_DP)

// Проверки конфликтов пинов на этапе компиляции
#if (SR595_DATA_PIN == SR595_CLK_PIN) || (SR595_DATA_PIN == SR595_LATCH_PIN) || (SR595_DATA_PIN == SR595_OE_PIN) || \
	(SR595_CLK_PIN == SR595_LATCH_PIN) || (SR595_CLK_PIN == SR595_OE_PIN) || (SR595_LATCH_PIN == SR595_OE_PIN)
#error "SR595 pin conflict: DATA/CLK/LATCH/OE must be different GPIOs"
#endif

#if (ENC_A == ENC_B) || (ENC_A == ENC_BTN) || (ENC_B == ENC_BTN)
#error "Encoder pin conflict: ENC_A/ENC_B/ENC_BTN must be different GPIOs"
#endif

#if (ALARM_BTN == ENC_A) || (ALARM_BTN == ENC_B) || (ALARM_BTN == ENC_BTN)
#warning "Pin conflict: ALARM_BTN conflicts with encoder lines/buttons (set unique GPIO for stable operation)"
#endif

#if (POWER_FAIL_PIN == I2C_SDA) || (POWER_FAIL_PIN == I2C_SCL) || (POWER_FAIL_PIN == SQW_PIN) || \
	(POWER_FAIL_PIN == ENC_A) || (POWER_FAIL_PIN == ENC_B) || (POWER_FAIL_PIN == ENC_BTN) || (POWER_FAIL_PIN == ALARM_BTN) || \
	(POWER_FAIL_PIN == SR595_DATA_PIN) || (POWER_FAIL_PIN == SR595_CLK_PIN) || (POWER_FAIL_PIN == SR595_LATCH_PIN) || (POWER_FAIL_PIN == SR595_OE_PIN) || \
	(POWER_FAIL_PIN == AUDIO_I2S_BCLK_PIN) || (POWER_FAIL_PIN == AUDIO_I2S_LRCLK_PIN) || (POWER_FAIL_PIN == AUDIO_I2S_DOUT_PIN) || \
	(POWER_FAIL_PIN == SD_SPI_SCK_PIN) || (POWER_FAIL_PIN == SD_SPI_MOSI_PIN) || (POWER_FAIL_PIN == SD_SPI_MISO_PIN) || (POWER_FAIL_PIN == SD_SPI_CS_PIN)
#error "Pin conflict: POWER_FAIL_PIN overlaps with active peripheral pins"
#endif

// Защита от пересечения линий индикации (74HC595) и SPI microSD
#if (SR595_DATA_PIN == SD_SPI_SCK_PIN) || (SR595_DATA_PIN == SD_SPI_MOSI_PIN) || (SR595_DATA_PIN == SD_SPI_MISO_PIN) || (SR595_DATA_PIN == SD_SPI_CS_PIN) || \
	(SR595_CLK_PIN == SD_SPI_SCK_PIN) || (SR595_CLK_PIN == SD_SPI_MOSI_PIN) || (SR595_CLK_PIN == SD_SPI_MISO_PIN) || (SR595_CLK_PIN == SD_SPI_CS_PIN) || \
	(SR595_LATCH_PIN == SD_SPI_SCK_PIN) || (SR595_LATCH_PIN == SD_SPI_MOSI_PIN) || (SR595_LATCH_PIN == SD_SPI_MISO_PIN) || (SR595_LATCH_PIN == SD_SPI_CS_PIN) || \
	(SR595_OE_PIN == SD_SPI_SCK_PIN) || (SR595_OE_PIN == SD_SPI_MOSI_PIN) || (SR595_OE_PIN == SD_SPI_MISO_PIN) || (SR595_OE_PIN == SD_SPI_CS_PIN)
#error "Pin conflict: 74HC595 display lines must not overlap with microSD SPI lines"
#endif

// Жёсткая защита от использования memory GPIO в пользовательской обвязке
#if IS_MEM_GPIO(I2C_SDA) || IS_MEM_GPIO(I2C_SCL) || IS_MEM_GPIO(SQW_PIN) || \
	IS_MEM_GPIO(POWER_FAIL_PIN) || \
	IS_MEM_GPIO(ENC_A) || IS_MEM_GPIO(ENC_B) || IS_MEM_GPIO(ENC_BTN) || IS_MEM_GPIO(ALARM_BTN) || \
	IS_MEM_GPIO(SR595_DATA_PIN) || IS_MEM_GPIO(SR595_CLK_PIN) || IS_MEM_GPIO(SR595_LATCH_PIN) || IS_MEM_GPIO(SR595_OE_PIN) || \
	IS_MEM_GPIO(AUDIO_I2S_BCLK_PIN) || IS_MEM_GPIO(AUDIO_I2S_LRCLK_PIN) || IS_MEM_GPIO(AUDIO_I2S_DOUT_PIN) || \
	IS_MEM_GPIO(SD_SPI_SCK_PIN) || IS_MEM_GPIO(SD_SPI_MOSI_PIN) || IS_MEM_GPIO(SD_SPI_MISO_PIN) || IS_MEM_GPIO(SD_SPI_CS_PIN)
#error "Forbidden GPIO assignment: GPIO36/37/38 are reserved for memory lines on this ESP32-S3 build"
#endif

// Жёсткая защита USB: GPIO19/20 недоступны для пользовательских функций
#if IS_USB_RESERVED_GPIO(I2C_SDA) || IS_USB_RESERVED_GPIO(I2C_SCL) || IS_USB_RESERVED_GPIO(SQW_PIN) || \
	IS_USB_RESERVED_GPIO(POWER_FAIL_PIN) || \
	IS_USB_RESERVED_GPIO(ENC_A) || IS_USB_RESERVED_GPIO(ENC_B) || IS_USB_RESERVED_GPIO(ENC_BTN) || IS_USB_RESERVED_GPIO(ALARM_BTN) || \
	IS_USB_RESERVED_GPIO(SR595_DATA_PIN) || IS_USB_RESERVED_GPIO(SR595_CLK_PIN) || IS_USB_RESERVED_GPIO(SR595_LATCH_PIN) || IS_USB_RESERVED_GPIO(SR595_OE_PIN) || \
	IS_USB_RESERVED_GPIO(AUDIO_I2S_BCLK_PIN) || IS_USB_RESERVED_GPIO(AUDIO_I2S_LRCLK_PIN) || IS_USB_RESERVED_GPIO(AUDIO_I2S_DOUT_PIN) || \
	IS_USB_RESERVED_GPIO(SD_SPI_SCK_PIN) || IS_USB_RESERVED_GPIO(SD_SPI_MOSI_PIN) || IS_USB_RESERVED_GPIO(SD_SPI_MISO_PIN) || IS_USB_RESERVED_GPIO(SD_SPI_CS_PIN)
#error "Forbidden GPIO assignment: GPIO19/GPIO20 are reserved for USB (D-/D+)"
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
float getESP32Temperature();
void printESP32Temperature();

// Считывание датчика освещенности (совместимый API, ожидаемый диапазон 0..1023)
uint16_t readLightSensorFiltered(uint8_t samples, uint8_t adcResolutionBits);

// Управление OE линии 74HC595 (true = включить выходы, false = погасить индикаторы)
void setDisplayOutputEnabled(bool enabled);
bool isDisplayOutputEnabled();