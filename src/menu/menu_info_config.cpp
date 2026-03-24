#include "menu_manager.h"

#include "config.h"
#include "hardware.h"
#include "platform_profile.h"
#include "runtime_counter.h"
#include "time_utils.h"

#include <Arduino.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

namespace {

static bool isAlarmFeatureEnabled() {
    return platformGetCapabilities().alarm_enabled;
}

static bool isNixClockForUserMenu() {
    return (config.clock_type == CLOCK_TYPE_NIXIE || config.clock_type == CLOCK_TYPE_NIXIE_HAND);
}

static const char* getClockTypeLabelForInfo() {
    switch (config.clock_type) {
        case CLOCK_TYPE_NIXIE:
            switch (config.clock_digits) {
                case 1: return "Nix 1";
                case 2: return "Nix 2";
                case 4: return "Nix 4";
                case 6: return "Nix 6";
                default: return "Nix (unknown digits)";
            }
        case CLOCK_TYPE_NIXIE_HAND: return "Nix hand";
        case CLOCK_TYPE_CYCLOTRON: return "Cycl";
        case CLOCK_TYPE_VERTICAL: return "Vert";
        case CLOCK_TYPE_MECH_2: return "Mech 2";
        case CLOCK_TYPE_MECH_PEND: return "Mech pend";
        default: return "Unknown";
    }
}

static const char* getNix6OutputModeLabelForInfo() {
    return (config.nix6_output_mode == NIX6_OUTPUT_REVERSE_INVERT)
        ? "Reverse order + reverse bits in nibble"
        : "Standard order, direct bits";
}

static bool configDefaultConfirmPending = false;

static void formatDisplayActivityIntervals(uint8_t start1,
                                           uint8_t end1,
                                           uint8_t start2,
                                           uint8_t end2,
                                           char* out,
                                           size_t outSize) {
    if (!out || outSize == 0) {
        return;
    }

    struct Interval {
        uint8_t start;
        uint8_t end;
    };

    Interval intervals[2] = {
        {start1, end1},
        {start2, end2}
    };

    Interval normalized[2];
    uint8_t count = 0;
    for (uint8_t i = 0; i < 2; ++i) {
        const uint8_t s = intervals[i].start;
        const uint8_t e = intervals[i].end;
        if (s > 24 || e > 24 || s == e) {
            continue;
        }
        normalized[count++] = {s, e};
    }

    if (count == 0) {
        strlcpy(out, "нет активных интервалов", outSize);
        return;
    }

    if (count == 2 && normalized[1].start < normalized[0].start) {
        const Interval tmp = normalized[0];
        normalized[0] = normalized[1];
        normalized[1] = tmp;
    }

    Interval merged[2];
    uint8_t mergedCount = 0;
    for (uint8_t i = 0; i < count; ++i) {
        if (mergedCount == 0) {
            merged[mergedCount++] = normalized[i];
            continue;
        }

        Interval& last = merged[mergedCount - 1];
        if (normalized[i].start <= last.end) {
            if (normalized[i].end > last.end) {
                last.end = normalized[i].end;
            }
        } else {
            merged[mergedCount++] = normalized[i];
        }
    }

    if (mergedCount == 1) {
        snprintf(out, outSize, "%u-%u",
                 static_cast<unsigned>(merged[0].start),
                 static_cast<unsigned>(merged[0].end));
        return;
    }

    snprintf(out, outSize, "%u-%u, %u-%u",
             static_cast<unsigned>(merged[0].start),
             static_cast<unsigned>(merged[0].end),
             static_cast<unsigned>(merged[1].start),
             static_cast<unsigned>(merged[1].end));
}

} // namespace

// ======================= МЕНЮ ИНФОРМАЦИИ (уровень 2) =======================

