#include "menu_manager.h"

#include "config.h"
#include "hardware.h"
#include "platform_profile.h"
#include "time_utils.h"

#include <Arduino.h>
#include <WiFi.h>

namespace {

static bool isAlarmFeatureEnabled() {
    return platformGetCapabilities().alarm_enabled;
}

static bool isNixClockForUserMenu() {
    return (config.clock_type == CLOCK_TYPE_NIXIE || config.clock_type == CLOCK_TYPE_NIXIE_HAND);
}

// Полуинтервал [start, end):
// 0-24 = весь день; 0-23 = до 22:59 включительно; 23-6 = через полночь.
static bool parseHourRange(const String &raw, uint8_t &startHour, uint8_t &endHour) {
    String s = raw;
    s.trim();
    int dash = s.indexOf('-');
    if (dash <= 0 || dash >= static_cast<int>(s.length()) - 1) {
        return false;
    }

    String a = s.substring(0, dash);
    String b = s.substring(dash + 1);
    a.trim();
    b.trim();
    if (a.length() == 0 || b.length() == 0) return false;

    for (size_t i = 0; i < a.length(); ++i) if (!isDigit(a[i])) return false;
    for (size_t i = 0; i < b.length(); ++i) if (!isDigit(b[i])) return false;

    int sa = a.toInt();
    int sb = b.toInt();
    if (sa < 0 || sa > 24 || sb < 0 || sb > 24) return false;

    startHour = static_cast<uint8_t>(sa);
    endHour = static_cast<uint8_t>(sb);
    return true;
}

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

// ======================= МЕНЮ ЗВУКА И ДИСПЛЕЯ (уровень 2) =======================

void printDisplayMenu() {
    const bool nixClock = isNixClockForUserMenu();
    const bool soundEnabled = isAlarmFeatureEnabled();

    Serial.println("\n=== ЗВУК И ДИСПЛЕЙ ===");
    Serial.print("\n╔════════════════════════════════════════════════════");
    Serial.print("\n║                  ТЕКУЩИЕ НАСТРОЙКИ");
    Serial.print("\n╠════════════════════════════════════════════════════");
    if (soundEnabled) {
        Serial.printf("\n║ Громкость будильника: %3u%%", static_cast<unsigned>(config.alarm_volume));
        Serial.printf("\n║ Громкость боя:        %3u%%", static_cast<unsigned>(config.chime_volume));
        Serial.printf("\n║ Бой в час:            %u", static_cast<unsigned>(config.chimes_per_hour));
        Serial.printf("\n║ Активность боя:       %u-%u", static_cast<unsigned>(config.chime_active_start_hour), static_cast<unsigned>(config.chime_active_end_hour));
    } else {
        Serial.print("\n║ Звуковая подсистема отключена в инженерном меню");
    }
    Serial.print("\n╠══════════════════════════════");
    if (nixClock) {
        Serial.printf("\n║ Активность дисплея:   %u-%u", static_cast<unsigned>(config.display_active_start_hour), static_cast<unsigned>(config.display_active_end_hour));
        Serial.printf("\n║ Автояркость:          %s", config.brightness_control_enabled ? "ВКЛ" : "ВЫКЛ");
        Serial.printf("\n║ Порог max яркости:    %u", static_cast<unsigned>(config.brightness_sensor_max));
        Serial.printf("\n║ Порог min яркости:    %u", static_cast<unsigned>(config.brightness_sensor_min));
        Serial.printf("\n║ Фильтр датчика света: samples=%u, adc=%u-bit",
                      static_cast<unsigned>(config.light_filter_samples),
                      static_cast<unsigned>(config.light_sensor_resolution_bits));
    } else {
        Serial.print("\n║ Параметры дисплея недоступны для данного типа часов");
    }
    Serial.print("\n╚════════════════════════════════════════════════════\n");
    if (soundEnabled) {
        Serial.println("\nНастройка громкости:");
        Serial.println("  set alarm volume / sav 0...100      - Уровень громкости будильника");
        Serial.println("  set bell volume / sbv 0...100       - Уровень громкости боя");

        Serial.println("\nНастройка боя:");
        Serial.println("  bells per hour / bph 0|1|2|4        - 4=четвертной, 2=половинный, 1=часовой, 0=выкл");
        Serial.println("  bells time activity / bta HH-HH     - Активность боя (полуинтервал [start,end))");
    } else {
        Serial.println("\nЗвуковая подсистема отключена в инженерном меню. Команды настройки звука недоступны.");
    }
    if (nixClock) {
        Serial.println("\nНастройки дисплея:");
        Serial.println("  display activity hours / dah HH-HH  - Активность дисплея (полуинтервал [start,end))");
        Serial.println("  brightness control on/off / bc1/bc0 - Вкл/выкл управление яркостью");
        Serial.println("  max brightness learning / mbe       - Обучение порога max яркости");
        Serial.println("  smallest brightness learning / sbe  - Обучение порога min яркости");
    }
        else {
        Serial.println("\nПараметры дисплея недоступны для данного типа часов.");
    }

    printMappingMenuCommands();
}

void handleDisplayMenu(String command) {
    if (handleCommonMenuCommands(command, printDisplayMenu)) return;

    String cmd = command;
    cmd.trim();
    String lower = cmd;
    lower.toLowerCase();

    if (lower.startsWith("set alarm volume ") || lower.startsWith("sav ")) {
        String arg = lower.startsWith("sav ") ? lower.substring(4) : lower.substring(17);
        arg.trim();
        int v = arg.toInt();
        if (arg.length() == 0 || v < 0 || v > 100) {
            Serial.println("Неверное значение. Используйте 0...100");
        } else {
            config.alarm_volume = static_cast<uint8_t>(v);
            saveConfig();
            Serial.printf("Громкость будильника: %u%%\n", static_cast<unsigned>(config.alarm_volume));
        }
        printDisplayMenu();
        return;
    }

    if (lower.startsWith("set bell volume ") ||
        lower.startsWith("sbv ")) {
        String arg;
        if (lower.startsWith("sbv ")) {
            arg = lower.substring(4);
        } else {
            arg = lower.substring(16);
        }
        arg.trim();
        int v = arg.toInt();
        if (arg.length() == 0 || v < 0 || v > 100) {
            Serial.println("Неверное значение. Используйте 0...100");
        } else {
            config.chime_volume = static_cast<uint8_t>(v);
            saveConfig();
            Serial.printf("Громкость боя: %u%%\n", static_cast<unsigned>(config.chime_volume));
        }
        printDisplayMenu();
        return;
    }

    if (lower.startsWith("bells per hour ") ||
        lower.startsWith("bph ")) {
        String arg;
        if (lower.startsWith("bph ")) {
            arg = lower.substring(4);
        } else {
            arg = lower.substring(15);
        }
        arg.trim();
        int v = arg.toInt();
        if (!(v == 0 || v == 1 || v == 2 || v == 4)) {
            Serial.println("Неверное значение. Допустимо только: 0, 1, 2 или 4");
        } else {
            config.chimes_per_hour = static_cast<uint8_t>(v);
            saveConfig();
            Serial.printf("Бой в час установлен: %u\n", static_cast<unsigned>(config.chimes_per_hour));
        }
        printDisplayMenu();
        return;
    }

    if (lower.startsWith("bells time activity ") ||
        lower.startsWith("bta ")) {
        String arg;
        if (lower.startsWith("bta ")) {
            arg = lower.substring(4);
        } else {
            arg = lower.substring(20);
        }
        arg.trim();
        uint8_t sh = 0, eh = 24;
        if (!parseHourRange(arg, sh, eh)) {
            Serial.println("Неверный формат. Используйте HH-HH, например: 0-24 или 8-23");
        } else {
            config.chime_active_start_hour = sh;
            config.chime_active_end_hour = eh;
            saveConfig();
            Serial.printf("Активность боя: %u-%u\n", static_cast<unsigned>(sh), static_cast<unsigned>(eh));
        }
        printDisplayMenu();
        return;
    }

    if (isNixClockForUserMenu()) {
        if (lower.equals("bc1") || lower.equals("brightness control on")) {
            config.brightness_control_enabled = true;
            saveConfig();
            Serial.println("Управление яркостью: ВКЛЮЧЕНО");
            printDisplayMenu();
            return;
        }

        if (lower.equals("bc0") || lower.equals("brightness control off")) {
            config.brightness_control_enabled = false;
            saveConfig();
            Serial.println("Управление яркостью: ОТКЛЮЧЕНО");
            printDisplayMenu();
            return;
        }

        if (lower.equals("mbe") || lower.equals("max brightness learning")) {
            uint16_t sensor = readLightSensorFiltered(config.light_filter_samples, config.light_sensor_resolution_bits);
            config.brightness_sensor_max = sensor;
            if (config.brightness_sensor_min >= config.brightness_sensor_max) {
                config.brightness_sensor_min = (config.brightness_sensor_max > 10) ? (config.brightness_sensor_max - 10) : 0;
            }
            saveConfig();
            Serial.printf("Порог max яркости сохранён: %u\n", static_cast<unsigned>(config.brightness_sensor_max));
            printDisplayMenu();
            return;
        }

        if (lower.equals("sbe") || lower.equals("smallest brightness learning")) {
            uint16_t sensor = readLightSensorFiltered(config.light_filter_samples, config.light_sensor_resolution_bits);
            if (sensor >= config.brightness_sensor_max) {
                Serial.printf("Ошибка: значение %u не меньше порога max (%u). Повторите процедуру.\n",
                              static_cast<unsigned>(sensor),
                              static_cast<unsigned>(config.brightness_sensor_max));
            } else {
                config.brightness_sensor_min = sensor;
                saveConfig();
                Serial.printf("Порог min яркости сохранён: %u\n", static_cast<unsigned>(config.brightness_sensor_min));
            }
            printDisplayMenu();
            return;
        }

        if (lower.startsWith("display activity hours ") || lower.startsWith("dah ")) {
            String arg = lower.startsWith("dah ") ? lower.substring(4) : lower.substring(23);
            arg.trim();
            uint8_t sh = 0, eh = 24;
            if (!parseHourRange(arg, sh, eh)) {
                Serial.println("Неверный формат. Используйте HH-HH, например: 0-24 или 8-23");
            } else {
                config.display_active_start_hour = sh;
                config.display_active_end_hour = eh;
                saveConfig();
                Serial.printf("Активность дисплея: %u-%u\n", static_cast<unsigned>(sh), static_cast<unsigned>(eh));
            }
            printDisplayMenu();
            return;
        }
    }

    Serial.println("Неизвестная команда. Введите 'help' для справки");
    printDisplayMenu();
}

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

    printMappingMenuCommands();  //Управление меню
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
