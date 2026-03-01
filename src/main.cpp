#include <Arduino.h>
#include "config.h"
#include "hardware.h"
#include "command_handler.h"
#include "menu_manager.h"  
#include "time_utils.h"
#include "alarm_handler.h"
#include "dfplayer_manager.h"
#include "ble_terminal.h"
#include "ota_manager.h"
#include "display/display_manager.h"
#include <esp_system.h>

static bool sqwFailed = false;
extern bool printEnabled;
static DisplayManager displayManager;

void processSecondTick();

void setup() {
    delay(2000); // Небольшая задержка для корректной работы UART при старте
    initHardware();
    Serial.printf("\n[BOOT] reset reason: %d", (int)esp_reset_reason());
    initConfiguration();
    displayManager.begin();
    initNTPClient();
    checkTimeSource(); 
    printDS3231Temperature();

    // BLE включен по умолчанию (команды ble on/off остаются рабочими)
    bleTerminalEnable();
    otaInit();

    // initDFPlayer(); // времено отключено для теста
    
    // DEBUG: Асинхронная синхронизация - не блокирует setup()
    syncTimeAsync();
    
    Serial.print("\n\n=== Система готова ===");
    Serial.println("\n\nhelp / ? - Перечень доступных команд");
    
    // Инициализация меню (флаги уже инициализированы в menu_manager.cpp)
    printEnabled = true;
}

void loop() {
    static unsigned long lastSecondCheck = 0;
    static unsigned long lastSQWCheck = 0;
    static bool sqwMonitorArmed = false;
    unsigned long currentMillis = millis();
   
    // Обработка команд
    if (Serial.available()) {
        String command = Serial.readStringUntil('\n');
        command.trim();
        handleCommand(command);
    }

    bleTerminalProcess();
    otaProcess();

    // Во время передачи OTA приоритет — сетевой стек/обработчик OTA.
    // Пропускаем тяжёлую логику цикла, чтобы не получить таймаут upload.
    if (otaIsBusy()) {
        delay(2);
        return;
    }

    if (bleTerminalHasCommand()) {
        String bleCommand = bleTerminalReadCommand();
        bleCommand.trim();
        if (bleCommand.length() > 0) {
            bleTerminalLog(String("\n[Bluetooth] > ") + bleCommand + "\n");
            handleCommand(bleCommand);
            bleTerminalLog("[Bluetooth] OK\n");
        }
    }
 
    // === ОБРАБОТКА СЕКУНДНЫХ СОБЫТИЙ ===
    // Работает всегда, чтобы индикация на дисплее оставалась актуальной и в меню.
    // printEnabled управляет только авто-выводом в терминал.
    if (ds3231_available) {
        if (!sqwMonitorArmed) {
            // DS3231 только что стал доступен: даём окну контроля стартовать от "сейчас",
            // чтобы не получить ложный WARN из-за lastSQWCheck == 0 после загрузки.
            lastSQWCheck = currentMillis;
            sqwFailed = false;
            sqwMonitorArmed = true;
        }

        if (timeUpdatedFromSQW) {
            portENTER_CRITICAL(&timerMux);
            timeUpdatedFromSQW = false;
            portEXIT_CRITICAL(&timerMux);
            lastSQWCheck = currentMillis;
            sqwFailed = false;
            processSecondTick();
        }
        else if (!sqwFailed && (currentMillis - lastSQWCheck >= 3000)) {
            sqwFailed = true;
            Serial.print("\n[WARN] SQW не поступает 3 сек, переход на millis!");
            lastSecondCheck = currentMillis;
            processSecondTick();
        }
        else if (sqwFailed && (currentMillis - lastSecondCheck >= 1000)) {
            lastSecondCheck = currentMillis;
            processSecondTick();
        }
    }
    else {
        sqwMonitorArmed = false;
        sqwFailed = false;
        if (currentMillis - lastSecondCheck >= 1000) {
            lastSQWCheck = currentMillis;
            lastSecondCheck = currentMillis;
            processSecondTick();
        }
    }
    
    delay(10);
}

void processSecondTick() {
    static bool tickUtcInitialized = false;
    static bool lastDsState = false;
    static time_t tickUtc = 0;
    static time_t lastFiveMinuteSyncMarkUtc = 0;

    // Инициализация/реинициализация базового UTC из активного источника
    if (!tickUtcInitialized || lastDsState != ds3231_available) {
        tickUtc = getCurrentUTCTime();
        tickUtcInitialized = true;
        lastDsState = ds3231_available;
    } else {
        // Основной режим: считаем секунды локально, без чтения RTC каждую секунду
        tickUtc += 1;
    }

    // Контрольная сверка строго в 00 секунд каждых 5 минут: 00, 05, 10, ...
    // Источник сверки = текущий активный источник времени (DS3231 или System RTC)
    struct tm tickUtcTm;
    gmtime_r(&tickUtc, &tickUtcTm);
    if (tickUtcTm.tm_sec == 0 && (tickUtcTm.tm_min % 5) == 0 && tickUtc != lastFiveMinuteSyncMarkUtc) {
        tickUtc = getCurrentUTCTime();
        lastFiveMinuteSyncMarkUtc = tickUtc;

        if (printEnabled) {
            if (currentTimeSource == EXTERNAL_DS3231 && ds3231_available) {
                Serial.print("\n[DS3231] Updated");
            } else {
                Serial.print("\n[RTC] Updated");
            }
        }
    }

    time_t currentTime = tickUtc;
    time_t localTime = utcToLocal(currentTime);
    
    struct tm* tm_info = gmtime(&currentTime);
    struct tm local_tm_info;
    gmtime_r(&localTime, &local_tm_info);
    uint8_t currentSecond = tm_info->tm_sec;

    // Универсальное обновление дисплея по политике типа часов
    if (displayManager.shouldUpdateOnSecond(currentSecond)) {
        displayManager.updateFromLocalTime(local_tm_info, millis(),
                                           config.alarm1.hour, config.alarm1.minute,
                                           config.alarm2.hour, config.alarm2.minute);
    }

    const DisplayDebugInfo dispDbg = displayManager.getDebugInfo();
    
    // Индикация работы
    if (printEnabled) {
        if (ds3231_available && !sqwFailed) {
            Serial.print("/");
        } else {
            Serial.print("*");
        }
        
        if (currentSecond % 20 == 0) {
            if(ds3231_available && sqwFailed) {
                Serial.print("\n[WARN] SQW не доступен");    
            }
            printTime();
            if (!dispDbg.available) {
                Serial.printf("\n [DISP] backend not implemented for type=%u digits=%u",
                              static_cast<unsigned>(config.clock_type),
                              static_cast<unsigned>(config.clock_digits));
            } else if (dispDbg.reverseFormat) {
                Serial.printf("\n [DISP] %02X:%02X:%02X 0x%02X", dispDbg.hh, dispDbg.mm, dispDbg.ss, dispDbg.status);
            } else {
                Serial.printf("\n [DISP] 0x%02X %02X:%02X:%02X", dispDbg.status, dispDbg.hh, dispDbg.mm, dispDbg.ss);
            }
        }
    }
    
    checkAlarms();
    
    // Синхронизация
    static uint8_t lastSyncHour = 255;
    if (!otaIsEnabled() && (local_tm_info.tm_hour == 3 || local_tm_info.tm_hour == 15) && local_tm_info.tm_min == 5) {
        if (local_tm_info.tm_hour != lastSyncHour) {
            syncTimeAsync();
            lastSyncHour = local_tm_info.tm_hour;
        }
    }
}