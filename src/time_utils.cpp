#include "time_utils.h"
#include "config.h"
#include "hardware.h"
#include "timezone_manager.h"
#include <ezTime.h>

extern WiFiUDP ntpUDP;         // Определен где-то еще (возможно в .ino)
extern NTPClient *timeClient;  // Определен в config.cpp
static bool printEnabled = false; // Флаг для управления выводом в Serial

// ===== ASYNC SYNC SUPPORT (FreeRTOS Task) =====
static volatile bool syncInProgress = false;
static bool syncLastResult = false;
static bool syncForceFlag = false;
static uint8_t syncPreferredNtpIndex = 0;
static bool syncAutoEnabledSnapshot = false;
static char syncSsid1[sizeof(config.wifi_ssid)] = {0};
static char syncPass1[sizeof(config.wifi_pass)] = {0};
static char syncSsid2[sizeof(config.wifi_ssid_2)] = {0};
static char syncPass2[sizeof(config.wifi_pass_2)] = {0};

// Основная функция проверки и инициализации источников времени
// Вызывается при старте и потом при каждом получении времени
void checkTimeSource() {
    static bool firstCheck = true;
    static bool firstRunMessage = true;
    static unsigned long lastProbeMillis = 0;
    
    if (firstCheck) {
        firstCheck = false;
        Wire.begin(I2C_SDA, I2C_SCL);
        Wire.setClock(100000);
        Serial.print("\n[SYSTEM] Инициализация I2C завершена\n");
    }
    
    // Не дёргаем I2C слишком часто, если DS3231 отсутствует
    unsigned long nowMillis = millis();
    if (!ds3231_available && (nowMillis - lastProbeMillis < 1000)) {
        return;
    }
    lastProbeMillis = nowMillis;

    Wire.beginTransmission(0x68);
    bool ds3231_now_available = (Wire.endTransmission() == 0);

    // Первичное решение источника на старте принимаем ПОСЛЕ probing I2C,
    // чтобы не уходить преждевременно во внутренний RTC, когда DS3231 доступен.
    if (firstRunMessage) {
        firstRunMessage = false;
        if (!ds3231_now_available) {
            currentTimeSource = INTERNAL_RTC;
            Serial.print("\n✓ Используются внутренние часы RTC");
            setDefaultTimeToAllSources();
            setupInterrupts();
        }
    }

    // Проверка OSF (только если DS3231 доступен)
    if (ds3231_now_available) {
        Wire.beginTransmission(0x68);
        Wire.write(0x0F);
        if (Wire.endTransmission() == 0) {
            Wire.requestFrom(0x68, 1);
            if (Wire.available()) {
                uint8_t status = Wire.read();
                if (status & 0x80) {
                    Serial.print("\n[DS3231] ⚠️ Флаг OSF установлен (питание пропадало)");
                    Wire.beginTransmission(0x68);
                    Wire.write(0x0F);
                    Wire.write(status & 0x7F);
                    Wire.endTransmission();
                    setDefaultTimeToAllSources(); // Устанавливаем время по умолчанию
                    Serial.print("\nПопытка синхронизировать время с NTP...");
                    syncTimeAsync(); // Пробуем синхронизировать время
                }
            }
        }
    }
    
    bool showConnectionMessage = false;
    time_t diff = 0;

    // Если DS3231 есть, но не инициализирован
    if (ds3231_now_available && !rtc) {
        rtc = new RTC_DS3231();
        if (rtc && rtc->begin()) {
            ds3231_available = true;
            currentTimeSource = EXTERNAL_DS3231;

            Serial.print("\n[DS3231] Инициализирован");
            
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
                Serial.print("\n[DS3231] Получено некорректное время");
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
                    Serial.print("\n[DS3231] Повторное подключение: время некорректно");
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
            Serial.print("\n\n[ERR] DS3231 отключен, использую System RTC");   
        }
        setupInterrupts();
    }

    // Выводим сообщение о подключении (если нужно)
    if (showConnectionMessage && ds3231_available) {
        if (diff > 5) {
            Serial.printf("\n[DS3231] -> [RTC] синхронизация (расхождение: %ld сек)", diff);
        } else {
            Serial.print("\n[DS3231] -> [RTC] время совпадает");
        }
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
    Serial.print("\n[RTC] обновлен");

    // 2. Если есть внешний RTC, обновляем и его
    if(ds3231_available && rtc) {
        DateTime dt = convertTimeTToDateTime(utcTime);
        rtc->adjust(dt);
        Serial.print("\n[DS3231] обновлен");
    }
    
   // Serial.print("Текущее время: ");
   // printTimeFromTimeT(utcTime);
}

static bool applyNtpTime(time_t utcTime, bool force, bool auto_sync_was_enabled) {
    // Проверяем, что время валидное (не 1970 год)
    // Используем порог: 2025-07-06 09:00:00 UTC (наше значение по умолчанию)
    if (utcTime <= 1751792400) { // После 2025-07-06 09:00 UTC
        Serial.print("\n[NTP] Ошибка: получено некорректное время");
        return false;
    }

    // Выводим полученное UTC время
    struct tm *tm_utc = gmtime(&utcTime);
    Serial.printf("\n[NTP] Получено UTC: %04d-%02d-%02d %02d:%02d:%02d", 
               tm_utc->tm_year + 1900, tm_utc->tm_mon + 1, tm_utc->tm_mday,
               tm_utc->tm_hour, tm_utc->tm_min, tm_utc->tm_sec);

    // Устанавливаем UTC время в систему
    struct timeval tv = { utcTime, 0 };
    settimeofday(&tv, NULL);
    Serial.print("\n[NTP] -> [RTC] Время записано во внутренний RTC");
    
    // Записываем в DS3231 ТОЖЕ UTC
    if (currentTimeSource == EXTERNAL_DS3231 && rtc) {
        DateTime rtcTime(utcTime); // Конструктор принимает time_t (UTC)
        rtc->adjust(rtcTime);
        Serial.print("\n[NTP] -> [DS3231] Время записано в аппаратные часы");
    }
    
    // Показываем информацию о режиме работы с часовыми поясами
    if (config.time_config.automatic_localtime) {
        Serial.print("\n[TZ] Автоматическое определение локального времени включено");
        Serial.printf("\n[TZ] Текущая географическая зона: %s", config.time_config.timezone_name);
        
        // Обновляем/инициализируем ezTime после подключения WiFi
        if (setTimezone(config.time_config.timezone_name)) {
            if (force && !auto_sync_was_enabled) {
                config.time_config.auto_sync_enabled = false;
            }
            for (int i = 0; i < 5; i++) {
                events();
                delay(200);
            }
        }
        
        // Получаем и выводим данные от ezTime
        int8_t eztime_offset = 0;
        bool eztime_dst = false;
        if (!getEzTimeData(utcTime, eztime_offset, eztime_dst)) {
            // Fallback: используем текущую логику конвертации (может быть таблица/офлайн POSIX)
            utcToLocal(utcTime);
            eztime_offset = config.time_config.current_offset;
            eztime_dst = config.time_config.current_dst_active;
        }
        
        Serial.printf("\n[TZ] - [ONLINE] Сравнение с интернет (ezTime): UTC%+d, DST: %s", 
                     eztime_offset,
                     eztime_dst ? "ON" : "OFF");
        
        // Сверяем с локальной таблицей
        const TimezonePreset* preset = findPresetByLocation(config.time_config.timezone_name);
        if (preset) {
            bool local_dst = calculateDSTStatus(utcTime, preset);
            int8_t local_offset = local_dst ? preset->dst_offset : preset->std_offset;

            bool current_match = (eztime_offset == local_offset && eztime_dst == local_dst);
            struct tm* utc_tm = gmtime(&utcTime);
            int year = utc_tm ? (utc_tm->tm_year + 1900) : 0;
            bool rules_match = (year > 0) ? compareDSTRulesWithEzTime(preset, year, 2, false) : true;

            if (current_match && rules_match) {
                Serial.print("\n[TZ] ✅ СОВПАДЕНИЕ - локальные правила актуальны");
                if (clearPosixOverrideIfZone(config.time_config.timezone_name)) {
                    saveConfig();
                }
            } else {
                Serial.print("\n[TZ] ⚠️  РАСХОЖДЕНИЕ! Требуется обновление локальных правил");
                if (!current_match) {
                    Serial.printf("\n[TZ]    ezTime: UTC%+d, DST: %s", eztime_offset, eztime_dst ? "ON" : "OFF");
                    Serial.printf("\n[TZ]    Таблица: UTC%+d, DST: %s", local_offset, local_dst ? "ON" : "OFF");
                }
                if (!rules_match) {
                    Serial.print("\n[TZ]    Переходы DST: РАСХОЖДЕНИЕ");
                }

                if (savePosixOverride(config.time_config.timezone_name)) {
                    saveConfig();
                    Serial.print("\n[TZ] 💾 POSIX правила сохранены для офлайн-работы");
                }
            }
        }
    } else {
        Serial.print("\n[TZ] Включено ручное определение локального времени.");
        Serial.printf("\n[TZ] Локация: %s (режим: табличные данные)", config.time_config.timezone_name);
        
        // Получаем данные из таблицы
        utcToLocal(utcTime);  // Это обновит current_offset и current_dst_active
        Serial.printf("\n[TZ] Данные из таблицы: UTC%+d, DST: %s", 
                     config.time_config.current_offset,
                     config.time_config.current_dst_active ? "ON" : "OFF");
    }
    
    // Обновляем время последней синхронизации в конфиге
    config.time_config.last_ntp_sync = utcTime;
    saveConfig();
    
    return true;
}

static const char* getNtpServerByIndex(uint8_t index) {
    switch (index) {
        case 1: return config.ntp_server_1;
        case 2: return config.ntp_server_2;
        case 3: return config.ntp_server_3;
        default: return nullptr;
    }
}

static bool trySyncWithNtpServers(bool force, bool auto_sync_was_enabled, uint8_t preferredIndex) {
    uint8_t order[3];
    uint8_t count = 0;

    if (preferredIndex >= 1 && preferredIndex <= 3) {
        order[count++] = preferredIndex;
    }
    for (uint8_t i = 1; i <= 3; ++i) {
        if (i == preferredIndex) {
            continue;
        }
        order[count++] = i;
    }

    for (uint8_t i = 0; i < count; ++i) {
        const char* server = getNtpServerByIndex(order[i]);
        if (!server || server[0] == '\0') {
            continue;
        }

        Serial.printf("\n[NTP] Используем сервер: %s", server);

        IPAddress resolvedIp;
        if (!WiFi.hostByName(server, resolvedIp)) {
            Serial.printf("[NTP] DNS ошибка для сервера: %s\n", server);
            continue;
        }

        try {
            NTPClient client(ntpUDP, resolvedIp, 0);
            client.begin();
            client.setTimeOffset(0); // Запрашиваем UTC
            
            if (client.forceUpdate()) {
                time_t utcTime = client.getEpochTime();
                if (applyNtpTime(utcTime, force, auto_sync_was_enabled)) {
                    client.end();
                    return true;
                }
            } else {
                Serial.print("\n[NTP] Ошибка: forceUpdate() не удался");
            }
            
            client.end();
        } catch (...) {
            Serial.print("\n[NTP] Исключение при синхронизации!");
        }
    }

    return false;
}

// ===== ASYNC SYNC (FreeRTOS Task) =====

bool isSyncInProgress() {
    return syncInProgress;
}

// Функция задачи FreeRTOS — полный цикл WiFi→NTP→отключение
static void wifiSyncTask(void* param) {
    bool success = false;
    uint8_t networkNum = 0;

    // --- Шаг 1: Инициализация WiFi ---
    // Даём другим задачам завершить текущие операции перед тяжёлой инициализацией
    delay(500);
    
    // Статические буферы WiFi — избегаем проблем с PSRAM (OPI)
    WiFi.useStaticBuffers(true);
    
    Serial.print("\n[WiFi] WiFi.mode(STA)...");
    WiFi.mode(WIFI_STA);
    delay(200);
    Serial.print("\n[WiFi] WiFi.STA: OK");

    // --- Шаг 2: Подключение к сети 1 ---
    Serial.print("\n[WiFi] Подключение к сети 1");
    WiFi.begin(syncSsid1, syncPass1);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(300);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        networkNum = 1;
        Serial.printf("\n[WiFi] Подключено к %s (сеть 1)", syncSsid1);
    } else {
        Serial.print("\n[WiFi] Не удалось подключиться к сети 1");
        
        // --- Шаг 3: Попытка сети 2 (если настроена) ---
        if (strlen(syncSsid2) > 0) {
            Serial.print("\n[WiFi] Подключение к сети 2");
            WiFi.disconnect(false);
            delay(100);
            WiFi.begin(syncSsid2, syncPass2);

            attempts = 0;
            while (WiFi.status() != WL_CONNECTED && attempts < 30) {
                delay(300);
                Serial.print(".");
                attempts++;
            }

            if (WiFi.status() == WL_CONNECTED) {
                networkNum = 2;
                Serial.printf("\n[WiFi] Подключено к %s (сеть 2)", syncSsid2);
            } else {
                Serial.print("\n[WiFi] Не удалось подключиться к сети 2");
            }
        }
    }

    // --- Шаг 4: NTP синхронизация ---
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\n[WiFi] IP: %s | RSSI: %d dBm",
                      WiFi.localIP().toString().c_str(), WiFi.RSSI());

        success = trySyncWithNtpServers(syncForceFlag, syncAutoEnabledSnapshot, syncPreferredNtpIndex);

        // Если не удалось через сеть 1 — попробовать через сеть 2
        if (!success && networkNum == 1 && strlen(syncSsid2) > 0) {
            Serial.print("\n[NTP] Попытка через сеть 2...");
            WiFi.disconnect(false);
            delay(100);
            WiFi.begin(syncSsid2, syncPass2);

            attempts = 0;
            while (WiFi.status() != WL_CONNECTED && attempts < 30) {
                delay(300);
                Serial.print(".");
                attempts++;
            }

            if (WiFi.status() == WL_CONNECTED) {
                success = trySyncWithNtpServers(syncForceFlag, syncAutoEnabledSnapshot, syncPreferredNtpIndex);
            }
        }
    }

    // --- Шаг 5: Завершение ---
    Serial.print("\n[WiFi] Отключение...");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    digitalWrite(LED_PIN, LOW);
    if (!success) {
        blinkError(3);
        Serial.print("\n[SYNC] Не удалось синхронизировать время!\n");
    } else {
        Serial.println("\n[SYNC] Синхронизация успешна!\n");
    }

    syncLastResult = success;
    syncInProgress = false;
    vTaskDelete(NULL);  // Самоудаление задачи
}

