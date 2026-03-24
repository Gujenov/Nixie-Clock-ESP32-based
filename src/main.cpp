#include <Arduino.h>
#include "config.h"
#include "hardware.h"
#include "command_handler.h"
#include "menu_manager.h"  
#include "time_utils.h"
#include "alarm_handler.h"
#include "audio_task.h"
#include "chime_scheduler.h"
#include "ble_terminal.h"
#include "ota_manager.h"
#include "display/display_manager.h"
#include "input_handler.h"
#include "platform_profile.h"
#include "runtime_counter.h"
#include <esp_system.h>

static bool sqwFailed = false;
extern bool printEnabled;
static DisplayManager displayManager;
static bool displayEditMode = false;

static const char* resetReasonText(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_UNKNOWN:   return "UNKNOWN";
        case ESP_RST_POWERON:   return "POWERON";
        case ESP_RST_EXT:       return "EXTERNAL";
        case ESP_RST_SW:        return "SOFTWARE";
        case ESP_RST_PANIC:     return "PANIC";
        case ESP_RST_INT_WDT:   return "INT_WDT";
        case ESP_RST_TASK_WDT:  return "TASK_WDT";
        case ESP_RST_WDT:       return "WDT";
        case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
        case ESP_RST_BROWNOUT:  return "BROWNOUT";
        case ESP_RST_SDIO:      return "SDIO";
        default:                return "UNMAPPED";
    }
}

struct AntiPoisonState {
    bool active = false;
    uint8_t pass = 0;
    uint8_t digit = 1;
    uint32_t nextStepMs = 0;
};

static AntiPoisonState antiPoisonState;

static void triggerDisplaySleepOverrideIfNeeded() {
    const time_t utcNow = getCurrentUTCTime();
    const time_t localNow = utcToLocal(utcNow);
    tm localTm{};
    gmtime_r(&localNow, &localTm);

    if (!displayManager.isSleepOverrideActive() && !displayManager.isDisplayActiveBySchedule(localTm)) {
        displayManager.triggerSleepOverrideIfNeeded(localTm);
        Serial.print("\n[DISP] Ручная активация: выход из текущего цикла гашения");
    }
}

static void printRuntimeStateSnapshot() {
    const time_t utcNow = getCurrentUTCTime();
    const time_t localNow = utcToLocal(utcNow);
    tm localTm{};
    gmtime_r(&localNow, &localTm);

    const bool displayActiveNow = displayManager.isDisplayActiveNow(localTm);
    const bool bellEnabled = bellSchedulerIsEnabled();
    const bool bellWindowActive = bellSchedulerIsWindowActive(localTm);
    char displayIntervalsCurrent[40] = {0};
    char displayIntervalsWorkdays[40] = {0};
    char displayIntervalsHolidays[40] = {0};
    displayManager.formatDisplayActivityIntervalsForCurrentDay(localTm,
                                                               displayIntervalsCurrent,
                                                               sizeof(displayIntervalsCurrent));
    displayManager.formatDisplayActivityIntervalsForWorkdays(displayIntervalsWorkdays,
                                                             sizeof(displayIntervalsWorkdays));
    displayManager.formatDisplayActivityIntervalsForHolidays(displayIntervalsHolidays,
                                                             sizeof(displayIntervalsHolidays));

    Serial.print("\nТекущее состояние часов:");
    Serial.printf("\n[DISP] %s", displayActiveNow ? "Дисплей активен" : "Режим сна для дисплея (дисплей неактивен)");
    Serial.printf("\n[DISP] Текущее время активности дисплея: %s", displayIntervalsCurrent);
    Serial.printf("\n[DISP] Настройка будни: %s", displayIntervalsWorkdays);
    Serial.printf("\n[DISP] Настройка выходные: %s", displayIntervalsHolidays);

    Serial.printf("\n[BELL] %s", bellEnabled ? "Бой включен в настройках" : "Бой отключен в настройках");
    if (bellEnabled) {
        if (bellWindowActive) {
            Serial.printf("\n[BELL] Время активности боя: %u-%u",
                          static_cast<unsigned>(config.chime_active_start_hour),
                          static_cast<unsigned>(config.chime_active_end_hour));
        } else {
            Serial.printf("\n[BELL] Активен режим тишины - бой не активен в данное время (окно %u-%u)",
                          static_cast<unsigned>(config.chime_active_start_hour),
                          static_cast<unsigned>(config.chime_active_end_hour));
        }
    }
}

static bool isAntiPoisonActive() {
    return antiPoisonState.active;
}

