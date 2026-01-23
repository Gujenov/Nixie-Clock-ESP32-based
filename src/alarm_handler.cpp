#include "alarm_handler.h"
#include "config.h"
#include "time_utils.h"
#include "timezone_manager.h"

// Установка будильника (общая функция)
bool setAlarm(uint8_t alarmNum, const String &timeStr) {
    int hours, minutes;
    if (sscanf(timeStr.c_str(), "%d:%d", &hours, &minutes) != 2) {
        Serial.print("\nОшибка формата. Используйте HH:MM");
        return false;
    }

    if (hours < 0 || hours > 23 || minutes < 0 || minutes > 59) {
        Serial.print("\nОшибка: некорректное время (00:00 - 23:59)");
        return false;
    }

    if (alarmNum == 1) {
        config.alarm1.hour = hours;
        config.alarm1.minute = minutes;
        config.alarm1.enabled = true;
    } else if (alarmNum == 2) {
        config.alarm2.hour = hours;
        config.alarm2.minute = minutes;
        config.alarm2.enabled = true;
    } else {
        Serial.print("\nОшибка: неверный номер будильника (1 или 2)");
        return false;
    }

    saveConfig();
    Serial.printf("Будильник %d установлен на %02d:%02d\n", alarmNum, hours, minutes);
    return true;
}

// Установка номера мелодии для будильника
bool setAlarmMelody(uint8_t alarmNum, uint8_t melody) {
    if (melody == 0) {
        Serial.print("\nОшибка: номер мелодии должен быть >= 1");
        return false;
    }

    if (alarmNum == 1) {
        config.alarm1.melody = melody;
    } else if (alarmNum == 2) {
        config.alarm2.melody = melody;
    } else {
        Serial.print("\nОшибка: неверный номер будильника (1 или 2)");
        return false;
    }

    saveConfig();
    Serial.printf("Будильник %d: мелодия установлена на %d\n", alarmNum, melody);
    return true;
}

// Установка режима одноразового будильника (только для будильника 1)
bool setAlarmOnceMode(uint8_t alarmNum, bool once) {
    if (alarmNum != 1) {
        Serial.print("\nОшибка: режим once доступен только для будильника 1");
        return false;
    }

    config.alarm1.once = once;
    saveConfig();
    Serial.printf("Будильник 1: режим %s\n", once ? "одноразовый" : "ежедневный");
    return true;
}

// Установка маски дней недели (только для будильника 2)
bool setAlarmDaysMask(uint8_t alarmNum, uint8_t daysMask) {
    if (alarmNum != 2) {
        Serial.print("\nОшибка: маска дней доступна только для будильника 2");
        return false;
    }

    config.alarm2.days_mask = daysMask;
    saveConfig();
    Serial.printf("Будильник 2: маска дней обновлена (0x%02X)\n", daysMask);
    return true;
}

static bool isDayInMask(uint8_t mask, int tm_wday) {
    // tm_wday: 0=Sun, 1=Mon, ... 6=Sat
    uint8_t bit = (tm_wday == 0) ? 6 : static_cast<uint8_t>(tm_wday - 1);
    return (mask & (1 << bit)) != 0;
}

static String formatDaysMask(uint8_t mask) {
    if (mask == 0) return String("нет");
    const char* names[7] = {"Пн", "Вт", "Ср", "Чт", "Пт", "Сб", "Вс"};
    String out;
    for (uint8_t i = 0; i < 7; ++i) {
        if (mask & (1 << i)) {
            if (out.length() > 0) out += " ";
            out += names[i];
        }
    }
    return out;
}

// Проверка срабатывания будильников
void checkAlarms() {
    time_t now_utc = getCurrentUTCTime();
    if (now_utc == 0) {
        return;  // Время не получено
    }

    // Конвертируем UTC в локальное время для будильников
    time_t now_local = utcToLocal(now_utc);
    struct tm timeinfo;
    gmtime_r(&now_local, &timeinfo);  // now_local уже со смещением

    // Проверяем только в 00 секунд заданной минуты
    if (timeinfo.tm_sec != 0) {
        return;
    }
    
    // Лямбда для проверки одного будильника
    auto checkAlarm = [&timeinfo](const AlarmSettings &alarm, uint8_t num) {
        if (alarm.enabled && 
            alarm.hour == timeinfo.tm_hour && 
            alarm.minute == timeinfo.tm_min) {

            if (num == 2 && !isDayInMask(alarm.days_mask, timeinfo.tm_wday)) {
                return;
            }
            
            Serial.printf("[ALARM %d] Сработал в %02d:%02d:%02d (локальное время)\n", 
                        num, 
                        timeinfo.tm_hour, 
                        timeinfo.tm_min, 
                        timeinfo.tm_sec);
            
            // Дополнительная информация для отладки
            time_t utc_now = getCurrentUTCTime();
            struct tm utc_tm;
            gmtime_r(&utc_now, &utc_tm);
            
            Serial.printf("[ALARM %d] UTC время: %02d:%02d:%02d\n",
                        num,
                        utc_tm.tm_hour,
                        utc_tm.tm_min,
                        utc_tm.tm_sec);

            if (num == 1 && alarm.once) {
                config.alarm1.enabled = false;
                saveConfig();
                Serial.println("[ALARM 1] Одноразовый будильник отключён");
            }
            
            // Добавьте здесь звук/световую индикацию
            // Например: triggerAlarm(num);
        }
    };
    
    checkAlarm(config.alarm1, 1);
    checkAlarm(config.alarm2, 2);
}