// Асинхронный запуск синхронизации (создаёт RTOS-задачу)
void syncTimeAsync(bool force, uint8_t preferredNtpIndex) {
    if (syncInProgress) {
        Serial.print("\n[SYNC] Синхронизация уже выполняется...");
        return;
    }
    
    // Проверка А: разрешена ли синхронизация
    if (!force && !config.time_config.auto_sync_enabled) {
        Serial.print("\n\n[SYNC] Автоматическая синхронизация отключена");
        Serial.print("\n[TZ] Будет использоваться табличный переход на летнее/зимнее время");
        if (config.time_config.automatic_localtime &&
            config.time_config.tz_posix[0] != '\0' &&
            strcmp(config.time_config.tz_posix_zone, config.time_config.timezone_name) == 0) {
            Serial.print("\n[TZ] Используются сохранённые POSIX правила (offline)");
        }
        return;
    }
    
    // Проверка Б: настроен ли WiFi
    if (strlen(config.wifi_ssid) == 0) {
        Serial.print("\n\n[SYNC] WiFi не настроен, автоматическая синхронизация невозможна");
        Serial.print("\n[TZ] Будет использоваться табличный переход на летнее/зимнее время");
        if (config.time_config.automatic_localtime &&
            config.time_config.tz_posix[0] != '\0' &&
            strcmp(config.time_config.tz_posix_zone, config.time_config.timezone_name) == 0) {
            Serial.print("\n[TZ] Используются сохранённые POSIX правила (offline)");
        }
        return;
    }
    
    // Проверка В: инициализирован ли timeClient
    if (!timeClient) {
        Serial.print("\n\n[SYNC] Ошибка: timeClient не инициализирован");
        return;
    }
    
    // Подготовка данных для задачи (копируем ДО создания задачи)
    syncForceFlag = force;
    syncPreferredNtpIndex = preferredNtpIndex;
    syncAutoEnabledSnapshot = config.time_config.auto_sync_enabled;
    strlcpy(syncSsid1, config.wifi_ssid, sizeof(syncSsid1));
    strlcpy(syncPass1, config.wifi_pass, sizeof(syncPass1));
    strlcpy(syncSsid2, config.wifi_ssid_2, sizeof(syncSsid2));
    strlcpy(syncPass2, config.wifi_pass_2, sizeof(syncPass2));
    syncLastResult = false;

    digitalWrite(LED_PIN, HIGH);

    syncInProgress = true;

    // Создаём RTOS-задачу на ядре 0 (вместе с WiFi системными задачами)
    // USB CDC отключен — ядро 0 свободно для WiFi
    // Arduino loop() работает на ядре 1
    BaseType_t result = xTaskCreatePinnedToCore(
        wifiSyncTask,       // Функция задачи
        "wifi_sync",        // Имя (для отладки)
        16384,              // Стек (16 КБ — WiFi + NTP нужен запас)
        NULL,               // Параметр
        1,                  // Приоритет (низкий, чтобы не мешать системе)
        NULL,               // Handle (не нужен)
        0                   // Ядро 0 (WiFi системные задачи здесь)
    );

    if (result != pdPASS) {
        Serial.print("\n[SYNC] ОШИБКА: не удалось создать задачу синхронизации времени");
        syncInProgress = false;
        digitalWrite(LED_PIN, LOW);
    } else {
        Serial.print("\n\n[SYNC] Синхронизация запущена в фоне (FreeRTOS Task)");
    }
}


