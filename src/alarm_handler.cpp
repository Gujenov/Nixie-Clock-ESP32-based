#include "alarm_handler.h"
#include "config.h"
#include "time_utils.h"

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

// Проверка срабатывания будильников
void checkAlarms() {
    static uint8_t lastCheckedHour = 255;  // Недопустимые начальные значения
    static uint8_t lastCheckedMinute = 255;
    
    // ИСПРАВЛЕНИЕ 1: Используем новую функцию getCurrentUTCTime()
    time_t now_utc = getCurrentUTCTime();  // Получаем UTC время
    
    if (now_utc == 0) {
        return;  // Время не получено
    }
    
    // ИСПРАВЛЕНИЕ 2: Конвертируем UTC в локальное время для будильников
    struct tm timeinfo;
    localtime_r(&now_utc, &timeinfo);  // Thread-safe версия
    
    // Проверяем только при изменении часа или минуты
    if (timeinfo.tm_hour == lastCheckedHour && 
        timeinfo.tm_min == lastCheckedMinute) {
        return;
    }

    // Обновляем последние проверенные значения
    lastCheckedHour = timeinfo.tm_hour;
    lastCheckedMinute = timeinfo.tm_min;
    
    // Лямбда для проверки одного будильника
    auto checkAlarm = [&timeinfo](const AlarmSettings &alarm, uint8_t num) {
        if (alarm.enabled && 
            alarm.hour == timeinfo.tm_hour && 
            alarm.minute == timeinfo.tm_min) {
            
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
            
            // Добавьте здесь звук/световую индикацию
            // Например: triggerAlarm(num);
        }
    };
    
    checkAlarm(config.alarm1, 1);
    checkAlarm(config.alarm2, 2);
}

// Дополнительная функция для проверки времени до будильника (опционально)
uint8_t getMinutesToNextAlarm() {
    time_t now_utc = getCurrentUTCTime();
    if (now_utc == 0) return 255;
    
    struct tm now_local;
    localtime_r(&now_utc, &now_local);
    
    // Текущее время в минутах от начала дня
    uint16_t currentMinutes = now_local.tm_hour * 60 + now_local.tm_min;
    
    uint8_t minMinutes = 255;  // Максимальное значение
    
    auto checkAlarmTime = [currentMinutes, &minMinutes](const AlarmSettings &alarm) {
        if (alarm.enabled) {
            uint16_t alarmMinutes = alarm.hour * 60 + alarm.minute;
            uint16_t diff;
            
            if (alarmMinutes >= currentMinutes) {
                diff = alarmMinutes - currentMinutes;
            } else {
                // Будильник на завтра
                diff = (24 * 60 - currentMinutes) + alarmMinutes;
            }
            
            if (diff < minMinutes) {
                minMinutes = diff;
            }
        }
    };
    
    checkAlarmTime(config.alarm1);
    checkAlarmTime(config.alarm2);
    
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
    Serial.print("\n\n=== Статус будильников ===");
    
    auto printAlarmInfo = [](const AlarmSettings &alarm, uint8_t num) {
        Serial.printf("Будильник %d: ", num);
        if (alarm.enabled) {
            Serial.printf("ВКЛ %02d:%02d", alarm.hour, alarm.minute);
        } else {
            Serial.print("ВЫКЛ");
        }
        Serial.print("\n");
    };
    
    printAlarmInfo(config.alarm1, 1);
    printAlarmInfo(config.alarm2, 2);
    
    // Время до ближайшего будильника
    uint8_t minutesToAlarm = getMinutesToNextAlarm();
    if (minutesToAlarm < 255) {
        Serial.printf("До ближайшего будильника: %d минут\n", minutesToAlarm);
    } else {
        Serial.print("\nНет активных будильников");
    }
    
    Serial.print("\n=========================\n");
}