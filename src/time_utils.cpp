#include "time_utils.h"
#include "config.h"
#include "hardware.h"

extern WiFiUDP ntpUDP;         // Определен где-то еще (возможно в .ino)
extern NTPClient *timeClient;  // Определен в config.cpp

// Основная функция проверки и инициализации источников времени
// Вызывается при старте и потом при каждом получении времени
void checkTimeSource() {
    static bool firstRun = true;
    
    if (firstRun) {
        // ПЕРВЫЙ ВЫЗОВ: инициализация (заменяем initTimeSource())
        firstRun = false;
        
        Wire.begin(I2C_SDA, I2C_SCL);
        Wire.setClock(100000);
        
        // Пытаемся инициализировать DS3231
        Wire.beginTransmission(0x68);
        if (Wire.endTransmission() == 0) {
            rtc = new RTC_DS3231();
            if (rtc && rtc->begin()) {
                // Проверяем валидность времени в DS3231
                DateTime now = rtc->now();
                if (now.year() >= 2021 && now.year() <= 2100) {
                    currentTimeSource = EXTERNAL_DS3231;
                    ds3231_available = true;
                    
                    // Устанавливаем время из DS3231
                    time_t rtc_utc = convertDateTimeToTimeT(now);
                    struct timeval tv = { rtc_utc, 0 };
                    settimeofday(&tv, NULL);
                    
                    Serial.println("\n✓ DS3231 инициализирован");
                } else {
                    // Время некорректно
                    delete rtc;
                    rtc = nullptr;
                    currentTimeSource = INTERNAL_RTC;
                    ds3231_available = false;
                    Serial.println("\n⚠ DS3231 найден, но время некорректно");
                    setDefaultTimeToAllSources();
                }
            }
        }
        
        if (!ds3231_available) {
            currentTimeSource = INTERNAL_RTC;
            Serial.println("\n✓ Используются внутренние часы RTC");
            setDefaultTimeToAllSources();
        }
        
        // Настраиваем прерывания
        setupInterrupts();
        return;
    }
    
    // ПОСЛЕДУЮЩИЕ ВЫЗОВЫ: только проверка доступности
    if (currentTimeSource == EXTERNAL_DS3231) {
        // Проверяем, доступен ли еще DS3231
        Wire.beginTransmission(0x68);
        bool stillAvailable = (Wire.endTransmission() == 0);
        
        if (!stillAvailable && ds3231_available) {
            // DS3231 пропал!
            ds3231_available = false;
            currentTimeSource = INTERNAL_RTC;
            Serial.println("[TIME] ⚠ DS3231 отключился, переключаюсь на System RTC");
        }
    }
}

