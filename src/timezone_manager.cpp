#include "config.h"
#include "timezone_manager.h"
#include "menu_manager.h"
#include <string.h>
#include <ezTime.h>
#include <WiFi.h>

// ========== ТАБЛИЦА ЧАСОВЫХ ПОЯСОВ ==========
// Формат: {zone_name, display_name, std_offset, dst_offset, 
//          dst_start_month, dst_start_week, dst_start_dow, dst_start_hour,
//          dst_end_month, dst_end_week, dst_end_dow, dst_end_hour}
// Неделя: 1-4 = конкретная неделя, 5 = последняя неделя месяца
// День недели: 0=Воскресенье, 1=Понедельник, ..., 6=Суббота

static const TimezonePreset TIMEZONE_PRESETS[] = {
    // === ЕВРОПА ===
    // Приоритетные зоны
    {"CET", "Центральноевропейское (CET)", 1, 2, 3, 5, 0, 2, 10, 5, 0, 3},
    {"Europe/London", "Лондон (GMT/BST)", 0, 1, 3, 5, 0, 1, 10, 5, 0, 2},
    {"Europe/Warsaw", "Варшава (CET/CEST)", 1, 2, 3, 5, 0, 2, 10, 5, 0, 3},
    
    // Западноевропейское время (WET/WEST) - UTC+0/+1
    {"Europe/Lisbon", "Лиссабон (WET/WEST)", 0, 1, 3, 5, 0, 1, 10, 5, 0, 2},
    {"Europe/Dublin", "Дублин (GMT/IST)", 0, 1, 3, 5, 0, 1, 10, 5, 0, 2},
    
    // Центральноевропейское время (CET/CEST) - UTC+1/+2
    {"Europe/Berlin", "Берлин (CET/CEST)", 1, 2, 3, 5, 0, 2, 10, 5, 0, 3},
    {"Europe/Paris", "Париж (CET/CEST)", 1, 2, 3, 5, 0, 2, 10, 5, 0, 3},
    {"Europe/Rome", "Рим (CET/CEST)", 1, 2, 3, 5, 0, 2, 10, 5, 0, 3},
    {"Europe/Madrid", "Мадрид (CET/CEST)", 1, 2, 3, 5, 0, 2, 10, 5, 0, 3},
    {"Europe/Amsterdam", "Амстердам (CET/CEST)", 1, 2, 3, 5, 0, 2, 10, 5, 0, 3},
    {"Europe/Brussels", "Брюссель (CET/CEST)", 1, 2, 3, 5, 0, 2, 10, 5, 0, 3},
    {"Europe/Vienna", "Вена (CET/CEST)", 1, 2, 3, 5, 0, 2, 10, 5, 0, 3},
    {"Europe/Prague", "Прага (CET/CEST)", 1, 2, 3, 5, 0, 2, 10, 5, 0, 3},
    {"Europe/Stockholm", "Стокгольм (CET/CEST)", 1, 2, 3, 5, 0, 2, 10, 5, 0, 3},
    {"Europe/Oslo", "Осло (CET/CEST)", 1, 2, 3, 5, 0, 2, 10, 5, 0, 3},
    {"Europe/Copenhagen", "Копенгаген (CET/CEST)", 1, 2, 3, 5, 0, 2, 10, 5, 0, 3},
    {"Europe/Zurich", "Цюрих (CET/CEST)", 1, 2, 3, 5, 0, 2, 10, 5, 0, 3},
    
    // Восточноевропейское время (EET/EEST) - UTC+2/+3
    {"Europe/Kiev", "Київ (EET/EEST)", 2, 3, 3, 5, 0, 3, 10, 5, 0, 4},
    {"Europe/Bucharest", "Бухарест (EET/EEST)", 2, 3, 3, 5, 0, 3, 10, 5, 0, 4},
    {"Europe/Athens", "Афины (EET/EEST)", 2, 3, 3, 5, 0, 3, 10, 5, 0, 4},
    {"Europe/Helsinki", "Хельсинки (EET/EEST)", 2, 3, 3, 5, 0, 3, 10, 5, 0, 4},
    {"Europe/Sofia", "София (EET/EEST)", 2, 3, 3, 5, 0, 3, 10, 5, 0, 4},
    {"Europe/Riga", "Рига (EET/EEST)", 2, 3, 3, 5, 0, 3, 10, 5, 0, 4},
    {"Europe/Tallinn", "Таллинн (EET/EEST)", 2, 3, 3, 5, 0, 3, 10, 5, 0, 4},
    {"Europe/Vilnius", "Вильнюс (EET/EEST)", 2, 3, 3, 5, 0, 3, 10, 5, 0, 4},
    {"Europe/Istanbul", "Стамбул (TRT)", 3, 3, 0, 0, 0, 0, 0, 0, 0, 0},  // Без DST с 2016
    
    // === АЗИЯ ===
    {"Asia/Dubai", "Дубай (GST)", 4, 4, 0, 0, 0, 0, 0, 0, 0, 0},
    {"Asia/Tokyo", "Токио (JST)", 9, 9, 0, 0, 0, 0, 0, 0, 0, 0},
    {"Asia/Shanghai", "Шанхай (CST)", 8, 8, 0, 0, 0, 0, 0, 0, 0, 0},
    {"Asia/Singapore", "Сингапур (SGT)", 8, 8, 0, 0, 0, 0, 0, 0, 0, 0},
    {"Asia/Seoul", "Сеул (KST)", 9, 9, 0, 0, 0, 0, 0, 0, 0, 0},
    {"Asia/Bangkok", "Бангкок (ICT)", 7, 7, 0, 0, 0, 0, 0, 0, 0, 0},
    {"Asia/Jerusalem", "Иерусалим (IST/IDT)", 2, 3, 3, 5, 5, 2, 10, 5, 0, 2},
    
    // === АМЕРИКА ===
    {"America/New_York", "Нью-Йорк (EST/EDT)", -5, -4, 3, 2, 0, 2, 11, 1, 0, 2},  // EST → EDT
    {"America/Chicago", "Чикаго (CST/CDT)", -6, -5, 3, 2, 0, 2, 11, 1, 0, 2},  // CST → CDT
    {"America/Denver", "Денвер (MST/MDT)", -7, -6, 3, 2, 0, 2, 11, 1, 0, 2},  // MST → MDT
    {"America/Los_Angeles", "Лос-Анджелес (PST/PDT)", -8, -7, 3, 2, 0, 2, 11, 1, 0, 2},  // PST → PDT
    {"America/Anchorage", "Анкоридж (AKST/AKDT)", -9, -8, 3, 2, 0, 2, 11, 1, 0, 2},  // AKST → AKDT
    {"America/Sao_Paulo", "Сан-Паулу (BRT/BRST)", -3, -2, 10, 3, 0, 0, 2, 3, 0, 0},  // BRT → BRST
    
    // === АВСТРАЛИЯ ===
    {"Australia/Sydney", "Сидней (AEDT/AEST)", 10, 11, 10, 1, 0, 2, 4, 1, 0, 3},  // AEST → AEDT
    {"Australia/Perth", "Перт (AWST)", 8, 8, 0, 0, 0, 0, 0, 0, 0, 0},  // Без DST
    
    // === ДРУГИЕ ===
    {"Pacific/Auckland", "Окленд (NZST/NZDT)", 12, 13, 9, 5, 0, 2, 4, 1, 0, 3},  // NZST → NZDT
};

