#include "config.h"
#include "timezone_manager.h"
#include "menu_manager.h"
#include <string.h>
#include <ezTime.h>

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

// ========== ФУНКЦИИ ПОИСКА ПРЕСЕТОВ ==========

const TimezonePreset* findPresetByLocation(const char* location) {
    if (!location) return nullptr;
    
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
    
    // Находим preset для этой локации
    const TimezonePreset* preset = findPresetByLocation(config.time_config.timezone_name);
    if (!preset) {
        Serial.printf("\n[TZ] Предупреждение: локация '%s' не найдена в таблице", config.time_config.timezone_name);
        return false;
    }
    
    Serial.printf("\n[TZ] Инициализация: %s", preset->display_name);
    
    // Если включён режим автоматического локального времени - пробуем загрузить ezTime
    if (config.time_config.automatic_localtime) {
        if (localTZ.setLocation(String(config.time_config.timezone_name))) {
            localTZ.setDefault();
            ezTimeAvailable = true;
            Serial.print("\n[TZ] Режим: ezTime (online с правилами DST)");
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
        
        time_t local = localTZ.tzTime(utc, UTC_TIME, tzname, isdst, offset_minutes);
        
        // Обновляем кэшированные значения для отображения
        config.time_config.current_offset = offset_minutes / 60;
        config.time_config.current_dst_active = isdst;
        
        return local;
    }
    
    // === РЕЖИМ 2: Локальная таблица (offline) ===
    
    // Находим preset для текущей локации
    const TimezonePreset* preset = findPresetByLocation(config.time_config.timezone_name);
    if (!preset) {
        // Fallback: возвращаем UTC без изменений
        Serial.print("\n[TZ] Ошибка: preset не найден, используется UTC");
        config.time_config.current_offset = 0;
        config.time_config.current_dst_active = false;
        return utc;
    }
    
    // Вычисляем, активен ли DST сейчас
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
        return utc;
    }
    
    // === РЕЖИМ 2: Локальная таблица (offline) ===
    
    const TimezonePreset* preset = findPresetByLocation(config.time_config.timezone_name);
    if (!preset) {
        return local;  // Fallback
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
    Serial.print("\n╔═══════════════════════════════════════════════════════╗");
    Serial.print("\n║           ИНФОРМАЦИЯ О ЧАСОВОМ ПОЯСЕ                  ║");
    Serial.print("\n╠═══════════════════════════════════════════════════════╣");
    
    // Текущая локация
    const TimezonePreset* preset = findPresetByLocation(config.time_config.timezone_name);
    if (preset) {
        Serial.printf("\n║ Time zone: %-42s ║", preset->zone_name);
        Serial.printf("\n║ Базовое смещение: UTC%+d%-33s ║", preset->std_offset, "");
        
        // Информация о DST
        if (preset->dst_start_month > 0 && preset->dst_offset != preset->std_offset) {
            Serial.print("\n║ Переход на летнее/зимнее время: используется         ║");
            
            // Показываем текущее время (летнее/зимнее)
            if (config.time_config.current_dst_active) {
                // "Текущее время: летнее (UTC+2)" - вычисляем длину
                char time_str[60];
                sprintf(time_str, "Текущее время: летнее (UTC%+d)", config.time_config.current_offset);
                int visual_len = 0;
                for (const char* s = time_str; *s; s++) {
                    if ((*s & 0xC0) != 0x80) visual_len++;
                }
                int padding = 54 - visual_len;  // Целевая ширина 54 символа
                if (padding < 0) padding = 0;
                
                Serial.print("\n║ ");
                Serial.print(time_str);
                for (int j = 0; j < padding; j++) Serial.print(" ");
                Serial.print("║");
            } else {
                // "Текущее время: зимнее (UTC+1)" - вычисляем длину
                char time_str[60];
                sprintf(time_str, "Текущее время: зимнее (UTC%+d)", config.time_config.current_offset);
                int visual_len = 0;
                for (const char* s = time_str; *s; s++) {
                    if ((*s & 0xC0) != 0x80) visual_len++;
                }
                int padding = 54 - visual_len;  // Целевая ширина 54 символа
                if (padding < 0) padding = 0;
                
                Serial.print("\n║ ");
                Serial.print(time_str);
                for (int j = 0; j < padding; j++) Serial.print(" ");
                Serial.print("║");
            }
        } else {
            Serial.print("\n║ Переход на летнее/зимнее время: не используется       ║");
        }
    } else {
        Serial.printf("\n║ Time zone: %-42s ║", config.time_config.timezone_name);
        Serial.print("\n║ ОШИБКА: Локация не найдена в таблице                 ║");
    }
    
    Serial.print("\n╚═══════════════════════════════════════════════════════╝\n");
}

void listAvailableTimezones() {
    Serial.print("\n╔════════════════════════════════════════════════════════════════════╗");
    Serial.print("\n║                    ДОСТУПНЫЕ ЧАСОВЫЕ ПОЯСА                         ║");
    Serial.print("\n╠════╦═══════════════════════════════════════╦══════════╦════════════╣");
    Serial.print("\n║ #  ║ Локация                               ║ Смещение ║ DST        ║");
    Serial.print("\n╠════╬═══════════════════════════════════════╬══════════╬════════════╣");
    
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
                Serial.print("\n╠════╬═══════════════════════════════════════╬══════════╬════════════╣");
            }
            
            // Вычисляем визуальную длину названия региона
            int region_visual_len = 0;
            for (const char* s = new_region; *s; s++) {
                if ((*s & 0xC0) != 0x80) region_visual_len++;
            }
            int region_padding = 38 - region_visual_len;  // Изменено с 37 на 38 для дополнительного пробела
            if (region_padding < 0) region_padding = 0;
            
            // Выводим разделитель региона с правильным выравниванием
            Serial.print("\n║    ║ ");
            Serial.print(new_region);
            for (int j = 0; j < region_padding; j++) Serial.print(" ");
            Serial.print("║          ║            ║");
            
            Serial.print("\n╠════╬═══════════════════════════════════════╬══════════╬════════════╣");
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
            dst_str = "Нет";  // Убрали лишний пробел
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
        Serial.printf(" ║ %-8s ║ %-14s ║", offset_str, dst_str);
    }
    
    Serial.print("\n╚════╩═══════════════════════════════════════╩══════════╩════════════╝");
    Serial.print("\n\nДля выбора локации часовой зоны введите её цифровое обозначение\n");
    printMappingMenuCommands();
    Serial.print("> ");
}
