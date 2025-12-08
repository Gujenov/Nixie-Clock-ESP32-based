#include "time_utils.h"
#include "config.h"
#include "dst_handler.h"
#include "hardware.h"

extern WiFiUDP ntpUDP;         // Определен где-то еще (возможно в .ino)
extern NTPClient *timeClient;  // Определен в config.cpp

time_t getCurrentTime() {
  if(currentTimeSource == EXTERNAL_DS3231 && rtc) {
    return rtc->now().unixtime();
  } else {
    struct tm timeinfo;
    if(getLocalTime(&timeinfo)) {
      return mktime(&timeinfo);
    }
    return 0;
  }
}

void initTimeSource() {
    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(100000);
    
    Wire.beginTransmission(0x68);
    if(Wire.endTransmission() == 0) {
        rtc = new RTC_DS3231();
        if(rtc && rtc->begin()) {
            currentTimeSource = EXTERNAL_DS3231;
            ds3231_available = true;
            
            // Проверяем валидность времени в DS3231
            DateTime now = rtc->now();
            if(now.year() >= 2021 && now.year() <= 2100) {
                Serial.println("\n✓ Используется внешний DS3231 (время валидно)");
                
                // Проверяем и устанавливаем день недели если нужно
                updateDayOfWeekInRTC();
                
                // Обновляем системное время из RTC
                time_t rtc_time = now.unixtime();
                struct timeval tv = { rtc_time, 0 };
                settimeofday(&tv, NULL);
                Serial.println("Системное время обновлено из DS3231");
            } else {
                Serial.println("\n⚠ DS3231 найден, но время некорректно");
                setDefaultTime();
            }
            return;
        }
        if(rtc) delete rtc;
        rtc = nullptr;
    }

    // Если не удалось инициализировать внешние часы
    currentTimeSource = INTERNAL_RTC;
    ds3231_available = false;
    Serial.println("\n✗ DS3231 не обнаружен");
    setDefaultTime(); // 9:00 6.07.1990 Пятница
}

