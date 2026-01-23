#include "engineering_menu.h"
#include "menu_manager.h"
#include "config.h"
#include "hardware.h"
#include "time_utils.h"

extern bool printEnabled;

enum EngineeringSubMenu {
    ENG_SUBMENU_NONE = 0,
    ENG_SUBMENU_CLOCK_TYPE
};

static EngineeringSubMenu engineeringSubMenu = ENG_SUBMENU_NONE;

static const char* getClockTypeName(ClockType type) {
    switch (type) {
        case CLOCK_TYPE_NIXIE: return "Nixie";
        case CLOCK_TYPE_NIXIE_HAND: return "Nixie hand";
        case CLOCK_TYPE_CYCLOTRON: return "Cyclotron";
        case CLOCK_TYPE_VERTICAL: return "Vertical";
        case CLOCK_TYPE_MECH_2: return "Mech 2";
        case CLOCK_TYPE_MECH_PEND: return "Mech pend";
        default: return "Unknown";
    }
}

static void printClockTypeMenu() {
    Serial.println("\n=== ТИП ЧАСОВ И КОЛИЧЕСТВО РАЗРЯДОВ ===");
    Serial.printf("Текущее: %s, разрядов: %d\n\n", getClockTypeName(config.clock_type), config.clock_digits);
    Serial.println("Nix <кол-во>  - Nixie clock (1,2,4,6 разрядов)");
    Serial.println("Nix hand      - Наручные nixie (2 разряда)");
    Serial.println("Cycl          - Cyclotron (4 разряда)");
    Serial.println("Vert          - Вертикальные механические разряды");
    Serial.println("Mech 2        - Классическая механика на 2 стрелки");
    Serial.println("Mech pend     - Маятниковая механика");
    Serial.println("back, b       - Назад в инженерное меню");
    Serial.println();
    Serial.print("> ");
}

static bool handleClockTypeMenu(String command) {
    if (handleCommonMenuCommands(command, printClockTypeMenu)) {
        engineeringSubMenu = ENG_SUBMENU_NONE;
        return true;
    }

    String cmd = command;
    cmd.trim();
    cmd.toLowerCase();

    if (cmd.equals("back") || cmd.equals("b")) {
        engineeringSubMenu = ENG_SUBMENU_NONE;
        printEngineeringMenu();
        return true;
    }

    if (cmd.startsWith("nix ")) {
        String arg = cmd.substring(4);
        arg.trim();
        if (arg.equals("hand")) {
            config.clock_type = CLOCK_TYPE_NIXIE_HAND;
            config.clock_digits = 2;
            saveConfig();
            Serial.println("\n[TYPE] Установлен тип: Nixie hand (2 разряда)");
            Serial.print("> ");
            return true;
        }

        int digits = arg.toInt();
        if (digits == 1 || digits == 2 || digits == 4 || digits == 6) {
            config.clock_type = CLOCK_TYPE_NIXIE;
            config.clock_digits = static_cast<uint8_t>(digits);
            saveConfig();
            Serial.printf("\n[TYPE] Установлен тип: Nixie, разрядов: %d\n", digits);
        } else {
            Serial.println("\n[TYPE] Ошибка: допустимы разряды 1, 2, 4 или 6");
        }
        Serial.print("> ");
        return true;
    }

    if (cmd.equals("cycl")) {
        config.clock_type = CLOCK_TYPE_CYCLOTRON;
        config.clock_digits = 4;
        saveConfig();
        Serial.println("\n[TYPE] Установлен тип: Cyclotron (4 разряда)");
        Serial.print("> ");
        return true;
    }

    if (cmd.equals("vert")) {
        config.clock_type = CLOCK_TYPE_VERTICAL;
        saveConfig();
        Serial.println("\n[TYPE] Установлен тип: Vertical");
        Serial.print("> ");
        return true;
    }

    if (cmd.startsWith("mech")) {
        if (cmd.equals("mech 2")) {
            config.clock_type = CLOCK_TYPE_MECH_2;
            config.clock_digits = 2;
            saveConfig();
            Serial.println("\n[TYPE] Установлен тип: Mech 2 (2 стрелки)");
            Serial.print("> ");
            return true;
        }
        if (cmd.equals("mech pend")) {
            config.clock_type = CLOCK_TYPE_MECH_PEND;
            saveConfig();
            Serial.println("\n[TYPE] Установлен тип: Mech pend");
            Serial.print("> ");
            return true;
        }
    }

    Serial.println("Неизвестная команда. Введите 'help' для списка");
    Serial.print("> ");
    return true;
}

void enterEngineeringMenu() {
    inMenuMode = true;
    printEnabled = false;
    currentMenuState = MENU_ENGINEERING;
    printEngineeringMenu();
}

void printEngineeringMenu() {
    Serial.println("\n=== ИНЖЕНЕРНОЕ МЕНЮ ===");
    Serial.println("Внимание: изменение параметров может повлиять на работу устройства.");
    Serial.println();
    Serial.println("1  Тип часов и количество разрядов");
    Serial.println("2  Проверка дисплея");
    Serial.println("3  Смена серийного номера");
    Serial.println("4  Специфические настройки");
    Serial.println();
    printMappingMenuCommands();
    Serial.print("> ");
}

void handleEngineeringMenu(String command) {
    if (engineeringSubMenu == ENG_SUBMENU_CLOCK_TYPE) {
        handleClockTypeMenu(command);
        return;
    }

    if (handleCommonMenuCommands(command, printEngineeringMenu)) return;

    if (command.equals("1")) {
        engineeringSubMenu = ENG_SUBMENU_CLOCK_TYPE;
        printClockTypeMenu();
        return;
    } else if (command.equals("2")) {
        Serial.println("\n[DISP] Проверка дисплея — в разработке");
    } else if (command.equals("3")) {
        Serial.printf("\n[SERIAL] Текущий серийный номер: %s\n", config.serial_number);
        Serial.println("[SERIAL] Смена серийного номера — в разработке");
    } else if (command.equals("4")) {
        Serial.println("\n[SET] Специфические настройки — в разработке");
    } else if (command.equals("time") || command.equals("t")) {
        printTime();
        return;
    } else {
        Serial.println("Неизвестная команда. Введите 'help' для списка");
    }

    Serial.print("> ");
}
