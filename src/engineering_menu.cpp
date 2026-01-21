#include "engineering_menu.h"
#include "menu_manager.h"
#include "config.h"
#include "hardware.h"
#include "time_utils.h"

extern bool printEnabled;

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
    Serial.println("1  Тип индикатора и количество разрядов (подменю)");
    Serial.println("2  Тип выходных данных на дисплей");
    Serial.println("3  Проверка дисплея");
    Serial.println("4  Смена серийного номера");
    Serial.println("5  Дополнительные модули");
    Serial.println("6  Специфические настройки");
    Serial.println();
    printMappingMenuCommands();
    Serial.print("> ");
}

void handleEngineeringMenu(String command) {
    if (handleCommonMenuCommands(command, printEngineeringMenu)) return;

    if (command.equals("1")) {
        Serial.println("\n[ENG] Подменю индикатора — в разработке");
    } else if (command.equals("2")) {
        Serial.println("\n[ENG] Тип выходных данных — в разработке");
    } else if (command.equals("3")) {
        Serial.println("\n[ENG] Проверка дисплея — в разработке");
    } else if (command.equals("4")) {
        Serial.printf("\n[ENG] Текущий серийный номер: %s\n", config.serial_number);
        Serial.println("[ENG] Смена серийного номера — в разработке");
    } else if (command.equals("5")) {
        Serial.println("\n[ENG] Дополнительные модули — в разработке");
    } else if (command.equals("6")) {
        Serial.println("\n[ENG] Специфические настройки — в разработке");
    } else if (command.equals("time") || command.equals("t")) {
        printTime();
        return;
    } else {
        Serial.println("Неизвестная команда. Введите 'help' для списка");
    }

    Serial.print("> ");
}
