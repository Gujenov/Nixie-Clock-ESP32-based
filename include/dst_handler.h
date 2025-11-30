#pragma once
#include <Arduino.h>

extern const char* DST_PRESETS[];

bool setDstPreset(uint8_t index);
bool setDstPresetByName(const String& name);
void printDstPresets();
bool setDstPresetByName(const String& name);