static bool startCathodesAntiPoisonProcedure() {
    if (antiPoisonState.active) {
        return false;
    }

    antiPoisonState.active = true;
    antiPoisonState.pass = 0;
    antiPoisonState.digit = 1;
    antiPoisonState.nextStepMs = 0;
    Serial.println("Kathodes anti-poison procedure started");
    return true;
}

static bool ensureDisplayActiveForManualAntiPoison() {
    const time_t utcNow = getCurrentUTCTime();
    const time_t localNow = utcToLocal(utcNow);
    tm localTm{};
    gmtime_r(&localNow, &localTm);

    const bool displayActiveNow = displayManager.isDisplayActiveNow(localTm);
    if (!displayActiveNow) {
        displayManager.triggerSleepOverrideIfNeeded(localTm);
        Serial.println("[ANTIPOISON] Ручной запуск: выхожу из режима гашения дисплея");
    }

    if (!isDisplayOutputEnabled()) {
        setDisplayOutputEnabled(true);
    }

    return true;
}

static void serviceCathodesAntiPoisonProcedure(uint32_t nowMs) {
    if (!antiPoisonState.active) {
        return;
    }

    if (antiPoisonState.nextStepMs != 0 && nowMs < antiPoisonState.nextStepMs) {
        return;
    }

    displayManager.showUniformDigits(antiPoisonState.digit);
    antiPoisonState.nextStepMs = nowMs + 500;

    antiPoisonState.digit++;
    if (antiPoisonState.digit > 9) {
        antiPoisonState.digit = 1;
        antiPoisonState.pass++;
        if (antiPoisonState.pass >= 2) {
            antiPoisonState.active = false;
            Serial.println("Kathodes anti-poison procedure run");
        }
    }
}

static bool isNixieCathodeDisplay() {
    return (config.clock_type == CLOCK_TYPE_NIXIE || config.clock_type == CLOCK_TYPE_NIXIE_HAND);
}

void runAntiPoisonNow() {
    if (!isNixieCathodeDisplay()) {
        Serial.println("[ANTIPOISON] Пропущено: режим не Nixie");
        return;
    }
    if (!ensureDisplayActiveForManualAntiPoison()) {
        Serial.println("[ANTIPOISON] Ручной запуск невозможен: дисплей не активирован");
        return;
    }
    if (!startCathodesAntiPoisonProcedure()) {
        Serial.println("[ANTIPOISON] Процедура уже выполняется");
    }
}

void processSecondTick();

static void processSecondTicksByMillis(unsigned long currentMillis, unsigned long& lastSecondCheck) {
    if (lastSecondCheck == 0) {
        lastSecondCheck = currentMillis;
        return;
    }

    const unsigned long elapsed = currentMillis - lastSecondCheck;
    if (elapsed < 1000) {
        return;
    }

    uint32_t ticksToRun = elapsed / 1000;
    if (ticksToRun > 1) {
        // Если пропущены секунды, берём опорное время заново из активного источника.
        requestTimeTickResync();
    }

    const uint32_t maxCatchUpTicks = 8;
    if (ticksToRun > maxCatchUpTicks) {
        ticksToRun = maxCatchUpTicks;
    }

    for (uint32_t i = 0; i < ticksToRun; ++i) {
        processSecondTick();
    }

    lastSecondCheck += ticksToRun * 1000;
    if (currentMillis - lastSecondCheck > 5000) {
        lastSecondCheck = currentMillis;
    }
}

static void handleDisplayButtonAction(uint8_t buttonEvent) {
    const PlatformCapabilities& caps = platformGetCapabilities();
    if (!caps.controls_enabled || !caps.button_enabled) {
        return;
    }

    if (!caps.display_navigation_enabled) {
        return;
    }

    switch (buttonEvent) {
        case BUTTON_PRESSED:
            displayManager.handleAction(DisplayAction::NextMainView);
            break;
        case BUTTON_LONG:
            displayManager.handleAction(DisplayAction::NextAuxView);
            break;
        case BUTTON_VERY_LONG:
            if (displayEditMode) {
                displayManager.handleAction(DisplayAction::ExitEditMode);
                displayEditMode = false;
            } else {
                displayManager.handleAction(DisplayAction::EnterEditMode);
                displayEditMode = true;
            }
            break;
        default:
            break;
    }
}

static void onButtonEvent(uint8_t buttonEvent) {
    triggerDisplaySleepOverrideIfNeeded();
    handleDisplayButtonAction(buttonEvent);
}

