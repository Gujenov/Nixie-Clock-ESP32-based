#pragma once

#include <Arduino.h>

bool initDFPlayer();
bool isDFPlayerReady();
void dfplayerSetVolume(uint8_t volume); // 0..30
void dfplayerPlayFolder(uint8_t folder, uint8_t track);
void dfplayerPlayTrack(uint16_t track); // для папки /MP3
void dfplayerStop();
