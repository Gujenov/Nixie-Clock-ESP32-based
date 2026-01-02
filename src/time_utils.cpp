#include "time_utils.h"
#include "config.h"
#include "hardware.h"

extern WiFiUDP ntpUDP;         // Определен где-то еще (возможно в .ino)
extern NTPClient *timeClient;  // Определен в config.cpp


// Основная функция проверки и инициализации источников времени
// Вызывается при старте и потом при каждом получении времени
void checkTimeSource() {
    static bool firstCheck = true;
    static bool interruptsConfigured = false;
    
    if (firstCheck) {
        firstCheck = false;
        Wire.begin(I2C_SDA, I2C_SCL);
        Wire.setClock(100000);
        Serial.println("\n[SYSTEM] Инициализация I2C завершена");
    }
    
    // Проверка OSF (этот блок можно оставить)
    Wire.beginTransmission(0x68);
    Wire.write(0x0F);
    if (Wire.endTransmission() == 0) {
        Wire.requestFrom(0x68, 1);
        if (Wire.available()) {
            uint8_t status = Wire.read();
            if (status & 0x80) {
                Serial.println("[DS3231] ⚠️ Флаг OSF установлен (питание пропадало)");
                Wire.beginTransmission(0x68);
                Wire.write(0x0F);
                Wire.write(status & 0x7F);
                Wire.endTransmission();
                setDefaultTimeToAllSources();
            }
        }
    }
    
    Wire.beginTransmission(0x68);
    bool ds3231_now_available = (Wire.endTransmission() == 0);
    
    bool showConnectionMessage = false;
    time_t diff = 0;

    // Если DS3231 появился, но не инициализирован
    if (ds3231_now_available && !rtc) {
        rtc = new RTC_DS3231();
        if (rtc && rtc->begin()) {
            ds3231_available = true;
            currentTimeSource = EXTERNAL_DS3231;

            Serial.println("\n✓ DS3231 инициализирован");
            
            // Получаем время от DS3231
            time_t currentTime = getCurrentUTCTime();
            
            if (currentTime > 0) {  // Проверяем, что время корректно
                // Сравниваем с системным временем
                time_t sys_time;
                time(&sys_time);
                diff = abs(currentTime - sys_time);
                
                if (diff > 1) {
                    struct timeval tv = { currentTime, 0 };
                    settimeofday(&tv, NULL);
                }
                
                // ВАЖНО: Устанавливаем флаг для показа сообщения!
                showConnectionMessage = true;
            } else {
                Serial.println("[DS3231] Получено некорректное время");
                setDefaultTimeToAllSources();
            }
            
            setupInterrupts();
        }
    }
    
    // Если статус изменился (DS3231 появился/исчез)
    if (ds3231_now_available != ds3231_available) {
        ds3231_available = ds3231_now_available;
        
        if (ds3231_available) {
            currentTimeSource = EXTERNAL_DS3231;
            
            if (rtc) {
                time_t currentTime = getCurrentUTCTime();
                
                if (currentTime == 0) {
                    Serial.println("[DS3231] Повторное подключение: время некорректно");
                    setDefaultTimeToAllSources();
                } else {
                    time_t sys_time;
                    time(&sys_time);
                    diff = abs(currentTime - sys_time);
                    
                    if (diff > 5) {
                        struct timeval tv = { currentTime, 0 };
                        settimeofday(&tv, NULL);
                    }
                    showConnectionMessage = true;
                }
            }
        } else {
            currentTimeSource = INTERNAL_RTC;
            Serial.println("\n[ERR] DS3231 отключен, использую System RTC");
        }
        
        setupInterrupts();
    }
    
    // Выводим сообщение о подключении (если нужно)
    if (showConnectionMessage && ds3231_available) {
        if (diff > 5) {
            Serial.printf("[SYNC] DS3231 подключен, синхронизация (расхождение: %ld сек)\n", diff);
        } else {
            Serial.println("[SYNC] DS3231 подключен, время совпадает");
        }
    }
    
    // Если это первый запуск и DS3231 не найден
    static bool firstRunMessage = true;
    if (firstRunMessage && !ds3231_available && !rtc) {
        currentTimeSource = INTERNAL_RTC;
        Serial.println("\n✓ Используются внутренние часы RTC");
        setDefaultTimeToAllSources();
        setupInterrupts();
        firstRunMessage = false;
    }
}

