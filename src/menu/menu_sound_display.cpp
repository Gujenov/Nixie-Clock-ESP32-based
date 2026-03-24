#include "menu_manager.h"

#include "config.h"
#include "hardware.h"
#include "platform_profile.h"
#include "time_utils.h"

#include <Arduino.h>

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

static bool parseDisplayActivityTimesCommand(const String& raw,
                                             uint8_t& start1,
                                             uint8_t& end1,
                                             uint8_t& start2,
                                             uint8_t& end2) {
    String s = raw;
    s.trim();

    const int sep = s.indexOf(' ');
    if (sep <= 0 || sep >= static_cast<int>(s.length()) - 1) {
        return false;
    }

    String r1 = s.substring(0, sep);
    String r2 = s.substring(sep + 1);
    r1.trim();
    r2.trim();

    if (!parseHourRange(r1, start1, end1) || !parseHourRange(r2, start2, end2)) {
        return false;
    }

    // Первый интервал: утро/день (0..12), второй: день/вечер (12..24)
    if (start1 > 12 || end1 > 12) {
        return false;
    }
    if (start2 < 12 || end2 < 12) {
        return false;
    }

    // Для этой команды запрещаем перенос через границы внутри полуинтервалов.
    if (start1 > end1 || start2 > end2) {
        return false;
    }

    return true;
}

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

// ======================= МЕНЮ ЗВУКА И ДИСПЛЕЯ (уровень 2) =======================

void printDisplayMenu() {
    const bool nixClock = isNixClockForUserMenu();
    const bool soundEnabled = isAlarmFeatureEnabled();
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
        Serial.printf("\n║ Активность (будни):   %s", displayIntervalsWorkdays);
        Serial.printf("\n║ Активность (выходн.): %s", displayIntervalsHolidays);
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
        Serial.println("  display activity workdays / daw HH-HH HH2-HH2  - Активность дисплея (будни)");
        Serial.println("  display activity holidays / dah HH-HH HH2-HH2  - Активность дисплея (выходные)");
        Serial.println("  display activity copy w2h / dacw               - Копировать будни -> выходные");
        Serial.println("  display activity copy h2w / dach               - Копировать выходные -> будни");
        Serial.println("   До и после полудня (0-12 и 12-24) - полуинтервал [start,end), например: 0-8 16-24");

        Serial.println("\n  brightness control on/off / bc1/bc0 - Вкл/выкл управление яркостью");
        Serial.println("  max brightness learning / mbe         - Обучение порога max яркости");
        Serial.println("  smallest brightness learning / sbe    - Обучение порога min яркости");
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

        if (lower.equals("display activity copy w2h") || lower.equals("dacw")) {
            config.display_holiday_active_start_hour = config.display_active_start_hour;
            config.display_holiday_active_end_hour = config.display_active_end_hour;
            config.display_holiday_active_start_hour_2 = config.display_active_start_hour_2;
            config.display_holiday_active_end_hour_2 = config.display_active_end_hour_2;
            saveConfig();

            char mergedIntervals[40] = {0};
            formatDisplayActivityIntervals(config.display_holiday_active_start_hour,
                                          config.display_holiday_active_end_hour,
                                          config.display_holiday_active_start_hour_2,
                                          config.display_holiday_active_end_hour_2,
                                          mergedIntervals,
                                          sizeof(mergedIntervals));
            Serial.printf("Активность дисплея (выходные) скопирована из будней: %s\n", mergedIntervals);
            printDisplayMenu();
            return;
        }

        if (lower.equals("display activity copy h2w") || lower.equals("dach")) {
            config.display_active_start_hour = config.display_holiday_active_start_hour;
            config.display_active_end_hour = config.display_holiday_active_end_hour;
            config.display_active_start_hour_2 = config.display_holiday_active_start_hour_2;
            config.display_active_end_hour_2 = config.display_holiday_active_end_hour_2;
            saveConfig();

            char mergedIntervals[40] = {0};
            formatDisplayActivityIntervals(config.display_active_start_hour,
                                          config.display_active_end_hour,
                                          config.display_active_start_hour_2,
                                          config.display_active_end_hour_2,
                                          mergedIntervals,
                                          sizeof(mergedIntervals));
            Serial.printf("Активность дисплея (будни) скопирована из выходных: %s\n", mergedIntervals);
            printDisplayMenu();
            return;
        }

        if (lower.startsWith("display activity workdays ") || lower.startsWith("daw ") ||
            lower.startsWith("display activity times ") || lower.startsWith("dat ")) {
            String arg;
            if (lower.startsWith("daw ")) {
                arg = lower.substring(4);
            } else if (lower.startsWith("display activity workdays ")) {
                arg = lower.substring(26);
            } else if (lower.startsWith("dat ")) {
                arg = lower.substring(4);
            } else {
                arg = lower.substring(23);
            }
            arg.trim();
            uint8_t s1 = 0, e1 = 0, s2 = 12, e2 = 12;
            if (!parseDisplayActivityTimesCommand(arg, s1, e1, s2, e2)) {
                Serial.println("Неверный формат. Используйте: daw HH-HH HH2-HH2");
                Serial.println("Ограничение: первый интервал 0..12, второй 12..24 (например: daw 2-12 12-16)");
            } else {
                config.display_active_start_hour = s1;
                config.display_active_end_hour = e1;
                config.display_active_start_hour_2 = s2;
                config.display_active_end_hour_2 = e2;
                saveConfig();
                char mergedIntervals[40] = {0};
                formatDisplayActivityIntervals(config.display_active_start_hour,
                                              config.display_active_end_hour,
                                              config.display_active_start_hour_2,
                                              config.display_active_end_hour_2,
                                              mergedIntervals,
                                              sizeof(mergedIntervals));
                Serial.printf("Активность дисплея (будни): %s\n", mergedIntervals);
            }
            printDisplayMenu();
            return;
        }

        if (lower.startsWith("display activity holidays ") || lower.startsWith("dah ")) {
            String arg = lower.startsWith("dah ") ? lower.substring(4) : lower.substring(26);
            arg.trim();
            uint8_t s1 = 0, e1 = 0, s2 = 12, e2 = 12;
            if (!parseDisplayActivityTimesCommand(arg, s1, e1, s2, e2)) {
                Serial.println("Неверный формат. Используйте: dah HH-HH HH2-HH2");
                Serial.println("Ограничение: первый интервал 0..12, второй 12..24 (например: dah 2-12 12-16)");
            } else {
                config.display_holiday_active_start_hour = s1;
                config.display_holiday_active_end_hour = e1;
                config.display_holiday_active_start_hour_2 = s2;
                config.display_holiday_active_end_hour_2 = e2;
                saveConfig();
                char mergedIntervals[40] = {0};
                formatDisplayActivityIntervals(config.display_holiday_active_start_hour,
                                              config.display_holiday_active_end_hour,
                                              config.display_holiday_active_start_hour_2,
                                              config.display_holiday_active_end_hour_2,
                                              mergedIntervals,
                                              sizeof(mergedIntervals));
                Serial.printf("Активность дисплея (выходные): %s\n", mergedIntervals);
            }
            printDisplayMenu();
            return;
        }
    }

    Serial.println("Неизвестная команда. Введите 'help' для справки");
    printDisplayMenu();
}