static const uint8_t PRESETS_COUNT = sizeof(TIMEZONE_PRESETS) / sizeof(TimezonePreset);

// Состояние ezTime
static Timezone localTZ;
static bool ezTimeAvailable = false;

// Ручной preset (для опции 100)
static char manual_zone_name[] = "MANUAL";
static char manual_display_name[] = "Ручная настройка";
static TimezonePreset manualPreset = {
    manual_zone_name,
    manual_display_name,
    0, 0,  // std_offset, dst_offset - будут установлены пользователем
    0, 0, 0, 0,  // DST start
    0, 0, 0, 0   // DST end
};
static bool manualPresetActive = false;

// ========== ФУНКЦИИ ПОИСКА ПРЕСЕТОВ ==========

const TimezonePreset* findPresetByLocation(const char* location) {
    if (!location) return nullptr;
    
    // Проверяем, это ручная настройка
    if (strcmp(location, "MANUAL") == 0 && manualPresetActive) {
        return &manualPreset;
    }
    
    for (uint8_t i = 0; i < PRESETS_COUNT; i++) {
        if (strcmp(TIMEZONE_PRESETS[i].zone_name, location) == 0) {
            return &TIMEZONE_PRESETS[i];
        }
    }
    return nullptr;
}

const TimezonePreset* getPresetByIndex(uint8_t index) {
    if (index >= PRESETS_COUNT) return nullptr;
    return &TIMEZONE_PRESETS[index];
}

uint8_t getPresetsCount() {
    return PRESETS_COUNT;
}

// ========== ВЫЧИСЛЕНИЕ DST ==========

// Вычисляет timestamp перехода на/с DST
time_t calculateDSTTransition(int year, uint8_t month, uint8_t week, uint8_t dow, uint8_t hour, int8_t offset) {
    if (month == 0) return 0;  // Нет DST
    
    // Создаём struct tm для первого дня указанного месяца
    struct tm tm_time = {0};
    tm_time.tm_year = year - 1900;
    tm_time.tm_mon = month - 1;
    tm_time.tm_mday = 1;
    tm_time.tm_hour = hour;
    tm_time.tm_min = 0;
    tm_time.tm_sec = 0;
    tm_time.tm_isdst = -1;
    
    time_t first_day = mktime(&tm_time);
    struct tm* first = gmtime(&first_day);
    
    // Определяем день недели первого дня месяца (0=Sun, 1=Mon, ...)
    int first_dow = first->tm_wday;
    
    // Вычисляем первое вхождение нужного дня недели
    int days_until_target = (dow - first_dow + 7) % 7;
    int target_day = 1 + days_until_target;
    
    // Если нужна конкретная неделя (1-4)
    if (week <= 4) {
        target_day += (week - 1) * 7;
    } else {
        // week == 5 означает "последняя неделя месяца"
        // Находим последнее вхождение дня недели
        target_day += 3 * 7;  // Начинаем с 4-й недели
        
        // Проверяем, есть ли ещё одна неделя
        tm_time.tm_mday = target_day + 7;
        time_t test_time = mktime(&tm_time);
        struct tm* test = gmtime(&test_time);
        
        if (test->tm_mon == month - 1) {
            target_day += 7;  // Да, есть 5-я неделя
        }
    }
    
    // Создаём финальный timestamp
    tm_time.tm_mday = target_day;
    tm_time.tm_hour = hour;
    
    // Применяем смещение часового пояса (переход происходит в местном времени)
    time_t transition = mktime(&tm_time) - (offset * 3600);
    
    return transition;
}

// Проверяет, активен ли DST для заданного UTC времени
bool calculateDSTStatus(time_t utc, const TimezonePreset* preset) {
    if (!preset) return false;
    
    // Если DST смещение равно стандартному - DST не используется
    if (preset->dst_offset == preset->std_offset || preset->dst_start_month == 0) {
        return false;
    }
    
    struct tm* tm_utc = gmtime(&utc);
    int year = tm_utc->tm_year + 1900;
    
    // Вычисляем точки перехода для текущего года
    time_t dst_start = calculateDSTTransition(year, preset->dst_start_month, preset->dst_start_week,
                                               preset->dst_start_dow, preset->dst_start_hour, preset->std_offset);
    time_t dst_end = calculateDSTTransition(year, preset->dst_end_month, preset->dst_end_week,
                                             preset->dst_end_dow, preset->dst_end_hour, preset->dst_offset);
    
    // Для южного полушария (dst_end < dst_start)
    if (dst_end < dst_start) {
        return (utc >= dst_start || utc < dst_end);
    }
    
    // Для северного полушария
    return (utc >= dst_start && utc < dst_end);
}

// ========== ИНИЦИАЛИЗАЦИЯ ==========

bool initTimezone() {
    // Настраиваем ezTime NTP сервер и интервал
    setServer(String(config.ntp_server));
    setInterval(config.time_config.sync_interval_hours * 3600);
    
    // Проверяем, что локация задана
    if (config.time_config.timezone_name[0] == '\0') {
        // Устанавливаем по умолчанию
        strncpy(config.time_config.timezone_name, DEFAULT_TIMEZONE_NAME, sizeof(config.time_config.timezone_name));
        config.time_config.timezone_name[sizeof(config.time_config.timezone_name)-1] = '\0';
    }
    
    // Проверяем, это ручная настройка?
    if (strcmp(config.time_config.timezone_name, "MANUAL") == 0) {
        // Восстанавливаем manual preset из config
        manualPreset.std_offset = config.time_config.manual_std_offset;
        manualPreset.dst_offset = config.time_config.manual_dst_offset;
        manualPreset.dst_start_month = config.time_config.manual_dst_start_month;
        manualPreset.dst_start_week = config.time_config.manual_dst_start_week;
        manualPreset.dst_start_dow = config.time_config.manual_dst_start_dow;
        manualPreset.dst_start_hour = config.time_config.manual_dst_start_hour;
        manualPreset.dst_end_month = config.time_config.manual_dst_end_month;
        manualPreset.dst_end_week = config.time_config.manual_dst_end_week;
        manualPreset.dst_end_dow = config.time_config.manual_dst_end_dow;
        manualPreset.dst_end_hour = config.time_config.manual_dst_end_hour;
        manualPresetActive = true;
        
        Serial.printf("\n[TZ] Инициализация: Ручная настройка (UTC%+d)", manualPreset.std_offset);
        if (manualPreset.dst_start_month > 0) {
            Serial.printf(" / DST: UTC%+d", manualPreset.dst_offset);
        }
        Serial.print("\n[TZ] Режим: Ручная настройка (offline)");
        return true;
    }
    
    // Находим preset для этой локации
    const TimezonePreset* preset = findPresetByLocation(config.time_config.timezone_name);
    if (!preset) {
        Serial.printf("\n[TZ] Предупреждение: локация '%s' не найдена в таблице\n", config.time_config.timezone_name);
        return false;
    }
    
    Serial.printf("\n[TZ] Инициализация: %s", preset->display_name);
    
    // Если включён режим автоматического локального времени - пробуем загрузить ezTime
    if (config.time_config.automatic_localtime) {
        if (localTZ.setLocation(String(config.time_config.timezone_name))) {
            localTZ.setDefault();
            ezTimeAvailable = true;
            Serial.print("\n[TZ] Режим: ezTime (online с правилами DST\n)");
            return true;
        } else {
            Serial.print("\n[TZ] ezTime недоступен, переключаюсь на локальную таблицу");
            ezTimeAvailable = false;
        }
    } else {
        ezTimeAvailable = false;
        Serial.print("\n[TZ] Режим: Локальная таблица (offline)");
    }
    
    return true;
}