static void onAlarmButtonEvent(uint8_t buttonEvent) {
    triggerDisplaySleepOverrideIfNeeded();
    handleDisplayButtonAction(buttonEvent);

    switch (buttonEvent) {
        case BUTTON_PRESSED:
            Serial.printf("\n[ALARM_BTN] Short press - %s active\n", displayManager.activeViewName());
            break;
        case BUTTON_LONG:
            Serial.printf("\n[ALARM_BTN] Long press (1s) - %s active\n", displayManager.activeViewName());
            break;
        case BUTTON_VERY_LONG:
            Serial.printf("\n[ALARM_BTN] Very long press (3s) - %s active\n", displayManager.activeViewName());
            break;
        default:
            break;
    }
}

static void onEncoderEvent(int32_t delta, int32_t position) {
    (void)position;

    triggerDisplaySleepOverrideIfNeeded();

    const PlatformCapabilities& caps = platformGetCapabilities();
    if (!caps.controls_enabled || !caps.encoder_enabled) {
        return;
    }

    if (!caps.display_navigation_enabled) {
        return;
    }

    if (delta > 0) {
        displayManager.handleAction(DisplayAction::NextMainView);
    } else if (delta < 0) {
        displayManager.handleAction(DisplayAction::NextAuxView);
    }
}

void setup() {
    delay(2000); // Небольшая задержка для корректной работы UART при старте
    initHardware();
    const esp_reset_reason_t rr = esp_reset_reason();
    Serial.printf("\n[BOOT] reset reason: %s", resetReasonText(rr));
    initConfiguration();
    runtimeCounterInit();
    platformRefreshCapabilities();
    displayManager.begin();
    initNTPClient();
    checkTimeSource(); 
    printDS3231Temperature();
    printESP32Temperature();

    // BLE включен по умолчанию (команды ble on/off остаются рабочими)
    bleTerminalEnable();
    otaInit();
    
    initInputHandler();
    setButtonCallback(onButtonEvent);
    setAlarmButtonCallback(onAlarmButtonEvent);
    setEncoderCallback(onEncoderEvent);

        printRuntimeStateSnapshot();
    
    // Асинхронная синхронизация - не блокирует setup()
    syncTimeAsync();

    if (platformGetCapabilities().sound_enabled) {
        audioTaskStart();
    } else {
        Serial.print("\n[AUDIO] Подсистема отключена, audioTask не запущена");
    }

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

    // Во время OTA-окна приоритет — сетевой стек/обработчик OTA.
    // Это снижает риск подвисаний после auth, до старта передачи данных.
    if (otaIsEnabled()) {
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

    const PlatformCapabilities& caps = platformGetCapabilities();
    if (caps.controls_enabled) {
        processAllInputs();
    }

    serviceCathodesAntiPoisonProcedure(currentMillis);
    chimeSchedulerService();
 
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
            lastSecondCheck = currentMillis;
            sqwFailed = false;
            processSecondTick();
        }
        else if (!sqwFailed && (currentMillis - lastSQWCheck >= 6000)) {
            sqwFailed = true;
            Serial.print("\n[WARN] SQW не поступает 6 сек, переход на millis!");
            requestTimeTickResync();
            lastSecondCheck = currentMillis;
            processSecondTick();
        }
        else if (sqwFailed) {
            processSecondTicksByMillis(currentMillis, lastSecondCheck);
        }
    }
    else {
        sqwMonitorArmed = false;
        sqwFailed = false;
        lastSQWCheck = currentMillis;
        processSecondTicksByMillis(currentMillis, lastSecondCheck);
    }
    
    delay(10);
}

