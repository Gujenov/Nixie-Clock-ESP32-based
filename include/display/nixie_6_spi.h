#pragma once

#include <Arduino.h>
#include <time.h>
#include "display/display_manager.h"

// 32-битный кадр: 8 бит флагов + 6 нибблов (24 бита)
struct Nixie6Frame {
    uint8_t startFlags = 0;
    uint8_t nibbles[6] = {0, 0, 0, 0, 0, 0};

    uint32_t pack() const {
        return (static_cast<uint32_t>(startFlags) << 24) |
               (static_cast<uint32_t>(nibbles[0] & 0x0F) << 20) |
               (static_cast<uint32_t>(nibbles[1] & 0x0F) << 16) |
               (static_cast<uint32_t>(nibbles[2] & 0x0F) << 12) |
               (static_cast<uint32_t>(nibbles[3] & 0x0F) << 8) |
               (static_cast<uint32_t>(nibbles[4] & 0x0F) << 4) |
               (static_cast<uint32_t>(nibbles[5] & 0x0F));
    }
};

enum class Nixie6View : uint8_t {
    DefaultTime = 0,
    Date,
    Alarm1,
    Alarm2,
    Pressure,
    Humidity,
    Temperature,
    EditPlaceholder
};

class Nixie6SpiDriver : public DisplayDriver {
public:
    Nixie6SpiDriver(uint8_t latchPin, uint8_t sckPin, uint8_t mosiPin);

    // DisplayDriver
    void begin() override;
    void setBrightness(uint8_t level) override;
    void showTime(uint8_t hours, uint8_t minutes, uint8_t seconds, bool showColon) override;
    void testPattern() override;
    void showUniformDigits(uint8_t digit);

    // Навигация
    void trigger1(bool allowAlarmViews = true);  // Основная ветка: time -> date -> alarm1 -> alarm2 -> time
    void trigger2();  // Вторая ветка: pressure -> humidity -> temperature -> time
    void tick(uint32_t nowMs);

    // Заглушка для будущего режима редактирования
    void enterEditPlaceholder();
    void exitEditPlaceholder();

    // Данные для отображения
    void setStartFlags(uint8_t flags);
    void setAlarm1(uint8_t hour, uint8_t minute);
    void setAlarm2(uint8_t hour, uint8_t minute);
    void setPressureMmHg(uint16_t pressure);
    void setHumidityPercent(uint8_t humidity);
    void setTemperatureC(int16_t temperature);
    void updateFromLocalTime(const tm& localTm);

    // Конфигурация
    void setMainModeTimeoutMs(uint32_t timeoutMs);

    // Диагностика
    Nixie6View currentView() const;
    Nixie6Frame currentFrame() const;
    uint32_t packedFrame() const;
    uint32_t packedFrameOutput() const;

    // Вывод кадра в 74HC595
    void pushFrame();

    // Неблокирующая анимация перехода времени (старый кадр -> новый кадр)
    void serviceTransition(uint32_t nowMs);
    void cancelTransition();
    bool isTransitionActive() const;

private:
    enum class MainMode : uint8_t { Time = 0, Date, Alarm1, Alarm2 };
    enum class AuxMode : uint8_t { Pressure = 0, Humidity, Temperature };

    uint8_t latchPin_;
    uint8_t sckPin_;
    uint8_t mosiPin_;
    uint8_t brightness_ = 128;

    bool editPlaceholder_ = false;
    bool secondaryBranchActive_ = false;

    MainMode mainMode_ = MainMode::Time;
    AuxMode auxMode_ = AuxMode::Pressure;

    uint32_t mainModeTimeoutMs_ = 5000;
    uint32_t modeEnteredAtMs_ = 0;

    uint8_t startFlags_ = 0;

    // Источники данных
    tm localTm_{};
    uint8_t al1Hour_ = 0;
    uint8_t al1Minute_ = 0;
    uint8_t al2Hour_ = 0;
    uint8_t al2Minute_ = 0;
    uint16_t pressureMmHg_ = 760;
    uint8_t humidityPercent_ = 50;
    int16_t temperatureC_ = 23;

    static uint8_t tens(uint8_t value);
    static uint8_t ones(uint8_t value);

    Nixie6Frame buildTimeFrame() const;
    Nixie6Frame buildUniformFrame(uint8_t digit) const;
    Nixie6Frame buildDateFrame() const;
    Nixie6Frame buildAlarmFrame(uint8_t hour, uint8_t minute) const;
    Nixie6Frame buildPressureFrame() const;
    Nixie6Frame buildHumidityFrame() const;
    Nixie6Frame buildTemperatureFrame() const;
    Nixie6Frame buildTimeFrameFromTm(const tm& sourceTm) const;
    Nixie6Frame applyOutputMode(const Nixie6Frame& frame) const;
    uint8_t statusFlagsWithSeparators(bool showHmSeparator = true,
                                      bool showMsSeparator = true) const;

    void maybeStartTimeTransition(const tm& previousTm, const tm& newTm, uint32_t nowMs);
    Nixie6Frame frameForOutputNow(uint32_t nowMs) const;

    void writeBit(bool value);
    void shiftOut32(uint32_t value);

    bool softTransitionEnabled_ = true;
    bool transitionActive_ = false;
    uint32_t transitionStartMs_ = 0;
    uint16_t transitionDurationMs_ = 150;
    Nixie6Frame transitionFrom_{};
    Nixie6Frame transitionTo_{};
};

// Глобальные runtime-настройки soft-transition для Nixie6.
void nixie6SetSoftTransitionEnabled(bool enabled);
bool nixie6IsSoftTransitionEnabled();
void nixie6SetTransitionDurationMs(uint16_t durationMs);
uint16_t nixie6GetTransitionDurationMs();