// ========== КОНВЕРТАЦИЯ ВРЕМЕНИ ==========

time_t utcToLocal(time_t utc) {
    // === РЕЖИМ 1: ezTime (online с автоматическими правилами DST) ===
    if (config.time_config.automatic_localtime && ezTimeAvailable) {
        String tzname;
        bool isdst = false;
        int16_t offset_minutes = 0;
        
        // Пытаемся получить время через ezTime
        time_t local = localTZ.tzTime(utc, UTC_TIME, tzname, isdst, offset_minutes);
        
        // ПРОВЕРКА: если ezTime вернул некорректное значение (нет связи с сервером)
        // Признак: offset_minutes == 0 и tzname пустой, или local == utc для зон с ненулевым offset
        bool eztime_failed = false;
        
        // Находим локальную таблицу для сравнения
        const TimezonePreset* preset = findPresetByLocation(config.time_config.timezone_name);
        
        if (preset) {
            // Если ezTime вернул нулевое смещение для зоны, где должно быть ненулевое
            if (offset_minutes == 0 && (preset->std_offset != 0 || preset->dst_offset != 0)) {
                eztime_failed = true;
            }
            // Или если смещение сильно отличается от ожидаемого (больше чем на 2 часа)
            else if (abs((offset_minutes / 60) - preset->std_offset) > 2 && 
                     abs((offset_minutes / 60) - preset->dst_offset) > 2) {
                eztime_failed = true;
            }
        }
        
        // Если ezTime работает нормально - используем его результат
        if (!eztime_failed) {
            // Обновляем кэшированные значения для отображения
            config.time_config.current_offset = offset_minutes / 60;
            config.time_config.current_dst_active = isdst;
            
            return local;
        }
        
        // ezTime недоступен - переключаемся на локальную таблицу
        Serial.print("\n[TZ] ⚠️  ezTime недоступен (нет интернета?), переключение на локальную таблицу");
    }
    
    // === РЕЖИМ 2: Локальная таблица (offline) или Fallback ===
    
    // Находим preset для текущей локации
    const TimezonePreset* preset = findPresetByLocation(config.time_config.timezone_name);
    if (!preset) {
        // Проверяем, это ли Manual preset, который не был активирован
        if (strcmp(config.time_config.timezone_name, "MANUAL") == 0) {
            // Пытаемся восстановить Manual preset из config
            if (config.time_config.manual_std_offset != 0 || config.time_config.manual_dst_offset != 0) {
                // Есть сохраненные данные - активируем preset
                manualPreset.std_offset = config.time_config.manual_std_offset;
                manualPreset.dst_offset = config.time_config.manual_dst_offset;
                manualPreset.dst_start_month = config.time_config.manual_dst_start_month;
                manualPreset.dst_start_week = config.time_config.manual_dst_start_week;
                manualPreset.dst_start_dow = config.time_config.manual_dst_start_dow;
                manualPreset.dst_start_hour = config.time_config.manual_dst_start_hour;
                manualPreset.dst_end_month = config.time_config.manual_dst_end_month;
                manualPreset.dst_end_week = config.time_config.manual_dst_end_week;
                manualPreset.dst_end_dow = config.time_config.manual_dst_end_dow;
                manualPreset.dst_end_hour = config.time_config.manual_dst_end_hour;
                manualPresetActive = true;
                preset = &manualPreset;  // Используем восстановленный preset
            }
        }
        
        if (!preset) {
            // Fallback: возвращаем UTC без изменений
            Serial.print("\n[TZ] Ошибка: preset не найден, используется UTC");
            config.time_config.current_offset = 0;
            config.time_config.current_dst_active = false;
            return utc;
        }
    }
    
    // Вычисляем, активен ли DST сейчас (по локальным правилам)
    bool is_dst = calculateDSTStatus(utc, preset);
    
    // Выбираем соответствующее смещение
    int8_t offset = is_dst ? preset->dst_offset : preset->std_offset;
    
    // Обновляем кэшированные значения
    config.time_config.current_offset = offset;
    config.time_config.current_dst_active = is_dst;
    
    // Применяем смещение
    return utc + (offset * 3600);
}

time_t localToUtc(time_t local) {
    // === РЕЖИМ 1: ezTime (online) ===
    if (config.time_config.automatic_localtime && ezTimeAvailable) {
        String tzname;
        bool isdst = false;
        int16_t offset_minutes = 0;
        
        time_t utc = localTZ.tzTime(local, LOCAL_TIME, tzname, isdst, offset_minutes);
        
        // ПРОВЕРКА: если ezTime вернул некорректное значение
        bool eztime_failed = false;
        
        const TimezonePreset* preset = findPresetByLocation(config.time_config.timezone_name);
        if (preset) {
            if (offset_minutes == 0 && (preset->std_offset != 0 || preset->dst_offset != 0)) {
                eztime_failed = true;
            }
            else if (abs((offset_minutes / 60) - preset->std_offset) > 2 && 
                     abs((offset_minutes / 60) - preset->dst_offset) > 2) {
                eztime_failed = true;
            }
        }
        
        // Если ezTime работает - возвращаем результат
        if (!eztime_failed) {
            return utc;
        }
        
        Serial.print("\n[TZ] ⚠️  ezTime недоступен, fallback на локальную таблицу");
    }
    
    // === РЕЖИМ 2: Локальная таблица (offline) или Fallback ===
    
    const TimezonePreset* preset = findPresetByLocation(config.time_config.timezone_name);
    if (!preset) {
        // Проверяем, это ли Manual preset, который не был активирован
        if (strcmp(config.time_config.timezone_name, "MANUAL") == 0) {
            // Пытаемся восстановить Manual preset из config
            if (config.time_config.manual_std_offset != 0 || config.time_config.manual_dst_offset != 0) {
                // Есть сохраненные данные - активируем preset
                manualPreset.std_offset = config.time_config.manual_std_offset;
                manualPreset.dst_offset = config.time_config.manual_dst_offset;
                manualPreset.dst_start_month = config.time_config.manual_dst_start_month;
                manualPreset.dst_start_week = config.time_config.manual_dst_start_week;
                manualPreset.dst_start_dow = config.time_config.manual_dst_start_dow;
                manualPreset.dst_start_hour = config.time_config.manual_dst_start_hour;
                manualPreset.dst_end_month = config.time_config.manual_dst_end_month;
                manualPreset.dst_end_week = config.time_config.manual_dst_end_week;
                manualPreset.dst_end_dow = config.time_config.manual_dst_end_dow;
                manualPreset.dst_end_hour = config.time_config.manual_dst_end_hour;
                manualPresetActive = true;
                preset = &manualPreset;  // Используем восстановленный preset
            }
        }
        
        if (!preset) {
            return local;  // Fallback
        }
    }
    
    // Примерная оценка: используем текущее кэшированное значение DST
    // (точное вычисление требует итеративного подхода)
    int8_t offset = config.time_config.current_dst_active ? preset->dst_offset : preset->std_offset;
    
    return local - (offset * 3600);
}

