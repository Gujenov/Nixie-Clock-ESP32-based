#pragma once
#include <Arduino.h>
#include "DCF77.h"  // Ваша библиотека
#include "config.h"

// Конфигурация пинов DCF77
#define DCF_DATA_PIN 7     // Пин данных (GPIO12)
#define DCF_ENABLE_PIN 14   // Пин включения модуля (GPIO14)
#define DCF_INTERRUPT digitalPinToInterrupt(DCF_DATA_PIN)

void initDCF77();
void updateDCF77();
bool isDCF77SignalAvailable();
time_t getDCF77Time();
void dcf77Enable(bool enable);
const char* getDCF77Status();