# Система управления часовыми поясами

## Обзор

Система часовых поясов полностью переработана и теперь использует **гибридный подход**:
- **Режим online (ezTime)**: автоматическое определение DST через интернет
- **Режим offline (локальная таблица)**: работа без интернета с локальными правилами DST

## Ключевые принципы

1. **Локация обязательна**: пользователь всегда должен указать географическую локацию (например, `Europe/Moscow`)
2. **UTC как основа**: все время хранится в UTC в DS3231 и ESP32 RTC
3. **Автоматический DST**: переход на летнее/зимнее время происходит автоматически
4. **Fallback**: если ezTime недоступен - используется локальная таблица

## Структура конфигурации

```cpp
struct TimeConfig {
    // === ОСНОВНАЯ НАСТРОЙКА ===
    char timezone_name[32];           // "Europe/Moscow" - ОБЯЗАТЕЛЬНО!
    bool automatic_localtime;         // true = ezTime, false = таблица
    
    // === ВЫЧИСЛЯЕМЫЕ ЗНАЧЕНИЯ (автоматически) ===
    int8_t current_offset;            // Текущее смещение UTC (+3, +4...)
    bool current_dst_active;          // Текущий статус DST
    
    // === СИНХРОНИЗАЦИЯ ===
    bool auto_sync_enabled;
    uint32_t last_ntp_sync;
    // ...
};
```

## Два режима работы

### Режим 1: ezTime (automatic_localtime = true)

```
Пользователь задаёт: timezone_name = "Europe/Moscow"
                     automatic_localtime = true

При инициализации:
├─ Загружается локация через ezTime (требует WiFi)
├─ ezTime получает полные правила DST из интернета
└─ Все переходы DST обрабатываются автоматически

При конвертации времени:
└─ utcToLocal() → использует localTZ.tzTime() из ezTime
```

**Преимущества:**
- ✅ Точные правила DST для любого года
- ✅ Учитывает исторические изменения
- ✅ Автоматическое обновление правил

**Недостатки:**
- ❌ Требует интернет при первой загрузке
- ❌ Зависит от внешнего сервиса

### Режим 2: Локальная таблица (automatic_localtime = false)

```
Пользователь задаёт: timezone_name = "Europe/Moscow"
                     automatic_localtime = false

При инициализации:
├─ Находится preset в таблице TIMEZONE_PRESETS[]
└─ Используются локальные правила DST

При конвертации времени:
├─ calculateDSTStatus() вычисляет, активен ли DST
└─ Применяется соответствующее смещение
```

**Преимущества:**
- ✅ Работает без интернета
- ✅ Не зависит от внешних сервисов
- ✅ Быстрая инициализация

**Недостатки:**
- ❌ Таблицу нужно обновлять вручную при изменении правил

## Таблица часовых поясов

В файле [timezone_manager.cpp](src/timezone_manager.cpp) определён массив:

```cpp
static const TimezonePreset TIMEZONE_PRESETS[] = {
    // {zone_name, display_name, std_offset, dst_offset, 
    //  dst_start_month, week, dow, hour, dst_end_month, week, dow, hour}
    
    {"Europe/Moscow", "Москва (MSK)", 3, 3, 0, 0, 0, 0, 0, 0, 0, 0},  // Без DST
    {"Europe/London", "Лондон (GMT/BST)", 0, 1, 3, 5, 0, 1, 10, 5, 0, 2},
    {"America/New_York", "Нью-Йорк (EST/EDT)", -5, -4, 3, 2, 0, 2, 11, 1, 0, 2},
    // ... всего 35+ предустановленных поясов
};
```

### Формат правил DST

- **month**: 1-12 (0 = нет DST)
- **week**: 1-4 = конкретная неделя, 5 = последняя неделя месяца
- **dow**: 0=Воскресенье, 1=Понедельник, ..., 6=Суббота
- **hour**: час перехода в местном времени

**Пример**: Лондон переходит на летнее время в **последнее воскресенье марта в 01:00**
```cpp
dst_start_month = 3,  // март
dst_start_week = 5,   // последняя неделя
dst_start_dow = 0,    // воскресенье
dst_start_hour = 1    // 01:00
```

## API функций

### Основные функции

```cpp
// Инициализация (вызывается при старте)
bool initTimezone();

// Конвертация времени
time_t utcToLocal(time_t utc);      // UTC → локальное
time_t localToUtc(time_t local);    // Локальное → UTC

// Установка часового пояса
bool setTimezone(const char* tz_name);  // "Europe/Moscow"

// Поиск в таблице
const TimezonePreset* findPresetByLocation(const char* location);
uint8_t getPresetsCount();

// Отладка
void printTimezoneInfo();          // Красивый вывод информации
void listAvailableTimezones();     // Список всех поясов
```

### Вычисление DST

```cpp
// Проверяет, активен ли DST для данного UTC времени
bool calculateDSTStatus(time_t utc, const TimezonePreset* preset);

// Вычисляет timestamp перехода на DST
time_t calculateDSTTransition(int year, uint8_t month, uint8_t week, 
                               uint8_t dow, uint8_t hour, int8_t offset);
```

## Алгоритм работы

### При старте системы:

```
1. initConfiguration()
   └─ Загружает timezone_name и automatic_localtime из NVRAM

2. initTimezone()
   ├─ Проверяет, что timezone_name установлена
   ├─ Находит preset в таблице
   ├─ Если automatic_localtime == true:
   │  ├─ Пытается загрузить через ezTime
   │  └─ При неудаче → fallback на таблицу
   └─ Если automatic_localtime == false:
      └─ Использует только таблицу
```

