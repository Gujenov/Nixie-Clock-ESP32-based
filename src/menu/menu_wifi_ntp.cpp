#include "menu_manager.h"

#include "config.h"
#include "time_utils.h"

#include <Arduino.h>
#include <WiFi.h>

namespace {

static volatile bool wifiScanInProgress = false;

static void wifiScanTask(void* param) {
    (void)param;

    Serial.println("\n[WiFi] Сканирование доступных WiFi сетей...");
    const int n = WiFi.scanNetworks(false, true);
    if (n <= 0) {
        Serial.println("[WiFi] Нет доступных сетей");
    } else {
        Serial.printf("[WiFi] Найдено %d сетей:\n", n);
        for (int i = 0; i < n; ++i) {
            Serial.printf("%d: %s (RSSI: %d dBm) %s\n",
                          i + 1,
                          WiFi.SSID(i).c_str(),
                          WiFi.RSSI(i),
                          (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "Open" : "");
        }
    }

    WiFi.scanDelete();
    wifiScanInProgress = false;
    vTaskDelete(NULL);
}

} // namespace

// ======================= МЕНЮ WIFI/NTP (уровень 2) =======================

void printWifiMenu() {
    Serial.println("\n=== WI-FI И NTP ===");

    Serial.println("\nТекущие параметры:");
    Serial.printf("  WiFi SSID (сеть 1): %s\n", strlen(config.wifi_ssid) > 0 ? config.wifi_ssid : "(не установлено)");
    Serial.printf("  WiFi SSID (сеть 2, резервная): %s\n", strlen(config.wifi_ssid_2) > 0 ? config.wifi_ssid_2 : "(не установлено)");
    Serial.printf("  NTP сервер 1: %s\n", config.ntp_server_1);
    Serial.printf("  NTP сервер 2: %s\n", config.ntp_server_2);
    Serial.printf("  NTP сервер 3: %s\n", config.ntp_server_3);
    if (config.time_config.last_ntp_sync) {
        time_t t = (time_t)config.time_config.last_ntp_sync;
        struct tm tm;
        localtime_r(&t, &tm);
        Serial.printf("  Время последней синхронизации: %02d:%02d\n", tm.tm_hour, tm.tm_min);
    } else {
        Serial.println("  Время последней синхронизации: (нет данных)");
    }

    Serial.println("\nКоманды:");
    Serial.println("  wifi scan     - Сканировать доступные сети");
    Serial.println("  wifi [SSID] [PASSWORD] - Данные основной сети WIFI (сеть 1)");
    Serial.println("  wifi2 [SSID] [PASSWORD] - Данные резервной сети WIFI (сеть 2)");
    Serial.println("  set ntp1 <SERVER> - Задать NTP сервер 1");
    Serial.println("  set ntp2 <SERVER> - Задать NTP сервер 2");
    Serial.println("  set ntp3 <SERVER> - Задать NTP сервер 3");

    printMappingMenuCommands();  // Управление меню
}

void handleWifiMenu(String command) {
    if (handleCommonMenuCommands(command, printWifiMenu)) return;
    else if (command.equals("wifi scan")) {
        if (wifiScanInProgress) {
            Serial.println("[WiFi] Сканирование уже выполняется...");
        } else {
            wifiScanInProgress = true;
            BaseType_t result = xTaskCreatePinnedToCore(
                wifiScanTask,
                "wifi_scan",
                8192,
                NULL,
                1,
                NULL,
                0
            );

            if (result != pdPASS) {
                wifiScanInProgress = false;
                Serial.println("[WiFi] Ошибка запуска сканирования");
            } else {
                Serial.println("[WiFi] Сканирование запущено в фоне");
            }
        }
    }
    else if (command.startsWith("wifi ")) {
        String args = command.substring(5);
        int spaceIdx = args.indexOf(' ');
        if (spaceIdx == -1) {
            Serial.println("Нужно указать SSID и пароль");
        }
        else {
            String ssid = args.substring(0, spaceIdx);
            String password = args.substring(spaceIdx + 1);
            ssid.trim();
            password.trim();
            strncpy(config.wifi_ssid, ssid.c_str(), sizeof(config.wifi_ssid) - 1);
            strncpy(config.wifi_pass, password.c_str(), sizeof(config.wifi_pass) - 1);
            config.wifi_ssid[sizeof(config.wifi_ssid) - 1] = '\0';
            config.wifi_pass[sizeof(config.wifi_pass) - 1] = '\0';
            Serial.printf("Данные для подключения к WiFi (сеть 1) установлены: SSID='%s'\n", config.wifi_ssid);
            saveConfig();
            Serial.println("WiFi настройки сохранены во flash");
        }
    }
    else if (command.startsWith("wifi2 ")) {
        String args = command.substring(6);
        int spaceIdx = args.indexOf(' ');
        if (spaceIdx == -1) {
            Serial.println("Нужно указать SSID и пароль");
        }
        else {
            String ssid = args.substring(0, spaceIdx);
            String password = args.substring(spaceIdx + 1);
            ssid.trim();
            password.trim();
            strncpy(config.wifi_ssid_2, ssid.c_str(), sizeof(config.wifi_ssid_2) - 1);
            strncpy(config.wifi_pass_2, password.c_str(), sizeof(config.wifi_pass_2) - 1);
            config.wifi_ssid_2[sizeof(config.wifi_ssid_2) - 1] = '\0';
            config.wifi_pass_2[sizeof(config.wifi_pass_2) - 1] = '\0';
            Serial.printf("Данные для подключения к WiFi (сеть 2, резервная) установлены: SSID='%s'\n", config.wifi_ssid_2);
            saveConfig();
            Serial.println("WiFi настройки сохранены во flash");
        }
    }
    else if (command.startsWith("set ntp1 ") || command.startsWith("set ntp2 ") || command.startsWith("set ntp3 ") || command.startsWith("set ntp ")) {
        uint8_t index = 1;
        String server;

        if (command.startsWith("set ntp1 ")) {
            index = 1;
            server = command.substring(9);
        } else if (command.startsWith("set ntp2 ")) {
            index = 2;
            server = command.substring(9);
        } else if (command.startsWith("set ntp3 ")) {
            index = 3;
            server = command.substring(9);
        } else {
            index = 1;
            server = command.substring(8);
        }

        server.trim();
        updateNTPServer(index, server.c_str());

        // Если WiFi ещё не настроен, уведомляем и не пытаемся синхронизировать
        if (strlen(config.wifi_ssid) == 0) {
            Serial.println("\nWiFi не настроен, синхронизация NTP невозможна (установите WiFi)");
        } else {
            Serial.print("\nПытаюсь синхронизироваться с новым NTP сервером...");
            syncTimeAsync(true, index);
            Serial.println("\nСинхронизация NTP запущена, результат будет в логе\n");
        }
    }
    else {
        Serial.println("Неизвестная команда. Введите 'help' для справки");
    }
}