time_t getCurrentUTCTime() {
    // Всегда проверяем актуальность источника
    checkTimeSource();
    
    if (currentTimeSource == EXTERNAL_DS3231 && rtc && ds3231_available) {
        // Берем время от DS3231
        DateTime now = rtc->now();
        time_t rtc_time = convertDateTimeToTimeT(now);
        
        // Обновляем системное время (чтобы оно не уплывало)
        struct timeval tv = { rtc_time, 0 };
        settimeofday(&tv, NULL);
        
        return rtc_time;
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
    
    // Временно устанавливаем UTC
    char* old_tz = getenv("TZ");
    setenv("TZ", "UTC", 1);
    tzset();
    
    time_t result = mktime(&tm_time);
    
    // Восстанавливаем часовой пояс
    if(old_tz) {
        setenv("TZ", old_tz, 1);
    } else {
        unsetenv("TZ");
    }
    tzset();
    
    return result;
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
    
    Serial.print("Установленное время: ");
    printTimeFromTimeT(utcTime);
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
        Serial.print("Время: ");
        Serial.print(buf);
        
        // Показываем текущий источник
        if (currentTimeSource == EXTERNAL_DS3231 && ds3231_available) {
            Serial.println(" (DS3231)");
        } else {
            Serial.println(" (System)");
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

// Установка времени (часы:минуты:секунды)
bool setManualTime(const String &timeStr) {
  int hours, minutes, seconds;
  
  if (sscanf(timeStr.c_str(), "%d:%d:%d", &hours, &minutes, &seconds) != 3) {
    Serial.println("Ошибка формата времени. Используйте HH:MM:SS");
    return false;
  }

  if (hours < 0 || hours > 23 || minutes < 0 || minutes > 59 || seconds < 0 || seconds > 59) {
    Serial.println("Ошибка: некорректное время (допустимо 00:00:00 - 23:59:59)");
    return false;
  }

  // 1. Получаем текущую дату в UTC
  time_t now_utc;
  time(&now_utc);
  struct tm utc_time;
  gmtime_r(&now_utc, &utc_time);
  
  // 2. Если дата некорректна (1970), устанавливаем дефолтную
  if (utc_time.tm_year < 101) { // менее 2001 года
    utc_time.tm_year = 90;     // 1990
    utc_time.tm_mon = 6;       // Июль
    utc_time.tm_mday = 6;      // 6-е число
  }
  
  // 3. Устанавливаем новое время (UTC!)
  utc_time.tm_hour = hours;
  utc_time.tm_min = minutes;
  utc_time.tm_sec = seconds;
  
  // 4. Ручной расчет Unix time (чтобы избежать mktime с TZ)
  // Простая формула для дат после 1970
  time_t newTime_utc = manualTimeToUnix(&utc_time);
  
  // 5. Устанавливаем время
  struct timeval tv = { newTime_utc, 0 };
  settimeofday(&tv, NULL);
  
  // 6. Обновляем RTC (UTC)
  if (currentTimeSource == EXTERNAL_DS3231 && rtc) {
    DateTime dt(newTime_utc);
    rtc->adjust(dt);
  }
  
  // Логирование
  Serial.printf("Время установлено: %02d:%02d:%02d UTC\n", hours, minutes, seconds);
  return true;
}

// Вспомогательная функция для ручного расчета Unix time
time_t manualTimeToUnix(struct tm* tm) {
  // Упрощенный расчет для дат после 2000 года
  // В реальном проекте лучше использовать библиотечные функции
  
  // Просто используем стандартный mktime с временным UTC
  char* old_tz = getenv("TZ");
  setenv("TZ", "UTC", 1);
  tzset();
  
  time_t result = mktime(tm);
  
  if (old_tz) {
    setenv("TZ", old_tz, 1);
  } else {
    unsetenv("TZ");
  }
  tzset();
  
  return result;
}

// Установка даты (день.месяц.год)
bool setManualDate(const String &dateStr) {
    int day, month, year;
    
    if (sscanf(dateStr.c_str(), "%d.%d.%d", &day, &month, &year) != 3) {
        Serial.println("Ошибка формата даты. Используйте DD.MM.YYYY");
        return false;
    }

    if (day < 1 || day > 31 || month < 1 || month > 12 || year < 2000 || year > 2100) {
        Serial.println("Ошибка: некорректная дата");
        return false;
    }

    // 1. Получаем текущее время
    time_t now_utc;
    time(&now_utc);
    
    // 2. Берем текущие час/минуту/секунду
    struct tm utc_time;
    gmtime_r(&now_utc, &utc_time);
    
    // 3. Меняем только дату
    utc_time.tm_mday = day;
    utc_time.tm_mon = month - 1;
    utc_time.tm_year = year - 1900;
    
    // 4. Используем существующую функцию из RTClib если есть
    // Или создаем DateTime напрямую
    DateTime dt(year, month, day, 
                utc_time.tm_hour, 
                utc_time.tm_min, 
                utc_time.tm_sec);
    
    time_t newTime_utc = dt.unixtime();  // RTClib уже дает Unix time!
    
    // 5. Устанавливаем
    setTimeToAllSources(newTime_utc);
    
    Serial.printf("Дата установлена: %02d.%02d.%04d UTC\n", day, month, year);
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

// Установка времени по умолчанию: 9:00 6.07.1990 Пятница
void setDefaultTimeToAllSources() {
    struct tm default_tm = {0};
    default_tm.tm_year = 1990 - 1900;
    default_tm.tm_mon = 7 - 1;
    default_tm.tm_mday = 6;
    default_tm.tm_hour = 9;
    default_tm.tm_min = 0;
    default_tm.tm_sec = 0;
    default_tm.tm_isdst = 0;
    
    // Конвертируем в time_t (UTC)
    setenv("TZ", "UTC", 1);
    tzset();
    time_t default_time = mktime(&default_tm);
    unsetenv("TZ");
    tzset();
    
    // Устанавливаем во все источники
    setTimeToAllSources(default_time);
}

// Вспомогательная функция для обновления дня недели в RTC
void updateDayOfWeekInRTC() {
    if (currentTimeSource == EXTERNAL_DS3231 && rtc) {
        DateTime now = rtc->now();
        
        // Проверяем, установлен ли день недели (0-7, где 0=нет дня недели)
        if (now.dayOfTheWeek() == 0) {
            int dow = calculateDayOfWeek(now.year(), now.month(), now.day());
            // Конвертируем из C формата (0=вс) в формат DS3231 (1=пн)
            int rtc_dow = (dow == 0) ? 7 : dow; // Вс=7, Пн=1, ..., Сб=6
            // Обновляем день недели в RTC
            // Для DS3231 нужно перезаписать время с правильным днем недели
            DateTime updated(now.year(), now.month(), now.day(), 
                            now.hour(), now.minute(), now.second());
            // К сожалению, библиотека RTClib обычно не позволяет установить день недели отдельно
            // Нужно записывать полностью
            rtc->adjust(updated);
            Serial.printf("День недели установлен в RTC: %d\n", rtc_dow);
        }
    }
}