bool printTime() {
    // Получаем время через getCurrentUTCTime() - она сама определит источник
    time_t utcTime = getCurrentUTCTime();
    
    if (utcTime > 0) {
        
        // Вывод локального времени
        time_t local = utcToLocal(utcTime);
        struct tm local_tm;
        gmtime_r(&local, &local_tm);
        char lbuf[128];
        strftime(lbuf, sizeof(lbuf), "\nTime: %a %d.%m.%Y %H:%M:%S", &local_tm);
        Serial.print(lbuf);
        
        // Timezone info
        Serial.printf(" (TZ: %s) UTC%+d", 
                     config.time_config.timezone_name,
                     config.time_config.current_offset);
        
        // DST info - показываем только если активен
        if (config.time_config.current_dst_active) {
            Serial.print(", DST ON");
        }

        // Вывод UTC времени
        struct tm utc_tm;
        gmtime_r(&utcTime, &utc_tm);
        
        char buf[128];
        strftime(buf, sizeof(buf), "\n UTC: %a %d.%m.%Y %H:%M:%S", &utc_tm);
        Serial.print(buf);

         // Показываем текущий источник
        if (currentTimeSource == EXTERNAL_DS3231 && ds3231_available) {
            Serial.print(" [DS3231]");
        } else {
            Serial.print(" [ESP32 RTC]");
        }

        

       

        return true;
    }
    
    Serial.print("\nОшибка получения времени");
    return false;
}