// Дополнительная функция для проверки времени до будильника (опционально)
uint16_t getMinutesToNextAlarm() {
    time_t now_utc = getCurrentUTCTime();
    if (now_utc == 0) return 0xFFFF;

    time_t now_local_time = utcToLocal(now_utc);
    struct tm now_local;
    gmtime_r(&now_local_time, &now_local);
    
    // Текущее время в минутах от начала дня
    uint16_t currentMinutes = now_local.tm_hour * 60 + now_local.tm_min;

    uint16_t minMinutes = 0xFFFF;  // Максимальное значение

    auto checkAlarmTimeDaily = [currentMinutes, &minMinutes](const AlarmSettings &alarm) {
        if (!alarm.enabled) return;
        uint16_t alarmMinutes = alarm.hour * 60 + alarm.minute;
        uint16_t diff = (alarmMinutes >= currentMinutes)
                            ? (alarmMinutes - currentMinutes)
                            : ((24 * 60 - currentMinutes) + alarmMinutes);
        if (diff < minMinutes) minMinutes = diff;
    };

    auto checkAlarmTimeWeekly = [currentMinutes, &now_local, &minMinutes](const AlarmSettings &alarm) {
        if (!alarm.enabled || alarm.days_mask == 0) return;
        for (uint8_t offset = 0; offset < 7; ++offset) {
            int wday = (now_local.tm_wday + offset) % 7;
            if (!isDayInMask(alarm.days_mask, wday)) continue;
            int diff = static_cast<int>(offset) * 1440 - static_cast<int>(currentMinutes) +
                       static_cast<int>(alarm.hour * 60 + alarm.minute);
            if (diff < 0) continue;
            if (static_cast<uint16_t>(diff) < minMinutes) {
                minMinutes = static_cast<uint16_t>(diff);
            }
        }
    };

    checkAlarmTimeDaily(config.alarm1);
    checkAlarmTimeWeekly(config.alarm2);
    
    return minMinutes;
}

// Функция для отключения будильника
bool disableAlarm(uint8_t alarmNum) {
    if (alarmNum == 1) {
        config.alarm1.enabled = false;
    } else if (alarmNum == 2) {
        config.alarm2.enabled = false;
    } else {
        Serial.print("\nОшибка: неверный номер будильника (1 или 2)");
        return false;
    }
    
    saveConfig();
    Serial.printf("Будильник %d отключен\n", alarmNum);
    return true;
}

// Функция для включения будильника
bool enableAlarm(uint8_t alarmNum) {
    if (alarmNum == 1) {
        config.alarm1.enabled = true;
    } else if (alarmNum == 2) {
        config.alarm2.enabled = true;
    } else {
        Serial.print("\nОшибка: неверный номер будильника (1 или 2)");
        return false;
    }
    
    saveConfig();
    Serial.printf("Будильник %d включен\n", alarmNum);
    return true;
}

// Функция для проверки статуса будильника
void printAlarmStatus() {
    Serial.println("\n=== Статус будильников ===");
    
    auto printAlarmInfo = [](const AlarmSettings &alarm, uint8_t num) {
        Serial.printf("\nБудильник %d: ", num);
        if (alarm.enabled) {
            if (num == 1) {
                Serial.printf("ВКЛ %02d:%02d (мелодия %d, %s)",
                              alarm.hour,
                              alarm.minute,
                              alarm.melody,
                              alarm.once ? "once" : "daily");
            } else {
                Serial.printf("ВКЛ %02d:%02d (мелодия %d, дни: %s)",
                              alarm.hour,
                              alarm.minute,
                              alarm.melody,
                              formatDaysMask(alarm.days_mask).c_str());
            }
        } else {
            Serial.print("ВЫКЛ");
        }
        Serial.print("\n");
    };
    
    printAlarmInfo(config.alarm1, 1);
    printAlarmInfo(config.alarm2, 2);
    
    // Время до ближайшего будильника
    bool anyEnabled = config.alarm1.enabled || config.alarm2.enabled;
    uint16_t minutesToAlarm = getMinutesToNextAlarm();
    if (anyEnabled && minutesToAlarm != 0xFFFF) {
        Serial.printf("\nДо ближайшего будильника: %u минут\n", minutesToAlarm);
    } else if (!anyEnabled) {
        Serial.print("\nНет активных будильников\n");
    }
    
    Serial.print("\n=========================\n");
}