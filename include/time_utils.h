#pragma once

#include <WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Arduino.h>

extern WiFiUDP ntpUDP;
extern NTPClient *timeClient;

time_t getCurrentTime();
bool syncTime();
void setTimeZone(int8_t offset, bool dst_enabled, uint8_t preset_index);
bool printTime();
bool setManualTime(const String &timeStr);
bool setManualDate(const String &dateStr);