void printTimeFromTimeT(time_t utcTime) {
    // Простая заглушка
    if (utcTime > 0) {
        struct tm* tm_info = localtime(&utcTime);
        Serial.printf("\n%04d-%02d-%02d %02d:%02d:%02d",
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

    Serial.print("\n[SYSTEM] Устанавливаю время по умолчанию: 2025-07-06 09:00:00 UTC");
    // Устанавливаем во все источники
    setTimeToAllSources(default_time);
}

// Установка времени (часы:минуты:секунды)
bool setManualTime(const String &timeStr) {
    int hours, minutes, seconds;
    
    if (sscanf(timeStr.c_str(), "%d:%d:%d", &hours, &minutes, &seconds) != 3) {
        Serial.print("\nОшибка формата времени. Используйте HH:MM:SS\n");
        return false;
    }

    if (hours < 0 || hours > 23 || minutes < 0 || minutes > 59 || seconds < 0 || seconds > 59) {
        Serial.print("\nОшибка: некорректное время (00:00:00 - 23:59:59)\n");
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
        Serial.print("\n⚠️  Использую дату по умолчанию: 06.07.2025\n");
    }
    
    // 3. Меняем только время
    utc_time.tm_hour = hours;
    utc_time.tm_min = minutes;
    utc_time.tm_sec = seconds;
    
    // 4. Записываем время (UTC)
    time_t newTime_utc = mktime(&utc_time);
    
    if (newTime_utc == -1) {
        Serial.print("\nОшибка конвертации времени\n");
        return false;
    }
    
    // 5. Устанавливаем во все источники через единую функцию
    setTimeToAllSources(newTime_utc);
    
    Serial.printf("\n✅ Время установлено: %02d:%02d:%02d\n", hours, minutes, seconds);
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
        Serial.print("\nОшибка формата даты. Используйте DD.MM.YYYY\n");
        return false;
    }

    // Простая проверка даты
    if (!isValidDate(day, month, year)) {
        Serial.printf("\nОшибка: некорректная дата %02d.%02d.%04d", day, month, year);
        
        // Полезное сообщение для 29 февраля
        if (month == 2 && day == 29) {
            Serial.printf("\nГод %d не высокосный\n", year);
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
    
    Serial.printf("\n✅ Дата установлена: %02d.%02d.%04d\n", day, month, year);
    //printTime();
    return true;
}

// Установка времени по локальной временной зоне (конвертируем в UTC)
bool setManualLocalTime(const String &timeStr) {
    int hours, minutes, seconds;
    
    if (sscanf(timeStr.c_str(), "%d:%d:%d", &hours, &minutes, &seconds) != 3) {
        Serial.print("\nОшибка формата времени. Используйте HH:MM:SS\n");
        return false;
    }

    if (hours < 0 || hours > 23 || minutes < 0 || minutes > 59 || seconds < 0 || seconds > 59) {
        Serial.print("\nОшибка: некорректное время (00:00:00 - 23:59:59)\n");
        return false;
    }

    // 1. Получаем текущую дату в UTC
    time_t now_utc;
    time(&now_utc);
    
    // 2. Конвертируем в локальное время
    time_t now_local = utcToLocal(now_utc);
    struct tm local_time;
    gmtime_r(&now_local, &local_time);
    
    // 3. Если дата некорректна (< 2025), устанавливаем дефолтную (06.07.2025)
    if (local_time.tm_year + 1900 < 2025) {
        local_time.tm_year = 2025 - 1900;  // 2025 год
        local_time.tm_mon = 7 - 1;         // Июль
        local_time.tm_mday = 6;            // 6-е число
        Serial.print("\n⚠️  Использую дату по умолчанию: 06.07.2025\n");
    }
    
    // 4. Меняем только время (локальное)
    local_time.tm_hour = hours;
    local_time.tm_min = minutes;
    local_time.tm_sec = seconds;
    
    // 5. Создаём time_t из локального времени
    time_t newTime_local = mktime(&local_time);
    
    if (newTime_local == -1) {
        Serial.print("\nОшибка конвертации времени\n");
        return false;
    }
    
    // 6. Конвертируем локальное время в UTC
    time_t newTime_utc = localToUtc(newTime_local);
    
    // 7. Устанавливаем UTC время во все источники
    setTimeToAllSources(newTime_utc);
    
    Serial.printf("\n✅ Локальное время установлено: %02d:%02d:%02d\n", hours, minutes, seconds);
    Serial.printf("   (UTC время: ");
    struct tm utc_tm;
    gmtime_r(&newTime_utc, &utc_tm);
    Serial.printf("%02d:%02d:%02d)\n", utc_tm.tm_hour, utc_tm.tm_min, utc_tm.tm_sec);
    return true;
}

// Установка даты по локальной временной зоне (конвертируем в UTC)
bool setManualLocalDate(const String &dateStr) {
    int day, month, year;
    
    if (sscanf(dateStr.c_str(), "%d.%d.%d", &day, &month, &year) != 3) {
        Serial.print("\nОшибка формата даты. Используйте DD.MM.YYYY\n");
        return false;
    }

    // Проверка даты
    if (!isValidDate(day, month, year)) {
        Serial.printf("\nОшибка: некорректная дата %02d.%02d.%04d", day, month, year);
        
        if (month == 2 && day == 29) {
            Serial.printf("\nГод %d не высокосный\n", year);
        }
        return false;
    }

    // 1. Получаем текущее время в UTC
    time_t now_utc;
    time(&now_utc);
    
    // 2. Конвертируем в локальное время
    time_t now_local = utcToLocal(now_utc);
    struct tm local_time;
    gmtime_r(&now_local, &local_time);
    
    // 3. Меняем дату (сохраняем текущее время в локальной зоне)
    local_time.tm_mday = day;
    local_time.tm_mon = month - 1;  // Месяцы 0-11
    local_time.tm_year = year - 1900;
    
    // 4. Создаём time_t из локального времени
    time_t newTime_local = mktime(&local_time);
    
    // 5. Конвертируем локальное время в UTC
    time_t newTime_utc = localToUtc(newTime_local);
    
    // 6. Устанавливаем UTC время во все источники
    setTimeToAllSources(newTime_utc);
    
    Serial.printf("\n✅ Локальная дата установлена: %02d.%02d.%04d\n", day, month, year);
    struct tm utc_tm;
    gmtime_r(&newTime_utc, &utc_tm);
    Serial.printf("   (UTC дата: %02d.%02d.%04d)\n", 
                  utc_tm.tm_mday, utc_tm.tm_mon + 1, utc_tm.tm_year + 1900);
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


