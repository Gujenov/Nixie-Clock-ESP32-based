# Документация прошивки Nixie Clock

> **Новичок?** Начните с [QUICKSTART.md](QUICKSTART.md) для быстрого старта в 5 минут.

В папке `docs/` собрана полная документация архитектуры и подсистем прошивки, организованная по уровню детализации.

## Основные документы (для всех)

- [QUICKSTART.md](QUICKSTART.md) — краткий 5-минутный старт для новичков.
- [GPIO_PINOUT.md](GPIO_PINOUT.md) — распределение пинов ESP32-S3, запрещённые линии памяти и USB.
- [SYSTEM_ARCHITECTURE.md](SYSTEM_ARCHITECTURE.md) — мастер-документ: общая архитектура, время, меню, будильники, ключевые файлы и точки расширения.
- [ONBOARDING_AND_OTA.md](ONBOARDING_AND_OTA.md) — шаг за шагом: первая прошивка, UART и OTA обновления.

## Технические документы (в папке `technical/`)

Детальные справочники по ядру системы и подсистемам:

- [technical/TIME_MANAGER.md](technical/TIME_MANAGER.md) — ядро времени: выбор источника, синхронизация, работа DS3231 и системного RTC.
- [technical/TIMEZONE_SYSTEM.md](technical/TIMEZONE_SYSTEM.md) — часовые пояса: режимы online/offline, правила DST, пресеты и примеры.
- [technical/MENU_SYSTEM.md](technical/MENU_SYSTEM.md) — архитектура меню и механика обработки команд.
- [technical/MENU_COMMANDS_STRUCTURE.md](technical/MENU_COMMANDS_STRUCTURE.md) — полное дерево меню и каталог команд.
- [technical/ENGINEERING_MENU.md](technical/ENGINEERING_MENU.md) — инженерное меню и аппаратная конфигурация.
- [technical/ALARM_SYSTEM.md](technical/ALARM_SYSTEM.md) — подсистема будильников и их логика.
- [technical/PLATFORM_CORE.md](technical/PLATFORM_CORE.md) — ядро платформы, аппаратные возможности и флаги.

## Как использовать документы

### Для новичка:
1. Прочитайте [QUICKSTART.md](QUICKSTART.md) — краткая схема на 5 минут.
2. При необходимости разводки схемы используйте [GPIO_PINOUT.md](GPIO_PINOUT.md).
3. При первой прошивке читайте [ONBOARDING_AND_OTA.md](ONBOARDING_AND_OTA.md).

### Для разработчика:
1. Начните с [SYSTEM_ARCHITECTURE.md](SYSTEM_ARCHITECTURE.md) — обзор и связи между подсистемами.
2. Изучите нужную вам подсистему в папке [technical/](technical/):
   - Время и синхронизация → [technical/TIME_MANAGER.md](technical/TIME_MANAGER.md)
   - Часовые пояса → [technical/TIMEZONE_SYSTEM.md](technical/TIMEZONE_SYSTEM.md)
   - Меню и команды → [technical/MENU_SYSTEM.md](technical/MENU_SYSTEM.md) + [technical/MENU_COMMANDS_STRUCTURE.md](technical/MENU_COMMANDS_STRUCTURE.md)
   - Инженерное меню → [technical/ENGINEERING_MENU.md](technical/ENGINEERING_MENU.md)
   - Будильники → [technical/ALARM_SYSTEM.md](technical/ALARM_SYSTEM.md)
   - Архитектура платформы → [technical/PLATFORM_CORE.md](technical/PLATFORM_CORE.md)

## Структура документации

```
docs/
├── README.md                    ← вы здесь
├── QUICKSTART.md               ← начните отсюда (5 минут)
├── SYSTEM_ARCHITECTURE.md      ← общая архитектура (для всех)
├── GPIO_PINOUT.md              ← распределение пинов (для разводки)
├── ONBOARDING_AND_OTA.md       ← первая прошивка и обновления (для всех)
└── technical/                  ← детальные справочники
    ├── TIME_MANAGER.md         ← ядро времени
    ├── TIMEZONE_SYSTEM.md      ← часовые пояса и DST
    ├── MENU_SYSTEM.md          ← архитектура меню
    ├── MENU_COMMANDS_STRUCTURE.md ← каталог команд
    ├── ENGINEERING_MENU.md     ← инженерное меню
    ├── ALARM_SYSTEM.md         ← будильники
    ├── PLATFORM_CORE.md        ← ядро платформы
    └── GPIO_PINOUT.md          ← копия для удобства в technical/
```

## Рекомендованный порядок для документации `.docx`

1. **Введение**
2. **Архитектура системы** — из [SYSTEM_ARCHITECTURE.md](SYSTEM_ARCHITECTURE.md)
3. **Система времени** — из [technical/TIME_MANAGER.md](technical/TIME_MANAGER.md)
4. **Система часовых поясов** — из [technical/TIMEZONE_SYSTEM.md](technical/TIMEZONE_SYSTEM.md)
5. **Ядро платформы и возможности** — из [technical/PLATFORM_CORE.md](technical/PLATFORM_CORE.md)
6. **Меню и интерфейс** — из [technical/MENU_SYSTEM.md](technical/MENU_SYSTEM.md)
7. **Справочник команд** — из [technical/MENU_COMMANDS_STRUCTURE.md](technical/MENU_COMMANDS_STRUCTURE.md)
8. **Инженерное меню** — из [technical/ENGINEERING_MENU.md](technical/ENGINEERING_MENU.md)
9. **Подсистема будильников** — из [technical/ALARM_SYSTEM.md](technical/ALARM_SYSTEM.md)
10. **Распределение пинов и электрические характеристики** — из [GPIO_PINOUT.md](GPIO_PINOUT.md)
11. **Первое включение и OTA обновления** — из [ONBOARDING_AND_OTA.md](ONBOARDING_AND_OTA.md)

## Примечания

- **Основные документы** (`QUICKSTART.md`, `SYSTEM_ARCHITECTURE.md`, `GPIO_PINOUT.md`, `ONBOARDING_AND_OTA.md`) находятся в корне `docs/` для быстрого доступа.
- **Технические документы** находятся в папке `technical/` и содержат глубокие справочники по отдельным подсистемам.
- Файлы `README.md` (этот), `VERSIONING.md`, `PLATFORM_PROFILE_NIXIE6_SPI.md` и другие документы в корне относятся к проекту в целом, а не к документации архитектуры.
- Если будут добавлены новые подсистемные документы, добавьте их в папку `technical/` и обновите этот индекс.

