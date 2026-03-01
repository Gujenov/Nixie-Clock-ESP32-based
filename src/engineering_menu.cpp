#include "engineering_menu.h"
#include "menu_manager.h"
#include "config.h"
#include "hardware.h"
#include "time_utils.h"
#include "ota_manager.h"

extern bool printEnabled;

enum EngineeringSubMenu {
    ENG_SUBMENU_NONE = 0,
    ENG_SUBMENU_SERIAL,
    ENG_SUBMENU_HARDWARE,
    ENG_SUBMENU_TESTING,
    ENG_SUBMENU_NIX6_OUTPUT
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

static const char* getNix6OutputModeName(Nix6OutputMode mode) {
    switch (mode) {
        case NIX6_OUTPUT_STD: return "Стандартный";
        case NIX6_OUTPUT_REVERSE_INVERT: return "Обратный + инверсия нибблов";
        default: return "Unknown";
    }
}

static void printEngineeringSubmenuNavigation() {
    Serial.println("\nНавигация:");
    Serial.println("  back / b       - Назад в инженерное меню");
    Serial.println("  out / o       - Выход из режима настройки");
    Serial.println("=========================\n");
}

static bool handleEngineeringSubmenuNavigation(const String &command) {
    if (handleCommonMenuCommands(command, nullptr)) {
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

    return false;
}

static void printSerialNumberMenu() {
    Serial.println("\n=== СМЕНА СЕРИЙНОГО НОМЕРА ===");
    Serial.printf("\nТекущий серийный номер: %s\n\n", config.serial_number);
    Serial.println("Для изменения введите новые 11 символов");
    printEngineeringSubmenuNavigation();
    Serial.print("> ");
}

static void printHardwareMenu() {
    Serial.println("\n=== НАСТРОЙКИ ЖЕЛЕЗА ===");
    Serial.printf("Текущее: %s, разрядов: %d\n\n", getClockTypeName(config.clock_type), config.clock_digits);
    if (config.clock_type == CLOCK_TYPE_NIXIE && config.clock_digits == 6) {
        Serial.printf("Режим Nix 6: %s\n\n", getNix6OutputModeName(config.nix6_output_mode));
    }
    Serial.println("1  Nix <кол-во>  - Nixie clock (1,2,4,6 разрядов)");
    Serial.println("2  Nix hand      - Наручные nixie (2 разряда)");
    Serial.println("3  Cycl          - Cyclotron (4 разряда вечный календарь)");
    Serial.println("4  Vert          - Вертикальная механика (столбиковая ролик-рейка)");
    Serial.println("5  Mech pend     - Маятниковая механика");
    printEngineeringSubmenuNavigation();
    Serial.print("> ");
}

static void printTestingMenu() {
    Serial.println("\n=== ТЕСТИРОВАНИЕ ===");
    Serial.println("-=В разработке=-");
    printEngineeringSubmenuNavigation();
    Serial.print("> ");
}

static void printNix6OutputMenu() {
    Serial.println("\n=== NIX 6: ПОРЯДОК ВЫВОДА ===");
    Serial.println("\nВыберите порядок вывода разрядов и порядок вывода бит:\n");
    Serial.println("1  Стандартный порядок вывода: ЧАС-МИН-СЕК-СЛУЖ. Прямые биты (без инверсии)");
    Serial.println("2  Обратный порядок вывода + инверсия битов по нибблам;");
    Serial.println("   Этот вариант стоит выбрать для первого экземпляра часов,");
    Serial.println("   где порядок вывода: СЕК-МИН-ЧАС-СЛУЖ и все нибблы инвертированы.");
    printEngineeringSubmenuNavigation();
    Serial.print("> ");
}

static bool handleSerialNumberMenu(const String &command) {
    if (handleEngineeringSubmenuNavigation(command)) return true;

    String serial = command;
    serial.trim();

    if (serial.length() != 11) {
        Serial.println("\n[SERIAL] Ошибка: серийный номер должен быть длиной ровно 11 символов");
        printSerialNumberMenu();
        return true;
    }

    strlcpy(config.serial_number, serial.c_str(), sizeof(config.serial_number));
    saveConfig();

    Serial.println("\n[SERIAL] Серийный номер изменён.");
    Serial.printf("[SERIAL] Текущий серийный номер: %s\n", config.serial_number);

    engineeringSubMenu = ENG_SUBMENU_NONE;
    printEngineeringMenu();
    return true;
}

static bool handleHardwareMenu(const String &command) {
    if (handleEngineeringSubmenuNavigation(command)) return true;

    String cmd = command;
    cmd.trim();
    cmd.toLowerCase();

    if (cmd.equals("help") || cmd.equals("?")) {
        printHardwareMenu();
        return true;
    }

    if (cmd.startsWith("1 ")) {
        cmd = String("nix ") + cmd.substring(2);
        cmd.trim();
    }

    if (cmd.equals("2")) cmd = "nix hand";
    else if (cmd.equals("3")) cmd = "cycl";
    else if (cmd.equals("4")) cmd = "vert";
    else if (cmd.equals("5")) cmd = "mech pend";

    if (cmd.startsWith("nix")) {
        String arg = cmd.substring(3);
        arg.trim();

        if (arg.equals("hand")) {
            config.clock_type = CLOCK_TYPE_NIXIE_HAND;
            config.clock_digits = 2;
            saveConfig();
            Serial.println("\n[TYPE] Установлен тип: Nixie hand (2 разряда)");
            Serial.print("> ");
            return true;
        }

        if (arg.length() == 0) {
            Serial.println("\n[TYPE] Ошибка: укажите количество разрядов (1,2,4,6)");
            Serial.print("> ");
            return true;
        }

        int digits = arg.toInt();
        if (digits == 1 || digits == 2 || digits == 4 || digits == 6) {
            if (digits == 6) {
                engineeringSubMenu = ENG_SUBMENU_NIX6_OUTPUT;
                printNix6OutputMenu();
                return true;
            }

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

    if (cmd.equals("mech pend")) {
        config.clock_type = CLOCK_TYPE_MECH_PEND;
        saveConfig();
        Serial.println("\n[TYPE] Установлен тип: Mech pend");
        Serial.print("> ");
        return true;
    }

    Serial.println("Неизвестная команда. Введите 'help' для списка");
    Serial.print("> ");
    return true;
}

static bool handleTestingMenu(const String &command) {
    if (handleEngineeringSubmenuNavigation(command)) return true;

    String cmd = command;
    cmd.trim();
    cmd.toLowerCase();

    if (cmd.equals("help") || cmd.equals("?")) {
        printTestingMenu();
        return true;
    }

    Serial.println("-=В разработке=-");
    Serial.print("> ");
    return true;
}

static bool handleNix6OutputMenu(const String &command) {
    if (handleEngineeringSubmenuNavigation(command)) return true;

    String cmd = command;
    cmd.trim();
    cmd.toLowerCase();

    if (cmd.equals("help") || cmd.equals("?")) {
        printNix6OutputMenu();
        return true;
    }

    if (cmd.equals("1") || cmd.equals("2")) {
        config.clock_type = CLOCK_TYPE_NIXIE;
        config.clock_digits = 6;
        config.nix6_output_mode = cmd.equals("1") ? NIX6_OUTPUT_STD : NIX6_OUTPUT_REVERSE_INVERT;
        saveConfig();

        Serial.println("\n[TYPE] Выбор применён.");
        engineeringSubMenu = ENG_SUBMENU_NONE;
        printEngineeringMenu();
        return true;
    }

    Serial.println("Неизвестная команда. Введите 'help' для списка");
    Serial.print("> ");
    return true;
}

void enterEngineeringMenu() {
    inMenuMode = true;
    printEnabled = false;
    currentMenuState = MENU_ENGINEERING;

    // Автоматически открываем OTA окно на 1 час
    if (!otaIsEnabled()) {
        otaEnable(3600UL * 1000UL);
    }

    printEngineeringMenu();
}

void printEngineeringMenu() {
    Serial.println("\n\n=== ИНЖЕНЕРНОЕ МЕНЮ ===");
    Serial.println("Внимание: изменение параметров может повлиять на работу устройства.");
    Serial.println();
    Serial.println("1  Смена серийного номера");
    Serial.println("2  Настройки железа");
    Serial.println("3  Тестирование");
    printMappingMenuCommands();
    Serial.print("> ");
}

void handleEngineeringMenu(String command) {
    if (engineeringSubMenu == ENG_SUBMENU_SERIAL) {
        handleSerialNumberMenu(command);
        return;
    }
    if (engineeringSubMenu == ENG_SUBMENU_HARDWARE) {
        handleHardwareMenu(command);
        return;
    }
    if (engineeringSubMenu == ENG_SUBMENU_TESTING) {
        handleTestingMenu(command);
        return;
    }
    if (engineeringSubMenu == ENG_SUBMENU_NIX6_OUTPUT) {
        handleNix6OutputMenu(command);
        return;
    }

    if (handleCommonMenuCommands(command, printEngineeringMenu)) return;

    if (command.equals("1")) {
        engineeringSubMenu = ENG_SUBMENU_SERIAL;
        printSerialNumberMenu();
        return;
    } else if (command.equals("2")) {
        engineeringSubMenu = ENG_SUBMENU_HARDWARE;
        printHardwareMenu();
        return;
    } else if (command.equals("3")) {
        engineeringSubMenu = ENG_SUBMENU_TESTING;
        printTestingMenu();
        return;
    } else if (command.equals("time") || command.equals("t")) {
        printTime();
        return;
    } else {
        Serial.println("Неизвестная команда. Введите 'help' для списка");
    }

    Serial.print("> ");
}
