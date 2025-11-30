#include "time_utils.h"
#include "config.h"
#include "dst_handler.h"
#include "hardware.h"

WiFiUDP ntpUDP;
NTPClient *timeClient;

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

bool syncTime() {
  digitalWrite(LED_PIN, HIGH); // Индикация начала синхронизации
  Serial.println("[NTP] Попытка синхронизации...");

  // Подключаемся к WiFi
  WiFi.mode(WIFI_STA);
  if (WiFi.begin(config.wifi_ssid, config.wifi_pass) != WL_CONNECTED) {
    for (int i = 0; i < 10 && WiFi.status() != WL_CONNECTED; i++) {
      delay(500);
      Serial.print(".");
    }
  }

  bool success = false;
  if (WiFi.status() == WL_CONNECTED) {
    timeClient->begin();
    timeClient->setTimeOffset(0); // Явно запрашиваем UTC

    if (timeClient->forceUpdate()) {
      time_t utcTime = timeClient->getEpochTime();
      
      setTimeZone(config.timezone_offset, config.dst_enabled, config.dst_preset_index);
      time_t localTime = utcTime + config.timezone_offset * 3600;
        if (config.dst_enabled) {
        localTime = mktime(localtime(&localTime)); // Автокоррекция DST
        }

      // Отладочный вывод (UTC + локальное время)
      struct tm *tm_utc = gmtime(&utcTime);
      struct tm *tm_local = localtime(&localTime);
      Serial.printf("[NTP] Получено UTC: %02d:%02d:%02d\n", 
                   tm_utc->tm_hour, tm_utc->tm_min, tm_utc->tm_sec);
      Serial.printf("[NTP] Локальное время: %02d:%02d:%02d (TZ=%+d, DST=%s)\n",
                   tm_local->tm_hour, tm_local->tm_min, tm_local->tm_sec,
                   config.timezone_offset, config.dst_enabled ? "ON" : "OFF");

      // Записываем время встроенного RTC
      struct timeval tv = { localTime, 0 };
      settimeofday(&tv, NULL);
      Serial.println("[RTC] Время записано во внутренний RTC");

      // Записываем в DS3231 (если подключён)
      if (currentTimeSource == EXTERNAL_DS3231 && rtc) {
        rtc->adjust(DateTime(localTime));
        Serial.println("[DS3231] Время записано в аппаратные часы");
      }

      success = true;
      digitalWrite(LED_PIN, LOW); // Успешное завершение
    }
    timeClient->end();
  }

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  if (!success) {
    blinkError(11); // Индикация ошибки
    Serial.println("[ERROR] Не удалось синхронизировать время!");
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
    timeinfo.tm_year = 123;    // 2023 год (123 = 2023-1900)
    timeinfo.tm_mon = 0;       // январь
    timeinfo.tm_mday = 1;      // 1 число
    Serial.println("Установлена дата по умолчанию: 01.01.2023");
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
    timeinfo.tm_hour = 0;
    timeinfo.tm_min = 0;
    timeinfo.tm_sec = 0;
    Serial.println("Обнаружены некорректные значения времени, установлено 00:00:00");
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