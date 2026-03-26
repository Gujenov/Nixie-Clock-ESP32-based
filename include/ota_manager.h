#pragma once

#include <Arduino.h>
#include "config.h"

void otaInit();
bool otaEnable(uint32_t windowMs = OTA_WINDOW_MS);
void otaDisable();
void otaProcess();

// Колбэк вызывается в момент старта OTA-передачи (ArduinoOTA.onStart).
void otaSetTransferStartCallback(void (*callback)());

bool otaIsEnabled();
bool otaIsBusy();
uint32_t otaSecondsLeft();
