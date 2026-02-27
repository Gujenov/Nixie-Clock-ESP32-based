#pragma once

#include <Arduino.h>
#include "config.h"

void otaInit();
bool otaEnable(uint32_t windowMs = OTA_WINDOW_MS);
void otaDisable();
void otaProcess();

bool otaIsEnabled();
bool otaIsBusy();
uint32_t otaSecondsLeft();