// ========== УСТАНОВКА ЧАСОВОГО ПОЯСА ==========

bool setTimezone(const char* tz_name) {
    if (!tz_name) return false;
    
    // Проверяем, есть ли этот пояс в таблице
    const TimezonePreset* preset = findPresetByLocation(tz_name);
    if (!preset) {
        Serial.printf("\n[TZ] Ошибка: локация '%s' не найдена в таблице", tz_name);
        return false;
    }
    
    // Сохраняем имя локации
    strncpy(config.time_config.timezone_name, tz_name, sizeof(config.time_config.timezone_name));
    config.time_config.timezone_name[sizeof(config.time_config.timezone_name)-1] = '\0';
    
    Serial.printf("\n[TZ] Установлена локация: %s", preset->display_name);
    
    // Если включён режим automatic_localtime - пробуем загрузить ezTime
    if (config.time_config.automatic_localtime) {
        if (localTZ.setLocation(String(tz_name))) {
            localTZ.setDefault();
            ezTimeAvailable = true;
            Serial.print(" → ezTime загружен успешно");
            
            // Принудительно обновляем current_offset через utcToLocal
            time_t now = time(nullptr);
            utcToLocal(now);
            
            return true;
        } else {
            Serial.print(" → ezTime недоступен, используется таблица");
            ezTimeAvailable = false;
        }
    } else {
        ezTimeAvailable = false;
        Serial.printf(" → Таблица: UTC%+d (std), UTC%+d (DST)", preset->std_offset, preset->dst_offset);
    }
    
    // Принудительно обновляем current_offset через utcToLocal (для режима таблицы)
    time_t now = time(nullptr);
    utcToLocal(now);
    
    return true;
}

bool setTimezoneOffset(int8_t offset) {
    // Эта функция оставлена для совместимости, но не рекомендуется
    // Лучше использовать setTimezone() с конкретной локацией
    Serial.printf("\n[TZ] Предупреждение: setTimezoneOffset устарела, используйте setTimezone()");
    Serial.printf("\n[TZ] Ручное смещение UTC%+d установлено", offset);
    
    // Отключаем ezTime и автоматический режим
    ezTimeAvailable = false;
    config.time_config.automatic_localtime = false;
    config.time_config.current_offset = offset;
    config.time_config.current_dst_active = false;
    
    return true;
}

// ========== ФУНКЦИИ ОТЛАДКИ ==========

void printTimezoneInfo() {
    Serial.print("\n╔═══════════════════════════════════════════════════════");
    Serial.print("\n║           ИНФОРМАЦИЯ О ЧАСОВОМ ПОЯСЕ");
    Serial.print("\n╠═══════════════════════════════════════════════════════");
    
    // Текущая локация
    const TimezonePreset* preset = findPresetByLocation(config.time_config.timezone_name);
    if (preset) {
        Serial.printf("\n║ Time zone: %s", preset->zone_name);
        Serial.printf("\n║ Базовое смещение: UTC%+d", preset->std_offset);
        
        // Информация о DST
        if (preset->dst_start_month > 0 && preset->dst_offset != preset->std_offset) {
            Serial.print("\n║ Переход на летнее/зимнее время: используется");
            
            // Показываем текущее время (летнее/зимнее)
            if (config.time_config.current_dst_active) {
                Serial.printf("\n║ Текущее время: летнее (UTC%+d)", config.time_config.current_offset);
            } else {
                Serial.printf("\n║ Текущее время: зимнее (UTC%+d)", config.time_config.current_offset);
            }
        } else {
            Serial.print("\n║ Переход на летнее/зимнее время: не используется");
        }
    } else {
        // Проверяем, это ручная настройка или действительно ошибка
        if (strcmp(config.time_config.timezone_name, "MANUAL") == 0) {
            Serial.print("\n║ Time zone: Ручная настройка");
            Serial.printf("\n║ Текущее смещение: UTC%+d", config.time_config.current_offset);
            if (config.time_config.current_dst_active) {
                Serial.print("\n║ DST: Активен (летнее время)");
            } else {
                Serial.print("\n║ DST: Неактивен (стандартное время)");
            }
        } else {
            Serial.printf("\n║ Time zone: %s", config.time_config.timezone_name);
            Serial.print("\n║ ОШИБКА: Локация не найдена в таблице");
        }
    }
    
    Serial.print("\n╚═══════════════════════════════════════════════════════\n");
}

