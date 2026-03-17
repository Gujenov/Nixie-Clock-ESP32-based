#include "command_handler.h"
#include "config.h"
#include "time_utils.h"
#include "alarm_handler.h"
#include "hardware.h"
#include "menu_manager.h"
#include "engineering_menu.h"
#include "ble_terminal.h"
#include "ota_manager.h"
#include "audio_task.h"
#include "runtime_counter.h"
#include <esp_system.h>

// Объявляем внешние переменные
extern WiFiUDP ntpUDP;
extern NTPClient *timeClient;
extern HardwareSource currentTimeSource;
extern bool ds3231_available;
extern void processSecondTick();
extern void runAntiPoisonNow();

void handleCommand(String command) {
    command.trim();

    // Управление BLE-монитором (доступно всегда, даже внутри меню)
    if (command.equalsIgnoreCase("bon")) {
        bleTerminalEnable();
        bleTerminalLog("\n[BLE] enabled\n");
        (void)audioPlaySfx(AudioSfxId::BleEnabled);
        return;
    }
    if (command.equalsIgnoreCase("boff")) {
        bleTerminalLog("\n[BLE] disabling...\n");
        (void)audioPlaySfx(AudioSfxId::BleDisabled);
        bleTerminalDisable();
        return;
    }
    if (command.equalsIgnoreCase("bdbg on")) {
        bleTerminalSetDebug(true);
        bleTerminalLog("\n[BLE-DBG] ON\n");
        return;
    }
    if (command.equalsIgnoreCase("bdbg off")) {
        bleTerminalLog("\n[BLE-DBG] OFF\n");
        bleTerminalSetDebug(false);
        return;
    }
    if (command.equalsIgnoreCase("reset") || command.equalsIgnoreCase("rst") || command.equalsIgnoreCase("reboot")) {
        Serial.println("\n[SYSTEM] Перезагрузка...");
        bleTerminalLog("\n[BLE] rebooting...");
        (void)runtimeCounterSaveNow();
        delay(100);
        ESP.restart();
        return;
    }
    if (command.equalsIgnoreCase("ota on")) {
        if (otaEnable()) {
            Serial.println("\n[OTA] Режим обновления активирован");
        }
        return;
    }
    if (command.equalsIgnoreCase("ota off")) {
        otaDisable();
        return;
    }
    if (command.equalsIgnoreCase("ota status")) {
        if (otaIsEnabled()) {
            Serial.printf("[OTA] ON, окно: %lu сек\n", static_cast<unsigned long>(otaSecondsLeft()));
        } else {
            Serial.println("[OTA] OFF");
        }
        return;
    }
    if (command.equalsIgnoreCase("antipoison") || command.equalsIgnoreCase("a")) {
        runAntiPoisonNow();
        return;
    }

    // Дублируем команду в BLE, если он включен
    if (bleTerminalIsEnabled() && command.length() > 0) {
        bleTerminalLog(String("\n> ") + command + "\n");
    }
    
    // Если в режиме меню - передаём в менеджер меню
    if (inMenuMode) {
        switch (currentMenuState) {
            case MENU_MAIN: handleMainMenu(command); break;
            case MENU_TIME: handleTimeMenu(command); break;
            case MENU_ALARMS: handleAlarmMenu(command); break;
            case MENU_WIFI: handleWifiMenu(command); break;
            case MENU_DISPLAY: handleDisplayMenu(command); break;
            case MENU_INFO: handleInfoMenu(command); break;
            case MENU_CONFIG: handleConfigMenu(command); break;
            case MENU_ENGINEERING: handleEngineeringMenu(command); break;
            default: break;
        }
        return;
    }
    
    // Базовые команды (работают без меню)
    if (command.equals("help") || command.equals("?")) {
        printQuickHelp();
    }
    else if (command.equals("time") || command.equals("t")) {
        printTime();
    }
    else if (command.equals("menu") || command.equals("m")) {
        enterMenuMode();
    }
    else if (command.equals("em")) {
        enterEngineeringMenu();
    }
    else if (command.equals("sync")) {
        if (otaIsEnabled()) {
            Serial.println("[SYNC] Заблокировано: OTA активен");
            return;
        }
        syncTimeAsync(true);
    }
    // Команды установки UTC времени и даты
    else if (command.startsWith("set UTC T ") || command.startsWith("SUT ")) {
        String timeStr = command.startsWith("set UTC T ") ? command.substring(10) : command.substring(4);
        if (setManualTime(timeStr)) {
            processSecondTick();
        }
    }
    else if (command.startsWith("set UTC D ") || command.startsWith("SUD ")) {
        String dateStr = command.startsWith("set UTC D ") ? command.substring(10) : command.substring(4);
        if (setManualDate(dateStr)) {
            processSecondTick();
        }
    }
    // Команды установки локального времени и даты
    else if (command.startsWith("set local T ") || command.startsWith("SLT ")) {
        String timeStr = command.startsWith("set local T ") ? command.substring(12) : command.substring(4);
        if (setManualLocalTime(timeStr)) {
            processSecondTick();
        }
    }
    else if (command.startsWith("set local D ") || command.startsWith("SLD ")) {
        String dateStr = command.startsWith("set local D ") ? command.substring(12) : command.substring(4);
        if (setManualLocalDate(dateStr)) {
            processSecondTick();
        }
    }
    // Команды автосинхронизации
    else if (command.equalsIgnoreCase("auto sync en") || command.equalsIgnoreCase("ASE")) {
        config.time_config.auto_sync_enabled = true;
        saveConfig();
        Serial.println("\nАвтоматическая синхронизация времени ВКЛЮЧЕНА");
    }
    else if (command.equalsIgnoreCase("auto sync dis") || command.equalsIgnoreCase("ASD")) {
        config.time_config.auto_sync_enabled = false;
        saveConfig();
        Serial.println("\nАвтоматическая синхронизация времени ОТКЛЮЧЕНА");
        Serial.println("Время можно установить вручную или синхронизировать командой 'sync'");
    }
    else {
        Serial.println("Неизвестная команда. Введите 'help' для справки");
        Serial.println("Или введите 'menu' для входа в режим настройки");
        (void)audioPlaySfx(AudioSfxId::OperationError);
    }
}