void printInfoMenu() {
    auto formatYmd = [](uint32_t ymd, char* out, size_t outSize) {
        if (ymd == 0) {
            strlcpy(out, "не задана", outSize);
            return;
        }
        uint32_t y = ymd / 10000;
        uint32_t m = (ymd / 100) % 100;
        uint32_t d = ymd % 100;
        snprintf(out, outSize, "%02lu.%02lu.%04lu",
                 static_cast<unsigned long>(d),
                 static_cast<unsigned long>(m),
                 static_cast<unsigned long>(y));
    };
    auto formatHm = [](uint64_t totalSeconds, char* out, size_t outSize) {
        const uint64_t hours = totalSeconds / 3600ULL;
        const uint64_t minutes = (totalSeconds % 3600ULL) / 60ULL;
        snprintf(out, outSize, "%llu:%02llu",
                 static_cast<unsigned long long>(hours),
                 static_cast<unsigned long long>(minutes));
    };

    Serial.println("\n=== Системная информация ===\n");
    Serial.printf("Версия ПО: %s\n", FIRMWARE_VERSION);
    Serial.printf("Серийный номер устройства: %s\n", config.serial_number);

    char lastServiceDate[24];
    char totalHm[32];
    char serviceHm[32];
    formatYmd(runtimeCounterGetLastServiceDate(), lastServiceDate, sizeof(lastServiceDate));
    formatHm(runtimeCounterGetTotalRunSeconds(), totalHm, sizeof(totalHm));
    formatHm(runtimeCounterGetLastServiceRunSeconds(), serviceHm, sizeof(serviceHm));
    Serial.printf("\nКоличество включений: %lu\n", static_cast<unsigned long>(runtimeCounterGetBootCount()));
    Serial.printf("Моточасы: %s (ЧЧ:ММ)\n", totalHm);
    Serial.printf("Дата последнего сервиса: %s\n", lastServiceDate);
    Serial.printf("Кол-во моточасов при посл. сервисе: %s (ЧЧ:ММ)\n", serviceHm);

    Serial.printf("\nТип часов: %s\n", getClockTypeLabelForInfo());
    if (config.clock_type == CLOCK_TYPE_NIXIE && config.clock_digits == 6) {
        Serial.printf("Режим вывода Nix 6: %s\n", getNix6OutputModeLabelForInfo());
    }
    Serial.printf("Аудио/будильник: %s\n", config.audio_module_enabled ? "Есть" : "Нет");
    Serial.printf("Ручное управление: %s\n", platformUiControlModeName(config.ui_control_mode));
    Serial.printf("Источник времени: %s",
                  currentTimeSource == EXTERNAL_DS3231 ? "DS3231" : "Внутренний RTC");
    printDS3231Temperature();

    Serial.printf("\nWiFi SSID: %s\n", config.wifi_ssid);
    Serial.printf("NTP сервер 1: %s\n", config.ntp_server_1);
    Serial.printf("NTP сервер 2: %s\n", config.ntp_server_2);
    Serial.printf("NTP сервер 3: %s\n", config.ntp_server_3);

    Serial.println("\n\n=== Информация о ESP32 ===");

    String chipModel = ESP.getChipModel();
    Serial.printf("\nESP-ROM: %s", chipModel.c_str());
    Serial.printf("\nModule: %s", "ESP32-S3-WROOM-1");
    Serial.printf("\nCPU Частота: %d MHz\n", ESP.getCpuFreqMHz());
    float cpuTemp = getESP32Temperature();
    if (cpuTemp > -100.0f) {
        Serial.printf("CPU Температура: %.1f°C\n", cpuTemp);
    } else {
        Serial.print("CPU Температура: недоступна\n");
    }

    Serial.printf("IDF версия: %s\n", esp_get_idf_version());
    Serial.printf("Cores: %d\n", ESP.getChipCores());
    Serial.printf("Revision: %d\n", ESP.getChipRevision());

    uint32_t flashSize = ESP.getFlashChipSize();
    uint32_t sketchSize = ESP.getSketchSize();
    const esp_partition_t* runningPart = esp_ota_get_running_partition();
    const esp_partition_t* nextOtaPart = esp_ota_get_next_update_partition(nullptr);

    uint32_t appVolume = runningPart ? static_cast<uint32_t>(runningPart->size) : (sketchSize + ESP.getFreeSketchSpace());
    uint32_t otaVolume = nextOtaPart ? static_cast<uint32_t>(nextOtaPart->size) : 0;
    uint32_t appFree = (appVolume > sketchSize) ? (appVolume - sketchSize) : 0;

    uint32_t totalAppPartitions = 0;
    uint32_t totalDataPartitions = 0;
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, nullptr);
    while (it != nullptr) {
        const esp_partition_t* p = esp_partition_get(it);
        if (p) totalAppPartitions += static_cast<uint32_t>(p->size);
        it = esp_partition_next(it);
    }
    esp_partition_iterator_release(it);

    it = esp_partition_find(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, nullptr);
    while (it != nullptr) {
        const esp_partition_t* p = esp_partition_get(it);
        if (p) totalDataPartitions += static_cast<uint32_t>(p->size);
        it = esp_partition_next(it);
    }
    esp_partition_iterator_release(it);

    Serial.printf("\nFlash Size: %lu MB\n", static_cast<unsigned long>(flashSize / (1024 * 1024)));
    Serial.printf("OTA volume: %.2f MB\n", otaVolume / (1024.0 * 1024.0));
    Serial.printf("Application volume: %.2f MB\n", appVolume / (1024.0 * 1024.0));
    Serial.printf("Sketch size: %.2f MB\n", sketchSize / (1024.0 * 1024.0));
    Serial.printf("App free space: %.2f MB\n", appFree / (1024.0 * 1024.0));

    float appUsage = (appVolume > 0) ? ((sketchSize * 100.0f) / appVolume) : 0.0f;
    Serial.printf("App usage: %.1f%%\n", appUsage);
    Serial.printf("Total APP partitions: %.2f MB\n", totalAppPartitions / (1024.0 * 1024.0));
    Serial.printf("Total DATA partitions: %.2f MB\n", totalDataPartitions / (1024.0 * 1024.0));

    printMappingMenuCommands();
}

