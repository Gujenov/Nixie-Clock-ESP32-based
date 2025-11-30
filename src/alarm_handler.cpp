#include "alarm_handler.h"
#include "config.h"
#include "time_utils.h"

// Установка будильника (общая функция)
bool setAlarm(uint8_t alarmNum, const String &timeStr) {
    int hours, minutes;
    if (sscanf(timeStr.c_str(), "%d:%d", &hours, &minutes) != 2) {
        Serial.println("Ошибка формата. Используйте HH:MM");
        return false;
    }

    if (hours < 0 || hours > 23 || minutes < 0 || minutes > 59) {
        Serial.println("Ошибка: некорректное время (00:00 - 23:59)");
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
        Serial.println("Ошибка: неверный номер будильника (1 или 2)");
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
    
    time_t now = getCurrentTime();
    struct tm *timeinfo = localtime(&now);
    
    // Проверяем только при изменении часа или минуты
    if (timeinfo->tm_hour == lastCheckedHour && 
        timeinfo->tm_min == lastCheckedMinute) {
        return;
    }

    // Обновляем последние проверенные значения
    lastCheckedHour = timeinfo->tm_hour;
    lastCheckedMinute = timeinfo->tm_min;
    
    // Лямбда для проверки одного будильника
    auto checkAlarm = [timeinfo](const AlarmSettings &alarm, uint8_t num) {
        if (alarm.enabled && 
            alarm.hour == timeinfo->tm_hour && 
            alarm.minute == timeinfo->tm_min) {
            Serial.printf("[ALARM %d] Сработал в %02d:%02d:%02d\n", 
                        num, 
                        timeinfo->tm_hour, 
                        timeinfo->tm_min, 
                        timeinfo->tm_sec);
            // Добавьте здесь звук/световую индикацию
        }
    };
    
    checkAlarm(config.alarm1, 1);
    checkAlarm(config.alarm2, 2);
}