bool syncTime() {
    // Проверяем, инициализирован ли timeClient
    if (!timeClient) {
        Serial.println("[NTP] Ошибка: timeClient не инициализирован");
        return false;
    }
    
    digitalWrite(LED_PIN, HIGH);
    Serial.println("[NTP] Попытка синхронизации...");
    
    bool success = false;
    
    // 1. Включаем WiFi
    WiFi.mode(WIFI_STA);
    WiFi.begin(config.wifi_ssid, config.wifi_pass);
    
    // 2. Ждем подключения с таймаутом
    int attempts = 0;
    Serial.print("[WiFi] Подключение");
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
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
            time_t utcTime = timeClient->getEpochTime();
            
            // Проверяем, что время валидное (не 1970 год)
            if (utcTime > 1609459200) { // После 2021-01-01
                
                time_t localTime = utcTime + config.time_config.timezone_offset * 3600;
                if (config.time_config.dst_enabled) {
                    localTime = mktime(localtime(&localTime)); // Автокоррекция DST
                }
                
                // Отладочный вывод
                struct tm *tm_utc = gmtime(&utcTime);
                struct tm *tm_local = localtime(&localTime);
                Serial.printf("[NTP] Получено UTC: %04d-%02d-%02d %02d:%02d:%02d\n", 
                           tm_utc->tm_year + 1900, tm_utc->tm_mon + 1, tm_utc->tm_mday,
                           tm_utc->tm_hour, tm_utc->tm_min, tm_utc->tm_sec);
                Serial.printf("[NTP] Локальное время: %02d:%02d:%02d (TZ=%+d, DST=%s)\n",
                       tm_local->tm_hour, tm_local->tm_min, tm_local->tm_sec,
                       config.time_config.timezone_offset, config.time_config.dst_enabled ? "ON" : "OFF");
                
                // Записываем системное время в UTC
                struct timeval tv = { utcTime, 0 };
                settimeofday(&tv, NULL);
                Serial.println("[RTC] Время NTP записано во внутренний RTC");
                
                // Записываем в DS3231 (если подключён)
                if (currentTimeSource == EXTERNAL_DS3231 && rtc) {
                    // Для записи в RTC используем локальное время
                    DateTime rtcTime(localTime);
                    rtc->adjust(rtcTime);
                    Serial.println("[DS3231] Время NTP в аппаратные часы");
                }
                
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

void setTimeZone(int8_t offset, bool dst_enabled, uint8_t preset_index) {
    char tz[TZ_BUF_SIZE];
    
    if (!dst_enabled) {
        snprintf(tz, sizeof(tz), "UTC%+d", offset);
    }
    else {
        // Хардкод количества пресетов - у нас 11 элементов в массиве
        if (preset_index < 11) {
            strlcpy(tz, DST_PRESETS[preset_index * 3 + 1], sizeof(tz));
        } else {
            strlcpy(tz, "UTC0", sizeof(tz));
        }
    }

    setenv("TZ", tz, 1);
    tzset();
    Serial.printf("[TZ] Установлен пояс: %s\n", tz);
}

bool printTime() {
  char buf[TIME_BUF_SIZE]; // Объявляем буфер для строки времени
  bool success = false;    // Флаг успешного получения времени
  
  // 1. Пробуем получить время из DS3231 (если подключён)
  if (currentTimeSource == EXTERNAL_DS3231 && rtc) {
    DateTime now = rtc->now();
    if (snprintf(buf, sizeof(buf), "%02d:%02d:%02d %02d.%02d.%04d (DS3231)",
                now.hour(), now.minute(), now.second(),
                now.day(), now.month(), now.year()) > 0) {
      success = true;
    }
  }

  // 2. Если DS3231 не сработал, берём время из внутреннего RTC
  if (!success) {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      if (strftime(buf, sizeof(buf), "%H:%M:%S %d.%m.%Y (Internal RTC)", &timeinfo) > 0) {
        success = true;
      }
    } else {
      // 3. Если getLocalTime() не сработал, берём "сырое" время (с момента запуска)
      time_t rawTime;
      time(&rawTime);
      struct tm *timeinfo = localtime(&rawTime);
      strftime(buf, sizeof(buf), "%H:%M:%S %d.%m.%Y (Power-On Time)", timeinfo);
      success = true;
    }
  }
  
  Serial.print("Текущее время: ");
  Serial.println(buf);
  return success;
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

  // Получаем текущее время из системы
  struct tm timeinfo;
  time_t now;
  time(&now);
  localtime_r(&now, &timeinfo);

  // Если дата не была установлена (1970 год), используем дату по умолчанию
  if (timeinfo.tm_year < 101) { // год менее 2001
    timeinfo.tm_year = 90;    // 1990 год (90 = 1990-1900)
    timeinfo.tm_mon = 6;       // июль
    timeinfo.tm_mday = 6;      // 6 число
    Serial.println("Установлена дата по умолчанию: 06.07.1990");
  }

  // Устанавливаем новое время (дата остаётся прежней)
  timeinfo.tm_hour = hours;
  timeinfo.tm_min = minutes;
  timeinfo.tm_sec = seconds;
  time_t newTime = mktime(&timeinfo);

  // Применяем изменения
  struct timeval tv = { newTime, 0 };
  settimeofday(&tv, NULL);

  // Обновляем аппаратные часы (если подключены)
  if (currentTimeSource == EXTERNAL_DS3231 && rtc) {
    rtc->adjust(DateTime(newTime));
  }

  // Сбрасываем счётчик прерываний
  portENTER_CRITICAL(&timerMux);
  portEXIT_CRITICAL(&timerMux);

  Serial.println("Время успешно обновлено");
  return true;
}

// Установка даты (день.месяц.год)
bool setManualDate(const String &dateStr) {

  int day, month, year;
  if (sscanf(dateStr.c_str(), "%d.%d.%d", &day, &month, &year) != 3) {
    Serial.println("Ошибка формата даты. Используйте DD.MM.YYYY");
    return false;
  }

  if (day < 1 || day > 31 || month < 1 || month > 12 || year < 2000 || year > 2100) {
    Serial.println("Ошибка: некорректная дата (допустимо 01.01.2000 - 31.12.2100)");
    return false;
  }

  // Получаем текущее время из системы
  struct tm timeinfo;
  time_t now;
  time(&now);
  localtime_r(&now, &timeinfo);

  // Проверяем валидность времени (новый способ)
  if (timeinfo.tm_hour < 0 || timeinfo.tm_hour > 23 ||
      timeinfo.tm_min < 0 || timeinfo.tm_min > 59 ||
      timeinfo.tm_sec < 0 || timeinfo.tm_sec > 59) {
    // Если время содержит недопустимые значения
    timeinfo.tm_hour = 9;
    timeinfo.tm_min = 0;
    timeinfo.tm_sec = 0;
    Serial.println("Обнаружены некорректные значения времени, установлено 09:00:00");
  }

  // Устанавливаем новую дату (время остаётся прежним)
  timeinfo.tm_mday = day;
  timeinfo.tm_mon = month - 1;  // январь = 0
  timeinfo.tm_year = year - 1900;
  time_t newTime = mktime(&timeinfo);

  // Применяем изменения
  struct timeval tv = { newTime, 0 };
  settimeofday(&tv, NULL);

  // Обновляем аппаратные часы (если подключены)
  if (currentTimeSource == EXTERNAL_DS3231 && rtc) {
    rtc->adjust(DateTime(newTime));
  }

  // Сбрасываем счётчик прерываний
  portENTER_CRITICAL(&timerMux);
  portEXIT_CRITICAL(&timerMux);

  Serial.println("Дата успешно обновлена");
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
void setDefaultTime() {
    Serial.println("Установка времени по умолчанию: 09:00 06.07.1990 (пятница)");
    
    struct tm default_time = {0};
    default_time.tm_year = 1990 - 1900; // 1990 год
    default_time.tm_mon = 7 - 1;        // Июль (месяцы 0-11)
    default_time.tm_mday = 6;           // 6 число
    default_time.tm_hour = 9;           // 9 часов
    default_time.tm_min = 0;            // 0 минут
    default_time.tm_sec = 0;            // 0 секунд
    default_time.tm_isdst = 0;          // Не летнее время
    
    // Устанавливаем день недели (пятница = 5 в C стандарте)
    default_time.tm_wday = calculateDayOfWeek(1990, 7, 6);
    
    time_t default_epoch = mktime(&default_time);
    
    // Устанавливаем системное время
    struct timeval tv = { default_epoch, 0 };
    settimeofday(&tv, NULL);
    
    // Если есть внешние часы, обновляем их тоже
    if (currentTimeSource == EXTERNAL_DS3231 && rtc) {
        rtc->adjust(DateTime(default_epoch));
        Serial.println("Время по умолчанию записано в DS3231");
    }
    
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

