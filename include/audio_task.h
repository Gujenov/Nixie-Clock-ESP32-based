#pragma once

#include <Arduino.h>

enum class AudioTestSource : uint8_t {
	None = 0,
	FlashWav,
	Tone
};

enum class AudioSfxId : uint8_t {
	BleEnabled = 0,
	BleDisabled,
	BleConnected,
	BleDisconnected,
	OperationOk,
	OperationError
};

void audioTaskStart();
bool audioTaskIsRunning();
void audioTaskStop();

bool audioPlayTestFallback();
bool audioPlayTestTone(uint16_t frequencyHz = 880, uint16_t durationMs = 1200);
bool audioPlaySfx(AudioSfxId id);
void audioStopPlayback();
bool audioIsPlaying();
AudioTestSource audioGetLastTestSource();
const char* audioTestSourceName(AudioTestSource source);
