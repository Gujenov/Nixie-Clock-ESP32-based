#include "ota_manager.h"

#include "config.h"
#include "time_utils.h"
#include "ble_terminal.h"

#include <WiFi.h>
#include <ArduinoOTA.h>

namespace {
volatile bool g_otaBusy = false;
bool g_otaEnabled = false;
bool g_wifiOwnedByOta = false;
unsigned long g_windowDeadlineMs = 0;
bool g_bleWasEnabledBeforeOta = false;
bool g_otaDisableInProgress = false;
unsigned long g_lastProgressMs = 0;
unsigned int g_lastProgressPercent = 0;
void (*g_otaTransferStartCallback)() = nullptr;

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
        {config.wifi_ssid, config.wifi_pass, "1"},
        {config.wifi_ssid_2, config.wifi_pass_2, "2"}
    };

    const uint32_t deadline = millis() + OTA_CONNECT_TIMEOUT_MS;

    for (const auto& c : creds) {
        if (!c.ssid || c.ssid[0] == '\0') {
            continue;
        }

        Serial.printf("\n[OTA] -> [WiFi] Подключение к сети %s: %s", c.label, c.ssid);
        WiFi.begin(c.ssid, c.pass);

        while (WiFi.status() != WL_CONNECTED && millis() < deadline) {
            delay(250);
            Serial.print(".");
        }

        if (WiFi.status() == WL_CONNECTED) {
            g_wifiOwnedByOta = true;
            Serial.printf("\n[OTA] -> [WiFi] Подключено к: %s", c.ssid);
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
        g_lastProgressMs = millis();
        g_lastProgressPercent = 0;
        const char* type = (ArduinoOTA.getCommand() == U_FLASH) ? "прошивка" : "файловая система";
        Serial.printf("\n[OTA] START: %s", type);
        if (g_otaTransferStartCallback) {
            g_otaTransferStartCallback();
        }
    });

    ArduinoOTA.onEnd([]() {
        g_otaBusy = false;
        Serial.print("\n[OTA] END");
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        unsigned int percent = (total > 0) ? (progress * 100U / total) : 0;
        g_lastProgressMs = millis();
        if (percent >= g_lastProgressPercent + 5 || percent == 100) {
            g_lastProgressPercent = percent;
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

void otaSetTransferStartCallback(void (*callback)()) {
    g_otaTransferStartCallback = callback;
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

    constexpr const char* kOtaHostname = "Clockio-OTA";
    constexpr uint16_t kOtaPort = 3232;
    ArduinoOTA.setHostname(kOtaHostname);
    ArduinoOTA.setPort(kOtaPort);
    ArduinoOTA.setPassword(OTA_PASSWORD);
    setupCallbacks();
    ArduinoOTA.begin();

    g_otaEnabled = true;
    g_windowDeadlineMs = millis() + windowMs;
    g_lastProgressMs = millis();
    g_lastProgressPercent = 0;

    Serial.printf("\n[OTA] READY: %s IP: %s", kOtaHostname, WiFi.localIP().toString().c_str());
    Serial.printf("\n[OTA] Port: %u", static_cast<unsigned>(kOtaPort));
    Serial.printf("\n[OTA] Password: %s", OTA_PASSWORD);
    Serial.printf("\n[OTA] Окно обновления: %lu сек", static_cast<unsigned long>(windowMs / 1000UL));
    return true;
}

void otaDisable() {
    if (!g_otaEnabled) {
        return;
    }

    if (g_otaDisableInProgress) {
        return;
    }
    g_otaDisableInProgress = true;

    // Во время активной передачи не форсируем stop, чтобы не повиснуть в сетевом стеке.
    if (g_otaBusy) {
        Serial.print("\n[OTA] Нельзя выключить во время передачи");
        g_otaDisableInProgress = false;
        return;
    }

    g_otaEnabled = false;
    g_otaBusy = false;
    g_windowDeadlineMs = 0;

    if (g_wifiOwnedByOta) {
        // Безопасный shutdown WiFi без принудительного WIFI_OFF (избегаем зависаний на некоторых S3)
        WiFi.disconnect(false, false);

        const unsigned long t0 = millis();
        while (WiFi.status() == WL_CONNECTED && (millis() - t0) < 1200UL) {
            delay(20);
        }

        if (WiFi.status() == WL_CONNECTED) {
            Serial.print("\n[OTA] WiFi оставлен подключенным (safe-off)");
        } else {
            Serial.print("\n[OTA] WiFi отключен");
        }

        g_wifiOwnedByOta = false;
    }

    if (g_bleWasEnabledBeforeOta) {
        // На некоторых ESP32-S3 повторная BLE init/deinit после OTA вызывает зависания.
        // Оставляем BLE выключенным и предлагаем включить вручную командой bon.
        Serial.print("\n[OTA] BLE оставлен выключенным (включите вручную: bon)");
        g_bleWasEnabledBeforeOta = false;
    }

    Serial.print("\n[OTA] OFF");
    g_otaDisableInProgress = false;
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

    const unsigned long now = millis();
    if (g_otaBusy && (now - g_lastProgressMs) >= 5000UL) {
        Serial.printf("\n[OTA] WARN: нет прогресса %lu c (последний %u%%)",
                      static_cast<unsigned long>((now - g_lastProgressMs) / 1000UL),
                      g_lastProgressPercent);
        g_lastProgressMs = now;
    }

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
