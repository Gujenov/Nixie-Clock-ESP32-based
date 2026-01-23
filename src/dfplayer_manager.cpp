#include "dfplayer_manager.h"
#include "config.h"
#include <DFRobotDFPlayerMini.h>

static HardwareSerial dfSerial(1);
static DFRobotDFPlayerMini dfPlayer;
static bool dfPlayerReady = false;

bool initDFPlayer() {
    dfSerial.begin(9600, SERIAL_8N1, DFPLAYER_RX_PIN, DFPLAYER_TX_PIN);
    delay(300);

    if (!dfPlayer.begin(dfSerial)) {
        Serial.print("\n[DFP] Ошибка инициализации DFPlayer");
        dfPlayerReady = false;
        return false;
    }

    dfPlayerReady = true;
    dfPlayer.setTimeOut(500);
    dfPlayer.volume(20); // 0..30
    dfPlayer.EQ(DFPLAYER_EQ_NORMAL);
    Serial.print("\n[DFP] DFPlayer готов");
    return true;
}

bool isDFPlayerReady() {
    return dfPlayerReady;
}

void dfplayerSetVolume(uint8_t volume) {
    if (!dfPlayerReady) return;
    if (volume > 30) volume = 30;
    dfPlayer.volume(volume);
}

void dfplayerPlayFolder(uint8_t folder, uint8_t track) {
    if (!dfPlayerReady) return;
    dfPlayer.playFolder(folder, track);
}

void dfplayerPlayTrack(uint16_t track) {
    if (!dfPlayerReady) return;
    dfPlayer.playMp3Folder(track);
}

void dfplayerStop() {
    if (!dfPlayerReady) return;
    dfPlayer.stop();
}