void listAvailableTimezones() {
    Serial.print("\n╔═════════════════════════════════════════════════════════════════╗");
    Serial.print("\n║                    ДОСТУПНЫЕ ЧАСОВЫЕ ПОЯСА                      ║");
    Serial.print("\n╠════╦═══════════════════════════════════════╦═════════════╦══════╣");
    Serial.print("\n║ #  ║ Локация                               ║ Смещение    ║  DST ║");
    Serial.print("\n╠════╬═══════════════════════════════════════╬═════════════╬══════╣");
    
    // Отслеживаем регионы для разделителей
    const char* current_region = "";
    
    for (uint8_t i = 0; i < PRESETS_COUNT; i++) {
        const TimezonePreset* p = &TIMEZONE_PRESETS[i];
        
        // Определяем регион по zone_name
        const char* new_region = "";
        if (strncmp(p->zone_name, "Europe/", 7) == 0 || strcmp(p->zone_name, "CET") == 0) {
            new_region = "ЕВРОПА";
        } else if (strncmp(p->zone_name, "Asia/", 5) == 0) {
            new_region = "АЗИЯ";
        } else if (strncmp(p->zone_name, "America/", 8) == 0) {
            new_region = "АМЕРИКА";
        } else if (strncmp(p->zone_name, "Australia/", 10) == 0) {
            new_region = "АВСТРАЛИЯ";
        } else if (strncmp(p->zone_name, "Pacific/", 8) == 0) {
            new_region = "ОКЕАНИЯ";
        }
        
        // Печатаем разделитель, если регион изменился
        if (strcmp(current_region, new_region) != 0) {
            if (i > 0) {
                Serial.print("\n╠════╬═══════════════════════════════════════╬═════════════╬══════╣");
            }
            
            // Вычисляем визуальную длину названия региона
            int region_visual_len = 0;
            for (const char* s = new_region; *s; s++) {
                if ((*s & 0xC0) != 0x80) region_visual_len++;
            }
            int region_padding = 38 - region_visual_len;
            if (region_padding < 0) region_padding = 0;
            
            // Выводим разделитель региона с правильным выравниванием
            Serial.print("\n║    ║ ");
            Serial.print(new_region);
            for (int j = 0; j < region_padding; j++) Serial.print(" ");
            Serial.print("║             ║      ║");
            
            Serial.print("\n╠════╬═══════════════════════════════════════╬═════════════╬══════╣");
            current_region = new_region;
        }
        
        // Форматируем данные
        char offset_str[12];
        const char* dst_str;
        
        if (p->dst_start_month > 0 && p->dst_offset != p->std_offset) {
            sprintf(offset_str, "UTC%+d/%+d", p->std_offset, p->dst_offset);
            dst_str = "Есть";
        } else {
            sprintf(offset_str, "UTC%+d", p->std_offset);
            dst_str = "Нет ";
        }
        
        // Вычисляем визуальную длину строки (количество символов, а не байтов)
        int visual_len = 0;
        for (const char* s = p->display_name; *s; s++) {
            if ((*s & 0xC0) != 0x80) visual_len++;  // Пропускаем continuation байты UTF-8
        }
        
        // Вычисляем количество пробелов для выравнивания (целевая ширина 37 символов)
        int padding = 37 - visual_len;
        if (padding < 0) padding = 0;
        
        // Выводим строку с правильным количеством пробелов
        Serial.printf("\n║ %-2d ║ %s", i + 1, p->display_name);
        for (int j = 0; j < padding; j++) Serial.print(" ");
        Serial.printf(" ║ %-11s ║ %-5s ║", offset_str, dst_str);
    }
    
    Serial.print("\n╚════╩═══════════════════════════════════════╩═════════════╩══════╝");
  Serial.print("\n\n╭─────────────────────────────────────────────────────────────────╮");
    Serial.print("\n│  100 - РУЧНАЯ НАСТРОЙКА СМЕЩЕНИЯ (без зоны из списка)           │");
    Serial.print("\n╰─────────────────────────────────────────────────────────────────╯");
    Serial.print("\n\nДля выбора локации часовой зоны введите её цифровое обозначение\n");
    printMappingMenuCommands();
    Serial.print("> ");
}

// ========== РУЧНАЯ НАСТРОЙКА СМЕЩЕНИЯ ==========

