Текущая структура меню по команде `m` такая:

- Вход в меню: команда `menu` / `m` в command_handler.cpp
- Печать главного меню: `enterMenuMode()` в menu_manager.cpp
- Обработка выбора главного меню: `handleMainMenu()` в menu_manager.cpp

### Дерево меню (`m`)

1. **Настройки времени и часовых поясов**  
   Печать: menu_manager.cpp  
   Команды внутри: время, sync, set UTC/local, auto sync, `tz list/tzl`, `tz auto/tza`, `tz manual/tzm`, `tz check/tzc` (menu_manager.cpp, menu_manager.cpp)  
   Доп. интерактивный уровень: список зон через `listAvailableTimezones()` (timezone_manager.cpp), выбор номера зоны + `100` для ручного смещения (timezone_manager.cpp)

2. **Управление звуком**  
   Корень звукового меню: menu_manager.cpp  
   Подменю:
   - 2.1 Будильники (`printAlarmControlMenu`) — menu_manager.cpp
   - 2.2 Настройка боя (`printSoundChimeMenu`, сейчас заглушка) — menu_manager.cpp
   - 2.3 Доп. функции аудио (`printSoundExtraMenu`, сейчас заглушка) — menu_manager.cpp  
   Переходы между этими подменю: menu_manager.cpp

3. **Настройки Wi‑Fi и NTP**  
   Печать: menu_manager.cpp  
   Без вложенных подменю (набор команд в одном уровне): menu_manager.cpp

4. **Управление дисплеем**  
   Сейчас заглушка “в разработке”: menu_manager.cpp

5. **Информация о системе**  
   Один уровень, вывод инфо: menu_manager.cpp  
   Обработка: menu_manager.cpp

6. **Конфигурация**  
   Один уровень, + команда `default`: menu_manager.cpp

### Навигация
Общие команды навигации (`back/b`, `menu/m`, `out/o`) реализованы в `handleCommonMenuCommands()` и `printMappingMenuCommands()`:
- menu_manager.cpp
- menu_manager.cpp

Если нужно, следующим шагом могу предложить новую целевую структуру (v2) и сразу разложить, что в какие уровни перенести.