time_t getCurrentUTCTime() {
    // Всегда проверяем актуальность источника
    checkTimeSource();
    
    if (currentTimeSource == EXTERNAL_DS3231 && rtc && ds3231_available) {
        // ТОЛЬКО чтение, БЕЗ обновления системного времени
        DateTime now = rtc->now();
        return convertDateTimeToTimeT(now);
    } else {
        // Берем системное время
        time_t sys_time;
        time(&sys_time);
        return sys_time;
    }
}

time_t convertDateTimeToTimeT(const DateTime& dt) {
    struct tm tm_time = {0};
    
    // Заполняем struct tm (предполагаем UTC)
    tm_time.tm_year = dt.year() - 1900;
    tm_time.tm_mon = dt.month() - 1;
    tm_time.tm_mday = dt.day();
    tm_time.tm_hour = dt.hour();
    tm_time.tm_min = dt.minute();
    tm_time.tm_sec = dt.second();
    tm_time.tm_isdst = 0; // В UTC нет DST
      
        return mktime(&tm_time); // Временная зона должна быть UTC в системе

}

DateTime convertTimeTToDateTime(time_t utcTime) {
    struct tm* tm_info = gmtime(&utcTime); // Используем gmtime для UTC
    
    return DateTime(
        tm_info->tm_year + 1900,
        tm_info->tm_mon + 1,
        tm_info->tm_mday,
        tm_info->tm_hour,
        tm_info->tm_min,
        tm_info->tm_sec
    );
}

void setTimeToAllSources(time_t utcTime) {
    // 1. Устанавливаем системное время ESP32 (должно быть UTC)
    struct timeval tv = { utcTime, 0 };
    settimeofday(&tv, NULL);
    Serial.println("[RTC] обновлен");

    // 2. Если есть внешний RTC, обновляем и его
    if(ds3231_available && rtc) {
        DateTime dt = convertTimeTToDateTime(utcTime);
        rtc->adjust(dt);
        Serial.println("[DS3231] обновлен");
    }
    
   // Serial.print("Текущее время: ");
   // printTimeFromTimeT(utcTime);
}