void setupManualOffset() {
    Serial.print("\n╭────────────────────────────────────────────────────────╮");
    Serial.print("\n│            РУЧНАЯ НАСТРОЙКА СМЕЩЕНИЯ ОТ UTC            │");
    Serial.print("\n╰────────────────────────────────────────────────────────╯");
    
    Serial.print("\n\nВ этом режиме вы можете задать произвольное смещение от UTC.");
    Serial.print("\nАвтоматическое определение часового пояса будет ОТКЛЮЧЕНО.\n");
    
    // Отключаем автоматическое определение
    config.time_config.automatic_localtime = false;
    
    // Шаг 1: Ввод смещения
    Serial.print("\nШаг 1/2: Введите смещение от UTC в часах (от -12 до +14)");
    Serial.print("\nПримеры: +3, -5, +5.5, 0");
    Serial.print("\n> ");
    
    // Ожидаем ввод
    while (!Serial.available()) { delay(100); }
    String offsetStr = Serial.readStringUntil('\n');
    offsetStr.trim();
    
    // Проверяем формат
    float offset = offsetStr.toFloat();
    if (offset < -12.0 || offset > 14.0) {
        Serial.print("\n✖ Ошибка: Смещение должно быть от -12 до +14 часов\n");
        return;
    }
    
    int8_t offset_hours = (int8_t)offset;
    Serial.printf("\n✓ Смещение установлено: UTC%+d\n", offset_hours);
    
    // Шаг 2: DST
    Serial.print("\nШаг 2/2: Нужен переход на летнее/зимнее время (DST)?");
    Serial.print("\nВведите 'yes' или 'no': ");
    
    while (!Serial.available()) { delay(100); }
    String dstChoice = Serial.readStringUntil('\n');
    dstChoice.trim();
    dstChoice.toLowerCase();
    
    // Используем глобальный preset
    manualPreset.std_offset = offset_hours;
    
    if (dstChoice == "yes" || dstChoice == "y") {
      Serial.print("\n\n╭────────────────────────────────────────────────────────╮");
        Serial.print("\n│       НАСТРОЙКА ПЕРЕХОДА НА ЛЕТНЕЕ/ЗИМНЕЕ ВРЕМЯ        │");
        Serial.print("\n╰────────────────────────────────────────────────────────╯");
        
        Serial.printf("\n\nДобавка к стандартному времени при DST (обычно +1):");
        Serial.printf("\n(Текущее стандартное: UTC%+d, DST будет: UTC%+d + добавка)", offset_hours, offset_hours);
        Serial.print("\nВведите добавку в часах: ");
        while (!Serial.available()) { delay(100); }
        String dstAddStr = Serial.readStringUntil('\n');
        dstAddStr.trim();
        int8_t dst_add = (int8_t)dstAddStr.toInt();
        
        // Вычисляем итоговое DST смещение
        int8_t dst_offset = offset_hours + dst_add;
        
        if (dst_offset < -12 || dst_offset > 14) {
            Serial.printf("\n✖ Ошибка: Итоговое DST смещение UTC%+d выходит за допустимый диапазон (-12..+14)\n", dst_offset);
            return;
        }
        manualPreset.dst_offset = dst_offset;
        
        // Простые настройки для Европейских правил
        Serial.print("\n\nИспользовать стандартные европейские правила DST?");
        Serial.print("\n(Последнее воскресенье марта 02:00 -> последнее воскресенье октября 03:00)");
        Serial.print("\nВведите 'yes' для стандартных правил, 'no' для ручной настройки: ");
        
        while (!Serial.available()) { delay(100); }
        String euroChoice = Serial.readStringUntil('\n');
        euroChoice.trim();
        euroChoice.toLowerCase();
        
        if (euroChoice == "yes" || euroChoice == "y") {
            // Стандартные европейские правила
            manualPreset.dst_start_month = 3;    // Март
            manualPreset.dst_start_week = 5;     // Последняя неделя
            manualPreset.dst_start_dow = 0;      // Воскресенье
            manualPreset.dst_start_hour = 2;     // 02:00
            manualPreset.dst_end_month = 10;     // Октябрь
            manualPreset.dst_end_week = 5;       // Последняя неделя
            manualPreset.dst_end_dow = 0;        // Воскресенье
            manualPreset.dst_end_hour = 3;       // 03:00
            
            Serial.print("\n✓ Используются стандартные европейские правила DST\n");
        } else {
            Serial.print("\n\n╭─ РУЧНАЯ НАСТРОЙКА DST ───────────────────────────────────────╮");
            Serial.print("\n│                                                              │");
            Serial.print("\n│ НАЧАЛО DST (переход на летнее время):                        │");
            Serial.print("\n│                                                              │");
            Serial.print("\n╰──────────────────────────────────────────────────────────────╯");
            
            // Запрашиваем параметры начала DST с валидацией и повторным вводом
            do {
                Serial.print("\nМесяц (1-12): ");
                while (!Serial.available()) { delay(100); }
                manualPreset.dst_start_month = Serial.readStringUntil('\n').toInt();
                if (manualPreset.dst_start_month < 1 || manualPreset.dst_start_month > 12) {
                    Serial.print("✖ Ошибка: Месяц должен быть от 1 до 12. Попробуйте снова.\n");
                }
            } while (manualPreset.dst_start_month < 1 || manualPreset.dst_start_month > 12);
            
            do {
                Serial.print("Неделя (1-4, 5=последняя): ");
                while (!Serial.available()) { delay(100); }
                manualPreset.dst_start_week = Serial.readStringUntil('\n').toInt();
                if (manualPreset.dst_start_week < 1 || manualPreset.dst_start_week > 5) {
                    Serial.print("✖ Ошибка: Неделя должна быть от 1 до 5. Попробуйте снова.\n");
                }
            } while (manualPreset.dst_start_week < 1 || manualPreset.dst_start_week > 5);
            
            do {
                Serial.print("День недели (0=Вск, 1=Пнд, ..., 6=Сбт): ");
                while (!Serial.available()) { delay(100); }
                manualPreset.dst_start_dow = Serial.readStringUntil('\n').toInt();
                if (manualPreset.dst_start_dow < 0 || manualPreset.dst_start_dow > 6) {
                    Serial.print("✖ Ошибка: День недели должен быть от 0 до 6. Попробуйте снова.\n");
                }
            } while (manualPreset.dst_start_dow < 0 || manualPreset.dst_start_dow > 6);
            
            do {
                Serial.print("Час перехода (0-23): ");
                while (!Serial.available()) { delay(100); }
                manualPreset.dst_start_hour = Serial.readStringUntil('\n').toInt();
                if (manualPreset.dst_start_hour < 0 || manualPreset.dst_start_hour > 23) {
                    Serial.print("✖ Ошибка: Час должен быть от 0 до 23. Попробуйте снова.\n");
                }
            } while (manualPreset.dst_start_hour < 0 || manualPreset.dst_start_hour > 23);
            
            Serial.print("\nКОНЕЦ DST (переход на зимнее время):\n");
            
            do {
                Serial.print("Месяц (1-12): ");
                while (!Serial.available()) { delay(100); }
                manualPreset.dst_end_month = Serial.readStringUntil('\n').toInt();
                if (manualPreset.dst_end_month < 1 || manualPreset.dst_end_month > 12) {
                    Serial.print("✖ Ошибка: Месяц должен быть от 1 до 12. Попробуйте снова.\n");
                }
            } while (manualPreset.dst_end_month < 1 || manualPreset.dst_end_month > 12);
            
            do {
                Serial.print("Неделя (1-4, 5=последняя): ");
                while (!Serial.available()) { delay(100); }
                manualPreset.dst_end_week = Serial.readStringUntil('\n').toInt();
                if (manualPreset.dst_end_week < 1 || manualPreset.dst_end_week > 5) {
                    Serial.print("✖ Ошибка: Неделя должна быть от 1 до 5. Попробуйте снова.\n");
                }
            } while (manualPreset.dst_end_week < 1 || manualPreset.dst_end_week > 5);
            
            do {
                Serial.print("День недели (0=Вск, 1=Пнд, ..., 6=Сбт): ");
                while (!Serial.available()) { delay(100); }
                manualPreset.dst_end_dow = Serial.readStringUntil('\n').toInt();
                if (manualPreset.dst_end_dow < 0 || manualPreset.dst_end_dow > 6) {
                    Serial.print("✖ Ошибка: День недели должен быть от 0 до 6. Попробуйте снова.\n");
                }
            } while (manualPreset.dst_end_dow < 0 || manualPreset.dst_end_dow > 6);
            
            do {
                Serial.print("Час перехода (0-23): ");
                while (!Serial.available()) { delay(100); }
                manualPreset.dst_end_hour = Serial.readStringUntil('\n').toInt();
                if (manualPreset.dst_end_hour < 0 || manualPreset.dst_end_hour > 23) {
                    Serial.print("✖ Ошибка: Час должен быть от 0 до 23. Попробуйте снова.\n");
                }
            } while (manualPreset.dst_end_hour < 0 || manualPreset.dst_end_hour > 23);
        }
    } else {
        // DST не используется
        manualPreset.dst_offset = offset_hours;
        manualPreset.dst_start_month = 0;
        manualPreset.dst_start_week = 0;
        manualPreset.dst_start_dow = 0;
        manualPreset.dst_start_hour = 0;
        manualPreset.dst_end_month = 0;
        manualPreset.dst_end_week = 0;
        manualPreset.dst_end_dow = 0;
        manualPreset.dst_end_hour = 0;
        Serial.print("\n✓ DST не используется\n");
    }
    
    // Сохраняем настройки в config
    strcpy(config.time_config.timezone_name, "MANUAL");
    config.time_config.automatic_localtime = false;
    
    // Сохраняем параметры ручной зоны в config
    config.time_config.manual_std_offset = manualPreset.std_offset;
    config.time_config.manual_dst_offset = manualPreset.dst_offset;
    config.time_config.manual_dst_start_month = manualPreset.dst_start_month;
    config.time_config.manual_dst_start_week = manualPreset.dst_start_week;
    config.time_config.manual_dst_start_dow = manualPreset.dst_start_dow;
    config.time_config.manual_dst_start_hour = manualPreset.dst_start_hour;
    config.time_config.manual_dst_end_month = manualPreset.dst_end_month;
    config.time_config.manual_dst_end_week = manualPreset.dst_end_week;
    config.time_config.manual_dst_end_dow = manualPreset.dst_end_dow;
    config.time_config.manual_dst_end_hour = manualPreset.dst_end_hour;
    
    // Активируем ручной preset
    manualPresetActive = true;
    
    // Пересчитываем текущее смещение
    time_t now = time(nullptr);
    bool dst_now = calculateDSTStatus(now, &manualPreset);
    config.time_config.current_offset = dst_now ? manualPreset.dst_offset : manualPreset.std_offset;
    config.time_config.current_dst_active = dst_now;
    
    saveConfig();
    
    Serial.print("\n\n╭────────────────────────────────────────────────────────╮");
    Serial.print("\n│                 ✅ НАСТРОЙКА ЗАВЕРШЕНА                 │");
    Serial.print("\n╰────────────────────────────────────────────────────────╯");
    Serial.printf("\nРежим: Ручная настройка");
    Serial.printf("\nСтандартное смещение: UTC%+d", manualPreset.std_offset);
    if (manualPreset.dst_start_month > 0) {
        Serial.printf("\nDST смещение: UTC%+d", manualPreset.dst_offset);
        Serial.printf("\nТекущее смещение: UTC%+d (DST: %s)", 
                     config.time_config.current_offset,
                     config.time_config.current_dst_active ? "ON" : "OFF");
    } else {
        Serial.printf("\nТекущее смещение: UTC%+d (DST не используется)", config.time_config.current_offset);
    }
    Serial.print("\n\n");
}

