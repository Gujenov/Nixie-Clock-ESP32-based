#pragma once

#include <Arduino.h>
#include <time.h>
#include "config.h"

// Базовый интерфейс дисплея
class DisplayDriver {
public:
    virtual ~DisplayDriver() = default;
    virtual void begin() = 0;
    virtual void setBrightness(uint8_t level) = 0;
    virtual void showTime(uint8_t hours, uint8_t minutes, uint8_t seconds, bool showColon) = 0;
    virtual void testPattern() = 0;
};

// Менеджер дисплея (пока один драйвер)
enum class DisplayTickMode : uint8_t {
    EverySecond = 0,
    EveryMinute,
    EventOnly
};

enum class DisplayAction : uint8_t {
    None = 0,
    NextMainView,      // Переключение основной ветки (time/date/alarm...)
    NextAuxView,       // Переключение вспомогательной ветки
    EnterEditMode,     // Вход в режим редактирования
    ExitEditMode,      // Выход из режима редактирования
    TestPattern        // Тестовый шаблон
};

struct DisplayDebugInfo {
    bool available = false;
    bool reverseFormat = false; // true => печатать как HH:MM:SS 0xSTATUS
    uint8_t status = 0;
    uint8_t hh = 0;
    uint8_t mm = 0;
    uint8_t ss = 0;
};

class DisplayManager {
public:
    void begin();
    void setBrightness(uint8_t level);
    void showTime(uint8_t hours, uint8_t minutes, uint8_t seconds, bool showColon);
    void showUniformDigits(uint8_t digit);
    void testPattern();

    // Универсальный апдейт из processSecondTick()
    void updateFromLocalTime(const tm& localTm, uint32_t nowMs,
                             uint8_t alarm1Hour, uint8_t alarm1Minute,
                             uint8_t alarm2Hour, uint8_t alarm2Minute);

    // Фоновое обслуживание неблокирующих анимаций отображения (soft-transition).
    void serviceAnimations(uint32_t nowMs);
    void stopAnimations();
    bool hasActiveAnimation() const;

    // Возможности активного backend'а
    bool isNixieClockType() const;
    bool supportsSoftTransition() const;
    bool supportsAntiPoison() const;
    bool isTimeViewActive() const;

    // Унифицированный runtime anti-poison (для неподдерживаемых backend'ов — no-op)
    bool isAntiPoisonActive() const;
    bool startAntiPoison();
    void stopAntiPoison();
    void serviceAntiPoison(uint32_t nowMs);

    // OTA-маркер старта передачи (актуально только для Nixie6)
    void showOtaTransferStartMarker();

    // Политика активности/гашения дисплея (расписание + временная ручная активация)
    bool isDisplayActiveBySchedule(const tm& localTm) const;
    bool isDisplayActiveNow(const tm& localTm);
    void triggerSleepOverrideIfNeeded(const tm& localTm);
    bool isSleepOverrideActive() const;

    // Форматирование расписания (склейка интервалов для вывода)
    void formatDisplayActivityIntervalsForWorkdays(char* out, size_t outSize) const;
    void formatDisplayActivityIntervalsForHolidays(char* out, size_t outSize) const;
    void formatDisplayActivityIntervalsForCurrentDay(const tm& localTm, char* out, size_t outSize) const;

    // Политика выполнения тика для разных типов часов
    bool shouldUpdateOnSecond(uint8_t second) const;
    DisplayTickMode tickMode() const;

    // Единая диагностика выводимого кадра
    DisplayDebugInfo getDebugInfo() const;

    // Слой событий для кнопок/энкодера
    bool handleAction(DisplayAction action);

    bool hasActiveDriver() const;
    const char* activeBackendName() const;
    const char* activeViewName() const;

private:
    enum class ActiveBackend : uint8_t {
        None = 0,
        Nixie6,
        NixieGeneric,
        NixieHand,
        Cyclotron,
        Vertical,
        Mech2,
        MechPend
    };

    void attach(DisplayDriver* driver);

    DisplayDriver* driver_ = nullptr;
    ActiveBackend activeBackend_ = ActiveBackend::None;
    bool isNixie6_ = false;
    DisplayTickMode tickMode_ = DisplayTickMode::EverySecond;

    // Debug fallback для backend'ов-заглушек
    uint8_t debugStatus_ = 0x00;
    uint8_t debugHh_ = 0;
    uint8_t debugMm_ = 0;
    uint8_t debugSs_ = 0;
    bool debugAvailable_ = false;

    bool sleepOverrideUntilSchedule_ = false;

    struct AntiPoisonRuntimeState {
        bool active = false;
        uint8_t pass = 0;
        uint8_t digit = 1;
        uint32_t nextStepMs = 0;
    } antiPoisonState_;

    static bool isHourInHalfOpenRange(uint8_t hour, uint8_t startHour, uint8_t endHour);
    static void formatDisplayActivityIntervalsInternal(uint8_t start1,
                                                       uint8_t end1,
                                                       uint8_t start2,
                                                       uint8_t end2,
                                                       char* out,
                                                       size_t outSize);
    static bool isHolidayDay(const tm& localTm);
};