bool syncTime() {
    // Проверяем, инициализирован ли timeClient
    if (!timeClient) {
        Serial.println("\n[NTP] Ошибка: timeClient не инициализирован");
        return false;
    }
    
    digitalWrite(LED_PIN, HIGH);
    Serial.println("\n[NTP] Попытка синхронизации...");
    
    bool success = false;
    
    // 1. Включаем WiFi
    WiFi.mode(WIFI_STA);
    WiFi.begin(config.wifi_ssid, config.wifi_pass);
    
    // 2. Ждем подключения с таймаутом
    int attempts = 0;
    Serial.print("[WiFi] Подключение");
    while (WiFi.status() != WL_CONNECTED && attempts < 10) {
        delay(300);
        Serial.print(".");
        attempts++;
    }
    Serial.println();
    
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[NTP] Ошибка: не удалось подключиться к WiFi");
        digitalWrite(LED_PIN, LOW);
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        return false;
    }
    
    Serial.printf("[WiFi] Подключено к %s\n", config.wifi_ssid);
    Serial.printf("[WiFi] IP: %s\n", WiFi.localIP().toString().c_str());
    
    // 3. Пробуем синхронизироваться с NTP
    try {
        timeClient->begin();
        timeClient->setTimeOffset(0); // Запрашиваем UTC
        
        if (timeClient->forceUpdate()) {
            // Получаем UTC время
            time_t utcTime = timeClient->getEpochTime();
            
            // Проверяем, что время валидное (не 1970 год)
            if (utcTime > 1609459200) { // После 2021-01-01
                
                // Выводим полученное UTC время
                struct tm *tm_utc = gmtime(&utcTime);
                Serial.printf("[NTP] Получено UTC: %04d-%02d-%02d %02d:%02d:%02d\n", 
                           tm_utc->tm_year + 1900, tm_utc->tm_mon + 1, tm_utc->tm_mday,
                           tm_utc->tm_hour, tm_utc->tm_min, tm_utc->tm_sec);
                
                // Устанавливаем UTC время в систему
                struct timeval tv = { utcTime, 0 };
                settimeofday(&tv, NULL);
                Serial.println("[NTP]->[RTC] Время записано во внутренний RTC");
                
                // Записываем в DS3231 ТОЖЕ UTC
                if (currentTimeSource == EXTERNAL_DS3231 && rtc) {
                    DateTime rtcTime(utcTime); // Конструктор принимает time_t (UTC)
                    rtc->adjust(rtcTime);
                    Serial.println("[NTP]->[DS3231] Время записано в аппаратные часы");
                }
                
                // УДАЛЕНА строка с локальным временем и DST
                // struct tm *tm_local = localtime(&utcTime);
                // Serial.printf("[NTP] Локальное время: %02d:%02d:%02d (TZ=%+d, DST=%s)\n",
                //        tm_local->tm_hour, tm_local->tm_min, tm_local->tm_sec,
                //        config.time_config.timezone_offset, 
                //        config.time_config.dst_enabled ? "ON" : "OFF");
                              
                // Обновляем время последней синхронизации в конфиге
                config.time_config.last_ntp_sync = utcTime;
                saveConfig();
                
                success = true;
                digitalWrite(LED_PIN, LOW);
            } else {
                Serial.println("[NTP] Ошибка: получено некорректное время");
            }
        } else {
            Serial.println("[NTP] Ошибка: forceUpdate() не удался");
        }
        
        timeClient->end();
    } catch (...) {
        Serial.println("[NTP] Исключение при синхронизации!");
    }
    
    // 4. Отключаем WiFi
    Serial.println("[WiFi] Отключение...");
    WiFi.disconnect(true);
    delay(100);
    WiFi.mode(WIFI_OFF);
    
    if (!success) {
        blinkError(11);
        Serial.println("[NTP] Не удалось синхронизировать время!");
    } else {
        Serial.println("[NTP] Синхронизация успешна!");
    }
    
    return success;
}

bool printTime() {
    // Получаем время через getCurrentUTCTime() - она сама определит источник
    time_t utcTime = getCurrentUTCTime();
    
    if (utcTime > 0) {
        struct tm utc_tm;
        gmtime_r(&utcTime, &utc_tm);
        
        char buf[64];
        strftime(buf, sizeof(buf), "%a %d.%m.%Y %H:%M:%S UTC", &utc_tm);
        Serial.print(buf);
        
        // Показываем текущий источник
        if (currentTimeSource == EXTERNAL_DS3231 && ds3231_available) {
            Serial.println(" [DS3231]");
        } else {
            Serial.println(" [ESP32 RTC]");
        }
        
        return true;
    }
    
    Serial.println("Ошибка получения времени");
    return false;
}