// ========== ДИАГНОСТИКА DST ПРАВИЛ ==========

void compareDSTRules() {
    // Если это ручная настройка - сравнение не требуется
    if (strcmp(config.time_config.timezone_name, "MANUAL") == 0) {
        Serial.print("\n[TZ] Режим ручной настройки - сравнение с ezTime не требуется\n");
        return;
    }
    
    // Находим текущий preset
    const TimezonePreset* preset = findPresetByLocation(config.time_config.timezone_name);
    if (!preset) {
        Serial.print("\n[TZ] Локальный preset не найден");
        return;
    }
    
    // Если режим не автоматический - сравнение не требуется
    if (!config.time_config.automatic_localtime) {
        Serial.print("\n[TZ] Режим локальной таблицы - сравнение с ezTime не требуется\n");
        return;
    }
    
    // ========== ПОДКЛЮЧЕНИЕ К WiFi ==========
    bool need_disconnect = false;
    
    if (WiFi.status() != WL_CONNECTED) {
        Serial.print("\n[TZ] Подключение к WiFi для проверки правил DST...");
        
        WiFi.mode(WIFI_STA);
        WiFi.begin(config.wifi_ssid, config.wifi_pass);
        
        // Ждем подключения с таймаутом
        int attempts = 0;
        Serial.print("\n[WiFi] Подключение");
        while (WiFi.status() != WL_CONNECTED && attempts < 10) {
            delay(300);
            Serial.print(".");
            attempts++;
        }
        Serial.print("\n");
        
        if (WiFi.status() != WL_CONNECTED) {
            Serial.print("\n[TZ] Ошибка: не удалось подключиться к WiFi");
            WiFi.disconnect(true);
            WiFi.mode(WIFI_OFF);
            return;
        }
        
        Serial.printf("\n[WiFi] Подключено к %s\n", config.wifi_ssid);
        need_disconnect = true;
        
        // Даем ezTime время на инициализацию и получение данных
        Serial.print("[TZ] Ожидание данных от ezTime");
        for (int i = 0; i < 10; i++) {
            events();  // Обновляем ezTime
            delay(200);
            Serial.print(".");
        }
        Serial.print("\n");
    }
    
    // ========== УСТАНОВКА ЧАСОВОЙ ЗОНЫ ДЛЯ ezTime ==========
    Serial.printf("[TZ] Установка зоны '%s' для ezTime...\n", config.time_config.timezone_name);
    
    if (!localTZ.setLocation(String(config.time_config.timezone_name))) {
        Serial.print("[TZ] ⚠️  Не удалось установить зону для ezTime\n");
        Serial.print("[TZ] Возможные причины:\n");
        Serial.print("  • Зона не поддерживается ezTime\n");
        Serial.print("  • Нет связи с сервером времени ezTime\n");
        
        if (need_disconnect) {
            WiFi.disconnect(true);
            WiFi.mode(WIFI_OFF);
            Serial.print("\n[WiFi] Отключено\n");
        }
        return;
    }
    
    Serial.print("[TZ] Зона установлена успешно\n");
    
    // Даем ezTime время обработать установку зоны
    Serial.print("[TZ] Обновление данных зоны");
    for (int i = 0; i < 5; i++) {
        events();
        delay(200);
        Serial.print(".");
    }
    Serial.print("\n");
    
    // ========== УГЛУБЛЁННАЯ ДИАГНОСТИКА ezTime ==========
    Serial.print("\n[ДИАГНОСТИКА] Проверка состояния ezTime:\n");
    
    // 1. Проверка WiFi
    Serial.printf("  • WiFi статус: %d (%s)\n", WiFi.status(), 
                  WiFi.status() == WL_CONNECTED ? "подключено" : "НЕ подключено");
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("  • IP адрес: %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("  • RSSI: %d dBm\n", WiFi.RSSI());
    }
    
    // 2. Проверка времени
    time_t now = time(nullptr);
    Serial.printf("  • Текущее UTC время: %lu\n", (unsigned long)now);
    Serial.printf("  • Время инициализировано: %s\n", now > 1000000000 ? "ДА" : "НЕТ");
    
    // 3. Проверка состояния ezTime
    Serial.printf("  • ezTime serverTime: %s\n", UTC.dateTime("Y-m-d H:i:s").c_str());
    Serial.printf("  • Локальная зона установлена: %s\n", 
                  config.time_config.timezone_name[0] != '\0' ? "ДА" : "НЕТ");
    Serial.printf("  • Имя зоны: %s\n", config.time_config.timezone_name);
    
    // 4. Проверка состояния localTZ объекта
    Serial.print("  • Проверка localTZ объекта...\n");
    String test_posix = localTZ.getPosix();
    Serial.printf("    - POSIX строка: '%s'\n", test_posix.c_str());
    Serial.printf("    - POSIX длина: %d\n", test_posix.length());
    
    String tzname_now;
    bool isdst_now = false;
    int16_t offset_minutes_now = 0;
    
    // Пытаемся получить данные от ezTime с несколькими попытками
    int max_attempts = 5;
    bool data_valid = false;
    
    Serial.print("\n[ДИАГНОСТИКА] Попытки получения данных от ezTime:\n");
    
    for (int attempt = 0; attempt < max_attempts && !data_valid; attempt++) {
        Serial.printf("\n  Попытка %d/%d:\n", attempt + 1, max_attempts);
        
        if (attempt > 0) {
            events();  // Обновляем ezTime
            delay(500);
        }
        
        // Получаем данные
        time_t test_local = localTZ.tzTime(now, UTC_TIME, tzname_now, isdst_now, offset_minutes_now);
        
        // Выводим полученные данные
        Serial.printf("    - UTC время: %lu\n", (unsigned long)now);
        Serial.printf("    - Локальное время: %lu\n", (unsigned long)test_local);
        Serial.printf("    - Разница: %ld сек\n", (long)(test_local - now));
        Serial.printf("    - Название зоны: '%s'\n", tzname_now.c_str());
        Serial.printf("    - DST активен: %s\n", isdst_now ? "ДА" : "НЕТ");
        Serial.printf("    - Смещение (мин): %d\n", offset_minutes_now);
        Serial.printf("    - Смещение (час): %+d\n", offset_minutes_now / 60);
        
        // Дополнительная проверка через dateTime
        String ez_time_str = localTZ.dateTime("Y-m-d H:i:s");
        Serial.printf("    - ezTime строка: %s\n", ez_time_str.c_str());
        
        // Проверяем корректность данных
        // Для зон с ненулевым смещением offset не должен быть 0
        if (preset->std_offset != 0 || preset->dst_offset != 0) {
            if (offset_minutes_now != 0) {
                Serial.print("    ✓ Данные валидны (смещение ненулевое)\n");
                data_valid = true;
            } else {
                Serial.print("    ✗ Данные невалидны (смещение должно быть ненулевым)\n");
            }
        } else {
            // Для UTC зоны offset может быть 0
            Serial.print("    ✓ Данные валидны (UTC зона)\n");
            data_valid = true;
        }
    }
    
    // Если результат некорректный - ezTime не готов
    if (!data_valid) {
        Serial.print("\n[TZ] ⚠️  ezTime не вернул корректные данные после ");
        Serial.print(max_attempts);
        Serial.print(" попыток\n");
        
        Serial.print("\n[ДИАГНОСТИКА] Анализ проблемы:\n");
        if (WiFi.status() != WL_CONNECTED) {
            Serial.print("  ✗ Проблема: WiFi не подключен\n");
        } else if (now < 1000000000) {
            Serial.print("  ✗ Проблема: Системное время не инициализировано\n");
        } else if (test_posix.length() == 0) {
            Serial.print("  ✗ Проблема: POSIX строка зоны пуста\n");
        } else {
            Serial.print("  ✗ Проблема: ezTime не может получить данные с сервера\n");
            Serial.print("     или часовой пояс не поддерживается\n");
        }
        
        Serial.print("\n[TZ] Возможные причины:\n");
        Serial.print("  • Нет доступа к серверам времени ezTime\n");
        Serial.print("  • Проблемы с интернет-соединением\n");
        Serial.print("  • Часовой пояс не поддерживается ezTime\n");
        Serial.print("  • Недостаточно времени для инициализации ezTime\n");
        
        if (need_disconnect) {
            WiFi.disconnect(true);
            WiFi.mode(WIFI_OFF);
            Serial.print("\n[WiFi] Отключено\n");
        }
        return;
    }
    
    Serial.print("\n[ДИАГНОСТИКА] ✓ Данные успешно получены от ezTime\n");
    
    // ========== ВЫВОД РЕЗУЛЬТАТОВ ==========
    Serial.print("\n╔════════════════════════════════════════════════════════════");
    Serial.print("\n║        СРАВНЕНИЕ ПРАВИЛ DST: ezTime vs Локальная таблица");
    Serial.print("\n╠════════════════════════════════════════════════════════════");
    Serial.printf("\n║ Зона: %s", config.time_config.timezone_name);
    Serial.print("\n╠════════════════════════════════════════════════════════════");
    
    // ВАЖНО: ezTime использует POSIX формат, где знаки инвертированы!
    // POSIX: CET-1 означает UTC+1 (минус = восток от UTC)
    // Поэтому инвертируем знак для корректного отображения
    int8_t eztime_offset_now = -(offset_minutes_now / 60);
    
    // Получаем данные из локальной таблицы
    bool local_dst_now = calculateDSTStatus(now, preset);
    int8_t local_offset_now = local_dst_now ? preset->dst_offset : preset->std_offset;
    
    Serial.print("\n║ ТЕКУЩИЙ СТАТУС:");
    Serial.printf("\n║   ezTime:    UTC%+d, DST: %s", 
                  eztime_offset_now, isdst_now ? "ДА" : "НЕТ");
    Serial.printf("\n║   Таблица:   UTC%+d, DST: %s", 
                  local_offset_now, local_dst_now ? "ДА" : "НЕТ");
    
    // Проверяем совпадение
    bool match = (eztime_offset_now == local_offset_now) && (isdst_now == local_dst_now);
    
    if (match) {
        Serial.print("\n║");
        Serial.print("\n║   ✅ СОВПАДЕНИЕ - правила актуальны");
    } else {
        Serial.print("\n║");
        Serial.print("\n║   ⚠️  РАСХОЖДЕНИЕ ОБНАРУЖЕНО!");
        Serial.print("\n║   Возможные причины:");
        Serial.print("\n║   • Изменились политические правила DST");
        Serial.print("\n║   • Требуется обновление таблицы переводов времени");
        Serial.print("\n║   • Проблема с подключением к серверу ezTime");
    }
    
    Serial.print("\n╠════════════════════════════════════════════════════════════");
    Serial.print("\n║ ПРАВИЛА ИЗ ЛОКАЛЬНОЙ ТАБЛИЦЫ:");
    
    if (preset->dst_start_month > 0 && preset->dst_offset != preset->std_offset) {
        Serial.printf("\n║   Стандарт:  UTC%+d (зима)", preset->std_offset);
        Serial.printf("\n║   DST:       UTC%+d (лето)", preset->dst_offset);
        
        const char* dow_names[] = {"Вск", "Пнд", "Втр", "Срд", "Чтв", "Птн", "Сбт"};
        const char* months[] = {"", "янв", "фев", "мар", "апр", "май", "июн", 
                                "июл", "авг", "сен", "окт", "ноя", "дек"};
        
        Serial.printf("\n║   Начало DST: %s %s, неделя %d, %02d:00",
                     dow_names[preset->dst_start_dow], 
                     months[preset->dst_start_month],
                     preset->dst_start_week,
                     preset->dst_start_hour);
        
        Serial.printf("\n║   Конец DST:  %s %s, неделя %d, %02d:00",
                     dow_names[preset->dst_end_dow],
                     months[preset->dst_end_month],
                     preset->dst_end_week,
                     preset->dst_end_hour);
    } else {
        Serial.printf("\n║   Постоянное смещение: UTC%+d (DST не используется)", preset->std_offset);
    }
    
    Serial.print("\n╚════════════════════════════════════════════════════════════\n");
    
    if (!match) {
        Serial.print("\n💡 РЕКОМЕНДАЦИЯ: проверьте наличие обновлений прошивки\n");
    }
    
    // ========== ОТКЛЮЧЕНИЕ WiFi ==========
    if (need_disconnect) {
        Serial.print("\n[WiFi] Отключение...");
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        Serial.print(" готово\n");
    }
}
