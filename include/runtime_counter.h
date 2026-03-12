#pragma once

#include <Arduino.h>

void runtimeCounterInit();
void runtimeCounterOnSecondTick();
bool runtimeCounterSaveNow();
bool runtimeCounterMarkService(uint32_t serviceDateYmd); // YYYYMMDD
bool runtimeCounterResetAll();

uint32_t runtimeCounterGetBootCount();
uint64_t runtimeCounterGetTotalRunSeconds();
float runtimeCounterGetMotorHours();
uint32_t runtimeCounterGetUnsavedSeconds();
uint32_t runtimeCounterGetLastServiceDate();  // YYYYMMDD, 0 = не задано
uint64_t runtimeCounterGetLastServiceRunSeconds();
float runtimeCounterGetLastServiceMotorHours();
