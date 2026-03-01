#include "ota_manager.h"

#include "config.h"
#include "time_utils.h"
#include "ble_terminal.h"

#include <WiFi.h>
#include <ArduinoOTA.h>
#include <string.h>

namespace {
volatile bool g_otaBusy = false;
bool g_otaEnabled = false;
bool g_wifiOwnedByOta = false;
unsigned long g_windowDeadlineMs = 0;
bool g_bleWasEnabledBeforeOta = false;

bool connectWifiForOta() {
    if (WiFi.status() == WL_CONNECTED) {
        g_wifiOwnedByOta = false;
        return true;
    }

    // Стабильнее для ESP32-S3 (особенно при OPI/PSRAM)
    WiFi.useStaticBuffers(true);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);

    struct Cred {
        const char* ssid;
        const char* pass;
        const char* label;
    };

    Cred creds[2] = {
        {config.wifi_ssid, config.wifi_pass, "WiFi 1"},
        {config.wifi_ssid_2, config.wifi_pass_2, "WiFi 2"}
    };

    const uint32_t deadline = millis() + OTA_CONNECT_TIMEOUT_MS;

    for (const auto& c : creds) {
        if (!c.ssid || c.ssid[0] == '\0') {
            continue;
        }

        Serial.printf("\n[OTA] Подключение к %s: %s", c.label, c.ssid);
        WiFi.begin(c.ssid, c.pass);

        while (WiFi.status() != WL_CONNECTED && millis() < deadline) {
            delay(250);
            Serial.print(".");
        }

        if (WiFi.status() == WL_CONNECTED) {
            g_wifiOwnedByOta = true;
            Serial.printf("\n[OTA] WiFi подключен: %s, IP: %s", c.ssid, WiFi.localIP().toString().c_str());
            return true;
        }

        WiFi.disconnect(true);
        delay(50);
    }

    return false;
}

void setupCallbacks() {
    ArduinoOTA.onStart([]() {
        g_otaBusy = true;
        const char* type = (ArduinoOTA.getCommand() == U_FLASH) ? "прошивка" : "файловая система";
        Serial.printf("\n[OTA] START: %s", type);
    });

    ArduinoOTA.onEnd([]() {
        g_otaBusy = false;
        Serial.print("\n[OTA] END");
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        static unsigned int lastPercent = 0;
        unsigned int percent = (total > 0) ? (progress * 100U / total) : 0;
        if (percent >= lastPercent + 10 || percent == 100) {
            lastPercent = percent;
            Serial.printf("\n[OTA] Progress: %u%%", percent);
        }
    });

    ArduinoOTA.onError([](ota_error_t error) {
        g_otaBusy = false;
        Serial.printf("\n[OTA] ERROR[%u]", static_cast<unsigned>(error));
        if (error == OTA_AUTH_ERROR) Serial.print(" auth");
        if (error == OTA_BEGIN_ERROR) Serial.print(" begin");
        if (error == OTA_CONNECT_ERROR) Serial.print(" connect");
        if (error == OTA_RECEIVE_ERROR) Serial.print(" receive");
        if (error == OTA_END_ERROR) Serial.print(" end");
    });
}
}

void otaInit() {
    // Инициализация выполняется при otaEnable(), т.к. нужен активный WiFi.
}

bool otaEnable(uint32_t windowMs) {
    if (isSyncInProgress()) {
        Serial.print("\n[OTA] Сначала дождитесь окончания sync");
        return false;
    }

    g_bleWasEnabledBeforeOta = bleTerminalIsEnabled();
    if (g_bleWasEnabledBeforeOta) {
        Serial.print("\n[OTA] Временно выключаю BLE для стабильного OTA");
        bleTerminalDisable();
        delay(120);
    }

    if (!connectWifiForOta()) {
        Serial.print("\n[OTA] WiFi недоступен, OTA не запущен");
        if (g_bleWasEnabledBeforeOta) {
            bleTerminalEnable();
            g_bleWasEnabledBeforeOta = false;
        }
        return false;
    }

    // Безопасно формируем hostname (на случай битого/не-terminated serial_number)
    char serialSafe[sizeof(config.serial_number) + 1] = {0};
    memcpy(serialSafe, config.serial_number, sizeof(config.serial_number));
    serialSafe[sizeof(config.serial_number)] = '\0';
    if (serialSafe[0] == '\0') {
        strlcpy(serialSafe, "unknown", sizeof(serialSafe));
    }

    String host = String("nixie-") + serialSafe;
    host.replace(" ", "-");

    ArduinoOTA.setHostname(host.c_str());
    ArduinoOTA.setPassword(OTA_PASSWORD);
    setupCallbacks();
    ArduinoOTA.begin();

    g_otaEnabled = true;
    g_windowDeadlineMs = millis() + windowMs;

    Serial.printf("\n[OTA] READY: %s.local (%s)", host.c_str(), WiFi.localIP().toString().c_str());
    Serial.printf("\n[OTA] Окно обновления: %lu сек", static_cast<unsigned long>(windowMs / 1000UL));
    return true;
}

void otaDisable() {
    if (!g_otaEnabled) {
        return;
    }

    g_otaEnabled = false;
    g_otaBusy = false;
    g_windowDeadlineMs = 0;

    if (g_wifiOwnedByOta) {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        g_wifiOwnedByOta = false;
    }

    if (g_bleWasEnabledBeforeOta) {
        bleTerminalEnable();
        g_bleWasEnabledBeforeOta = false;
    }

    Serial.print("\n[OTA] OFF");
}

void otaProcess() {
    if (!g_otaEnabled) {
        return;
    }

    if (WiFi.status() != WL_CONNECTED) {
        Serial.print("\n[OTA] WiFi потерян, OTA выключен");
        otaDisable();
        return;
    }

    ArduinoOTA.handle();

    if (!g_otaBusy && g_windowDeadlineMs > 0 && millis() >= g_windowDeadlineMs) {
        Serial.print("\n[OTA] Окно обслуживания закрыто");
        otaDisable();
    }
}

bool otaIsEnabled() {
    return g_otaEnabled;
}

bool otaIsBusy() {
    return g_otaBusy;
}

uint32_t otaSecondsLeft() {
    if (!g_otaEnabled || g_windowDeadlineMs == 0) {
        return 0;
    }

    unsigned long now = millis();
    if (now >= g_windowDeadlineMs) {
        return 0;
    }

    return static_cast<uint32_t>((g_windowDeadlineMs - now) / 1000UL);
}