### При конвертации времени:

```
utcToLocal(time_t utc):
  ├─ Если (automatic_localtime && ezTime доступен):
  │  ├─ Вызов localTZ.tzTime() → получаем локальное время
  │  ├─ Обновляем current_offset и current_dst_active
  │  └─ Возвращаем локальное время
  │
  └─ Иначе (локальная таблица):
     ├─ Находим preset по timezone_name
     ├─ Вычисляем DST: calculateDSTStatus(utc, preset)
     ├─ Выбираем offset: is_dst ? dst_offset : std_offset
     ├─ Обновляем current_offset и current_dst_active
     └─ Возвращаем utc + (offset * 3600)
```

### При NTP синхронизации:

```
syncTime():
  1. Подключение к WiFi
  2. Запрос UTC времени с NTP сервера
  3. Установка UTC в систему:
     ├─ settimeofday() → ESP32 RTC
     └─ rtc->adjust() → DS3231
  4. Отключение WiFi
  
  ⚠️ ВАЖНО: автоопределение смещения УДАЛЕНО!
  Пользователь обязан задать timezone_name вручную.
```

## Примеры использования

### Установка часового пояса

```cpp
// Способ 1: Использовать пресет
setTimezone("Europe/Moscow");
config.time_config.automatic_localtime = true;  // Попробовать ezTime
saveConfig();

// Способ 2: Только таблица
setTimezone("America/New_York");
config.time_config.automatic_localtime = false;  // Только офлайн
saveConfig();
```

### Получение локального времени

```cpp
time_t utc = getCurrentUTCTime();  // Получаем UTC
time_t local = utcToLocal(utc);    // Конвертируем в локальное

// Проверяем статус DST
Serial.printf("UTC%+d, DST: %s\n", 
              config.time_config.current_offset,
              config.time_config.current_dst_active ? "ON" : "OFF");
```

### Добавление нового пояса

Редактируем [timezone_manager.cpp](src/timezone_manager.cpp), добавляем в массив:

```cpp
{"Asia/Dubai", "Дубай (GST)", 4, 4, 0, 0, 0, 0, 0, 0, 0, 0},  // Без DST
```

Для пояса с DST (например, Берлин):

```cpp
{"Europe/Berlin", "Берлин (CET/CEST)", 
  1,     // Стандарт: UTC+1
  2,     // Летнее: UTC+2
  3, 5, 0, 2,   // Начало DST: последнее воскресенье марта в 02:00
  10, 5, 0, 3   // Конец DST: последнее воскресенье октября в 03:00
},
```

## Отладочные команды (Serial)

```
> tz_info          - Показать информацию о текущем поясе
> tz_list          - Список всех доступных поясов
> tz_set <name>    - Установить часовой пояс
> enable_auto_tz   - Включить ezTime (online)
> disable_auto_tz  - Выключить ezTime (offline)
```

## Миграция со старой версии

### Устаревшие поля (будут удалены в будущем):

```cpp
// DEPRECATED - не использовать!
```

### Переход на новую систему:

1. Установите `timezone_name` в config
2. Установите `automatic_localtime` (true/false)
3. Удалите обращения к устаревшим полям
4. Используйте `current_offset` и `current_dst_active` для UI

## Тестирование

### Проверка DST переходов:

```cpp
// Установить тестовое время перед/после перехода
struct tm test_time = {0};
test_time.tm_year = 2026 - 1900;
test_time.tm_mon = 2;   // Март
test_time.tm_mday = 30; // Последнее воскресенье
test_time.tm_hour = 0;  // 00:00 UTC

time_t utc = mktime(&test_time);
time_t local = utcToLocal(utc);

// Проверить, что DST активен после 01:00 местного времени
```

### Проверка fallback:

```
1. Отключить WiFi
2. Установить automatic_localtime = true
3. Перезагрузить → должен переключиться на таблицу
4. Проверить, что время всё равно корректное
```

## FAQ

**Q: Можно ли использовать систему без ezTime?**  
A: Да, установите `automatic_localtime = false` - будет использоваться только локальная таблица.

**Q: Что будет, если локация не найдена в таблице?**  
A: Система вернёт UTC время без смещения и выведет предупреждение в Serial.

**Q: Как часто обновляется DST статус?**  
A: При каждом вызове `utcToLocal()` - статус вычисляется заново.

**Q: Нужно ли хранить информацию о DST в NVRAM?**  
A: Нет, `current_dst_active` вычисляется автоматически при каждой конвертации времени.

**Q: Что делать, если правила DST изменились?**  
A: Обновить таблицу `TIMEZONE_PRESETS[]` в [timezone_manager.cpp](src/timezone_manager.cpp) и перепрошить устройство.

## Производительность

- Вычисление DST статуса: ~0.5мс (локальная таблица)
- Конвертация через ezTime: ~2-3мс
- Поиск preset в таблице: O(n) = ~35 итераций

## История изменений

**v4.0 (2026-01-12):**
- ✅ Полная переработка системы часовых поясов
- ✅ Добавлена таблица с 35+ пресетами
- ✅ Реализовано автоматическое вычисление DST
- ✅ Удалено автоопределение смещения через VPN/геолокацию
- ✅ Гибридный режим ezTime + локальная таблица

**v3.x (старая версия):**
- ❌ Ручное управление DST пользователем
- ❌ Автоопределение смещения (проблемы с VPN)
- ❌ Отсутствие fallback при недоступности ezTime