void printTimeFromTimeT(time_t utcTime) {
    // Простая заглушка
    if (utcTime > 0) {
        struct tm* tm_info = localtime(&utcTime);
        Serial.printf("%04d-%02d-%02d %02d:%02d:%02d\n",
            tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
            tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
    }
}

// Установка времени по умолчанию: 9:00 6.07.2025
void setDefaultTimeToAllSources() {
    struct tm default_tm = {0};
    default_tm.tm_year = 125;     // 2025
    default_tm.tm_mon = 7 - 1;
    default_tm.tm_mday = 6;
    default_tm.tm_hour = 9;
    default_tm.tm_min = 0;
    default_tm.tm_sec = 0;
    default_tm.tm_isdst = 0;
    
    time_t default_time = mktime(&default_tm);

    
    // Устанавливаем во все источники
    setTimeToAllSources(default_time);
}

// Установка времени (часы:минуты:секунды)
bool setManualTime(const String &timeStr) {
    int hours, minutes, seconds;
    
    if (sscanf(timeStr.c_str(), "%d:%d:%d", &hours, &minutes, &seconds) != 3) {
        Serial.println("Ошибка формата времени. Используйте HH:MM:SS");
        return false;
    }

    if (hours < 0 || hours > 23 || minutes < 0 || minutes > 59 || seconds < 0 || seconds > 59) {
        Serial.println("Ошибка: некорректное время (00:00:00 - 23:59:59)");
        return false;
    }

    // 1. Получаем текущую дату в UTC
    time_t now_utc;
    time(&now_utc);
    struct tm utc_time;
    gmtime_r(&now_utc, &utc_time);
    
    // 2. Если дата некорректна (< 2025), устанавливаем дефолтную (06.07.2025)
    if (utc_time.tm_year + 1900 < 2025) {
        utc_time.tm_year = 2025 - 1900;  // 2025 год
        utc_time.tm_mon = 7 - 1;         // Июль
        utc_time.tm_mday = 6;            // 6-е число
        Serial.println("⚠️  Использую дату по умолчанию: 06.07.2025");
    }
    
    // 3. Меняем только время
    utc_time.tm_hour = hours;
    utc_time.tm_min = minutes;
    utc_time.tm_sec = seconds;
    
    // 4. Записываем время (UTC)
    time_t newTime_utc = mktime(&utc_time);
    
    if (newTime_utc == -1) {
        Serial.println("Ошибка конвертации времени");
        return false;
    }
    
    // 5. Устанавливаем во все источники через единую функцию
    setTimeToAllSources(newTime_utc);
    
    Serial.printf("✅ Время установлено: %02d:%02d:%02d UTC\n", hours, minutes, seconds);
    //printTime();
    return true;
}

bool isValidDate(int day, int month, int year) {
    // Встроенная функция для високосного года
    auto isLeapYear = [](int y) -> bool {
        return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
    };
    
    // Проверки
    if (year < 2025 || year > 2099) return false;
    if (month < 1 || month > 12) return false;
    
    if (month == 4 || month == 6 || month == 9 || month == 11) {
        return day >= 1 && day <= 30;
    }
    
    if (month == 2) {
        return day >= 1 && day <= (isLeapYear(year) ? 29 : 28);
    }
    
    return day >= 1 && day <= 31;
}

// Установка даты (день.месяц.год)
bool setManualDate(const String &dateStr) {
    int day, month, year;
    
    if (sscanf(dateStr.c_str(), "%d.%d.%d", &day, &month, &year) != 3) {
        Serial.println("Ошибка формата даты. Используйте DD.MM.YYYY");
        return false;
    }

    // Простая проверка даты
    if (!isValidDate(day, month, year)) {
        Serial.printf("Ошибка: некорректная дата %02d.%02d.%04d\n", day, month, year);
        
        // Полезное сообщение для 29 февраля
        if (month == 2 && day == 29) {
            Serial.printf("Год %d не высокосный\n", year);
        }
        return false;
    }

    // 1. Получаем текущее время (UTC)
    time_t now_utc;
    time(&now_utc);
    struct tm utc_time;
    gmtime_r(&now_utc, &utc_time);
    
    
    // 2. Меняем дату (сохраняем текущее время)
    utc_time.tm_mday = day;
    utc_time.tm_mon = month - 1;  // Месяцы 0-11
    utc_time.tm_year = year - 1900;        
        
    
    // 3. Записываем время (UTC)
    time_t newTime_utc = mktime(&utc_time);


    // 4. Устанавливаем во все источники
    setTimeToAllSources(newTime_utc);
    
    Serial.printf("✅ Дата установлена: %02d.%02d.%04d UTC\n", day, month, year);
    //printTime();
    return true;
}

int calculateDayOfWeek(int year, int month, int day) {
    if (month < 3) {
        month += 12;
        year -= 1;
    }
    int k = year % 100;
    int j = year / 100;
    int h = (day + (13 * (month + 1)) / 5 + k + (k / 4) + (j / 4) + (5 * j)) % 7;
    
    // Преобразуем к стандарту C: 0=воскресенье, 1=понедельник, ..., 6=суббота
    return (h + 5) % 7; // Zeller дает: 0=суббота, 1=воскресенье, ... так что корректируем
}