void handleInfoMenu(String command) {
    command.trim();
    String cmdLower = command;
    cmdLower.toLowerCase();

    if (handleCommonMenuCommands(cmdLower, printInfoMenu)) return;
    else {
        Serial.println("Неизвестная команда. Введите 'help' для справки");
    }
}

// ======================= МЕНЮ КОНФИГУРАЦИИ (уровень 2) =======================

void printConfigMenu() {
    Serial.println("\n=== ТЕКУЩАЯ КОНФИГУРАЦИЯ ===");
    auto formatDays = [](uint8_t mask) {
        if (mask == 0) return String("нет");
        const char* names[7] = {"Пн", "Вт", "Ср", "Чт", "Пт", "Сб", "Вс"};
        String out;
        for (uint8_t i = 0; i < 7; ++i) {
            if (mask & (1 << i)) {
                out += names[i];
                if (out.length() > 1) out += ", ";
            }
        }
        return out;
    };

    Serial.println();
    Serial.printf("  • Сеть 1: %s\n", strlen(config.wifi_ssid) ? config.wifi_ssid : "(не установлена)");
    Serial.printf("  • Сеть 2: %s\n", strlen(config.wifi_ssid_2) ? config.wifi_ssid_2 : "(не установлена)");
    Serial.printf("  • NTP сервер 1: %s\n", strlen(config.ntp_server_1) ? config.ntp_server_1 : "(не установлен)");
    Serial.printf("  • NTP сервер 2: %s\n", strlen(config.ntp_server_2) ? config.ntp_server_2 : "(не установлен)");
    Serial.printf("  • NTP сервер 3: %s\n", strlen(config.ntp_server_3) ? config.ntp_server_3 : "(не установлен)");
    Serial.printf("  • Часовой пояс: %s\n", strlen(config.time_config.timezone_name) ? config.time_config.timezone_name : "(не установлен)");
    Serial.printf("  • Автосинхронизация по UTC: %s\n",
                  config.time_config.auto_sync_enabled ? "ВКЛЮЧЕНА" : "ОТКЛЮЧЕНА");
    if (config.time_config.automatic_localtime && config.time_config.auto_sync_enabled) {
        Serial.print("  • Локальное время: ИНТЕРНЕТ + проверка актуальности таблицы\n");
    } else if (config.time_config.automatic_localtime && !config.time_config.auto_sync_enabled) {
        Serial.print("  • Локальное время: ТАБЛИЦА (т.к. автосинхронизация отключена)\n");
    } else {
        Serial.print("  • Локальное время: ТАБЛИЦА / НОВОЕ ПРАВИЛО DST - если есть\n");
    }
    Serial.printf("  • Аудио/будильник: %s\n", config.audio_module_enabled ? "Есть" : "Нет");
    Serial.printf("  • Ручное управление: %s\n", platformUiControlModeName(config.ui_control_mode));
    Serial.printf("  • Громкость будильника: %u%%\n", static_cast<unsigned>(config.alarm_volume));
    Serial.printf("  • Громкость боя: %u%%\n", static_cast<unsigned>(config.chime_volume));
    Serial.printf("  • Бой в час: %u\n", static_cast<unsigned>(config.chimes_per_hour));
    Serial.printf("  • Активность боя: %u-%u\n",
                  static_cast<unsigned>(config.chime_active_start_hour),
                  static_cast<unsigned>(config.chime_active_end_hour));

    if (isNixClockForUserMenu()) {
        char displayIntervalsWorkdays[40] = {0};
        char displayIntervalsHolidays[40] = {0};
        formatDisplayActivityIntervals(config.display_active_start_hour,
                                       config.display_active_end_hour,
                                       config.display_active_start_hour_2,
                                       config.display_active_end_hour_2,
                                       displayIntervalsWorkdays,
                                       sizeof(displayIntervalsWorkdays));
        formatDisplayActivityIntervals(config.display_holiday_active_start_hour,
                                       config.display_holiday_active_end_hour,
                                       config.display_holiday_active_start_hour_2,
                                       config.display_holiday_active_end_hour_2,
                                       displayIntervalsHolidays,
                                       sizeof(displayIntervalsHolidays));
        Serial.printf("  • Автояркость: %s\n", config.brightness_control_enabled ? "ВКЛЮЧЕНА" : "ОТКЛЮЧЕНА");
        Serial.printf("  • Порог max яркости: %u\n", static_cast<unsigned>(config.brightness_sensor_max));
        Serial.printf("  • Порог min яркости: %u\n", static_cast<unsigned>(config.brightness_sensor_min));
        Serial.printf("  • Активность дисплея (будни): %s\n", displayIntervalsWorkdays);
        Serial.printf("  • Активность дисплея (выходные): %s\n", displayIntervalsHolidays);
        Serial.printf("  • Фильтр датчика освещения: samples=%u, adc=%u-bit\n",
                      static_cast<unsigned>(config.light_filter_samples),
                      static_cast<unsigned>(config.light_sensor_resolution_bits));
    }

    if (isAlarmFeatureEnabled()) {
        Serial.printf("  • Будильник 1: %s %02d:%02d, мелодия %d, %s\n",
                      config.alarm1.enabled ? "ВКЛ" : "ВЫКЛ",
                      config.alarm1.hour,
                      config.alarm1.minute,
                      config.alarm1.melody,
                      config.alarm1.once ? "once" : "daily");
        Serial.printf("  • Будильник 2: %s %02d:%02d, мелодия %d, дни: %s\n",
                      config.alarm2.enabled ? "ВКЛ" : "ВЫКЛ",
                      config.alarm2.hour,
                      config.alarm2.minute,
                      config.alarm2.melody,
                      formatDays(config.alarm2.days_mask).c_str());
    }

    Serial.println("\n   default  - Сбросить к настройкам по умолчанию");

    printMappingMenuCommands();
}

void handleConfigMenu(String command) {
    String cmd = command;
    cmd.trim();
    String lower = cmd;
    lower.toLowerCase();

    if (handleCommonMenuCommands(lower, printConfigMenu)) {
        configDefaultConfirmPending = false;
        return;
    }

    if (configDefaultConfirmPending) {
        if (lower.equals("y") || lower.equals("yes")) {
            setDefaultConfig();
            saveConfig();
            Serial.println("Конфигурация сброшена к настройкам по умолчанию.");
            configDefaultConfirmPending = false;
            printConfigMenu();
            return;
        }
        if (lower.equals("n") || lower.equals("no")) {
            Serial.println("Сброс отменён.");
            configDefaultConfirmPending = false;
            printConfigMenu();
            return;
        }

        Serial.println("Подтвердите действие: y/n");
        return;
    }

    if (lower.equals("default")) {
        configDefaultConfirmPending = true;
        Serial.println("Сбросить конфигурацию к настройкам по умолчанию? - y/n");
        return;
    }

    Serial.println("Неизвестная команда. Введите 'help' для справки");
}