void processSecondTick() {
    static bool tickUtcInitialized = false;
    static bool lastDsState = false;
    static time_t tickUtc = 0;
    static time_t lastFiveMinuteSyncMarkUtc = 0;
    static int32_t lastAntiPoisonHourMarker = -1;
    static bool displayStateInitialized = false;
    static bool prevDisplayShouldBeEnabled = true;
    static bool bellStateInitialized = false;
    static bool prevBellEnabled = false;
    static bool prevBellActive = false;
    const bool timeAdjustedNow = consumeTimeAdjustedFlag();

    // Инициализация/реинициализация базового UTC из активного источника
    if (!tickUtcInitialized || lastDsState != ds3231_available || timeAdjustedNow) {
        tickUtc = getCurrentUTCTime();
        tickUtcInitialized = true;
        lastDsState = ds3231_available;
    } else {
        // Основной режим: считаем секунды локально, без чтения RTC каждую секунду
        tickUtc += 1;
    }

    runtimeCounterOnSecondTick();

    // Контрольная сверка строго в 00 секунд каждых 5 минут: 00, 05, 10, ...
    // Источник сверки = текущий активный источник времени (DS3231 или System RTC)
    struct tm tickUtcTm;
    gmtime_r(&tickUtc, &tickUtcTm);
    if (tickUtcTm.tm_sec == 0 && (tickUtcTm.tm_min % 5) == 0 && tickUtc != lastFiveMinuteSyncMarkUtc) {
        tickUtc = getCurrentUTCTime();
        lastFiveMinuteSyncMarkUtc = tickUtc;

        if (printEnabled) {
            if (currentTimeSource == EXTERNAL_DS3231 && ds3231_available) {
                Serial.print("\n[DS3231] RTC time read");
            } else {
                Serial.print("\n[RTC] RTC time read");
            }
        }
    }

    time_t currentTime = tickUtc;
    time_t localTime = utcToLocal(currentTime);
    
    struct tm* tm_info = gmtime(&currentTime);
    struct tm local_tm_info;
    gmtime_r(&localTime, &local_tm_info);
    uint8_t currentSecond = tm_info->tm_sec;
    const bool displayActiveByScheduleNow = displayManager.isDisplayActiveBySchedule(local_tm_info);
    const bool overrideWasActive = displayManager.isSleepOverrideActive();
    const bool displayActiveNow = displayManager.isDisplayActiveNow(local_tm_info);
    if (overrideWasActive && !displayManager.isSleepOverrideActive() && displayActiveByScheduleNow) {
        Serial.println("\n[DISP] Ручная активация сброшена: наступил штатный интервал активности");
    }

    const int32_t hourMarker = (local_tm_info.tm_yday * 24) + local_tm_info.tm_hour;
    const bool shouldRunAntiPoison = isNixieCathodeDisplay() &&
                                     displayActiveNow &&
                                     local_tm_info.tm_min == 0 &&
                                     local_tm_info.tm_sec == 1 &&
                                     hourMarker != lastAntiPoisonHourMarker;

    if (shouldRunAntiPoison) {
        if (startCathodesAntiPoisonProcedure()) {
            lastAntiPoisonHourMarker = hourMarker;
        }
    }

    chimeSchedulerOnTick(local_tm_info);

    const bool displayShouldBeEnabled = displayActiveNow || isAntiPoisonActive();
    if (!displayStateInitialized) {
        prevDisplayShouldBeEnabled = displayShouldBeEnabled;
        displayStateInitialized = true;
    } else if (prevDisplayShouldBeEnabled != displayShouldBeEnabled) {
        if (displayShouldBeEnabled) {
            Serial.println("\n[DISP] Дисплей активен - выход из режима гашения");
        } else {
            Serial.println("\n[DISP] Активен режим гашения индикаторов");
        }
        prevDisplayShouldBeEnabled = displayShouldBeEnabled;
    }

    if (isDisplayOutputEnabled() != displayShouldBeEnabled) {
        setDisplayOutputEnabled(displayShouldBeEnabled);
    }

    const bool bellEnabled = bellSchedulerIsEnabled();
    const bool bellActive = bellSchedulerIsActiveNow(local_tm_info);
    if (!bellStateInitialized) {
        prevBellEnabled = bellEnabled;
        prevBellActive = bellActive;
        bellStateInitialized = true;
    } else if (prevBellEnabled != bellEnabled || prevBellActive != bellActive) {
        if (!bellEnabled) {
            Serial.println("\n[BELL] Бой отключен в настройках");
        } else if (!bellActive) {
            Serial.println("\n[BELL] Активен режим тишины - бой временно неактивен");
        } else if (prevBellEnabled && !prevBellActive) {
            Serial.println("\n[BELL] Режим боя восстановлен - выход из режима тишины");
        } else {
            Serial.println("\n[BELL] Бой включен в настройках и активен по расписанию");
        }
        prevBellEnabled = bellEnabled;
        prevBellActive = bellActive;
    }

    // Универсальное обновление дисплея по политике типа часов
    if (isAntiPoisonActive()) {
        // Пока идет антиотравление, не перетираем цифры штатным обновлением.
    } else if (displayActiveNow && displayManager.shouldUpdateOnSecond(currentSecond)) {
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
            if (!displayShouldBeEnabled) {
                Serial.print("\n [DISP] Погашен");
            } else if (!dispDbg.available) {
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