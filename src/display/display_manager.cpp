#include "display/display_manager.h"
#include "display/nixie_6_spi.h"
#include "hardware.h"
#include "platform_profile.h"

namespace {
class VirtualDisplayDriver : public DisplayDriver {
public:
    explicit VirtualDisplayDriver(uint8_t statusBase = 0x10) : statusBase_(statusBase) {}

    void begin() override {}
    void setBrightness(uint8_t level) override { (void)level; }

    void showTime(uint8_t hours, uint8_t minutes, uint8_t seconds, bool showColon) override {
        (void)showColon;
        h_ = hours;
        m_ = minutes;
        s_ = seconds;
    }

    void testPattern() override {
        h_ = 88;
        m_ = 88;
        s_ = 88;
    }

    uint8_t status() const { return statusBase_; }
    uint8_t h() const { return h_; }
    uint8_t m() const { return m_; }
    uint8_t s() const { return s_; }

private:
    uint8_t statusBase_ = 0x10;
    uint8_t h_ = 0;
    uint8_t m_ = 0;
    uint8_t s_ = 0;
};

Nixie6SpiDriver g_nixie6(HSPI_CS_PIN, HSPI_SCK_PIN, HSPI_MOSI_PIN);
VirtualDisplayDriver g_nixieGeneric(0x11);
VirtualDisplayDriver g_nixieHand(0x12);
VirtualDisplayDriver g_cyclotron(0x13);
VirtualDisplayDriver g_vertical(0x14);
VirtualDisplayDriver g_mech2(0x15);
VirtualDisplayDriver g_mechPend(0x16);
}

bool DisplayManager::isHolidayDay(const tm& localTm) {
    return (localTm.tm_wday == 0 || localTm.tm_wday == 6);
}

bool DisplayManager::isHourInHalfOpenRange(uint8_t hour, uint8_t startHour, uint8_t endHour) {
    if (hour > 23 || startHour > 24 || endHour > 24) {
        return true;
    }

    if (startHour == 0 && endHour == 24) {
        return true;
    }

    if (startHour == endHour) {
        return false;
    }

    if (startHour < endHour) {
        return (hour >= startHour) && (hour < endHour);
    }

    return (hour >= startHour) || (hour < endHour);
}

void DisplayManager::formatDisplayActivityIntervalsInternal(uint8_t start1,
                                                            uint8_t end1,
                                                            uint8_t start2,
                                                            uint8_t end2,
                                                            char* out,
                                                            size_t outSize) {
    if (!out || outSize == 0) {
        return;
    }

    struct Interval {
        uint8_t start;
        uint8_t end;
    };

    Interval intervals[2] = {
        {start1, end1},
        {start2, end2}
    };

    Interval normalized[2];
    uint8_t count = 0;
    for (uint8_t i = 0; i < 2; ++i) {
        const uint8_t s = intervals[i].start;
        const uint8_t e = intervals[i].end;
        if (s > 24 || e > 24 || s == e) {
            continue;
        }
        normalized[count++] = {s, e};
    }

    if (count == 0) {
        strlcpy(out, "нет активных интервалов", outSize);
        return;
    }

    if (count == 2 && normalized[1].start < normalized[0].start) {
        const Interval tmp = normalized[0];
        normalized[0] = normalized[1];
        normalized[1] = tmp;
    }

    Interval merged[2];
    uint8_t mergedCount = 0;
    for (uint8_t i = 0; i < count; ++i) {
        if (mergedCount == 0) {
            merged[mergedCount++] = normalized[i];
            continue;
        }

        Interval& last = merged[mergedCount - 1];
        if (normalized[i].start <= last.end) {
            if (normalized[i].end > last.end) {
                last.end = normalized[i].end;
            }
        } else {
            merged[mergedCount++] = normalized[i];
        }
    }

    if (mergedCount == 1) {
        snprintf(out, outSize, "%u-%u",
                 static_cast<unsigned>(merged[0].start),
                 static_cast<unsigned>(merged[0].end));
        return;
    }

    snprintf(out, outSize, "%u-%u, %u-%u",
             static_cast<unsigned>(merged[0].start),
             static_cast<unsigned>(merged[0].end),
             static_cast<unsigned>(merged[1].start),
             static_cast<unsigned>(merged[1].end));
}

bool DisplayManager::isDisplayActiveBySchedule(const tm& localTm) const {
    const bool isHoliday = isHolidayDay(localTm);
    const uint8_t hour = static_cast<uint8_t>((localTm.tm_hour < 0) ? 0 : (localTm.tm_hour % 24));

    const uint8_t start1 = isHoliday ? config.display_holiday_active_start_hour : config.display_active_start_hour;
    const uint8_t end1 = isHoliday ? config.display_holiday_active_end_hour : config.display_active_end_hour;
    const uint8_t start2 = isHoliday ? config.display_holiday_active_start_hour_2 : config.display_active_start_hour_2;
    const uint8_t end2 = isHoliday ? config.display_holiday_active_end_hour_2 : config.display_active_end_hour_2;

    const bool inTime1 = isHourInHalfOpenRange(hour, start1, end1);
    const bool inTime2 = isHourInHalfOpenRange(hour, start2, end2);
    return (inTime1 || inTime2);
}

void DisplayManager::triggerSleepOverrideIfNeeded(const tm& localTm) {
    if (sleepOverrideUntilSchedule_) {
        return;
    }

    if (!isDisplayActiveBySchedule(localTm)) {
        sleepOverrideUntilSchedule_ = true;
    }
}

bool DisplayManager::isDisplayActiveNow(const tm& localTm) {
    const bool scheduleActive = isDisplayActiveBySchedule(localTm);
    if (sleepOverrideUntilSchedule_ && scheduleActive) {
        sleepOverrideUntilSchedule_ = false;
    }
    return scheduleActive || sleepOverrideUntilSchedule_;
}

bool DisplayManager::isSleepOverrideActive() const {
    return sleepOverrideUntilSchedule_;
}

void DisplayManager::formatDisplayActivityIntervalsForWorkdays(char* out, size_t outSize) const {
    formatDisplayActivityIntervalsInternal(config.display_active_start_hour,
                                           config.display_active_end_hour,
                                           config.display_active_start_hour_2,
                                           config.display_active_end_hour_2,
                                           out,
                                           outSize);
}

void DisplayManager::formatDisplayActivityIntervalsForHolidays(char* out, size_t outSize) const {
    formatDisplayActivityIntervalsInternal(config.display_holiday_active_start_hour,
                                           config.display_holiday_active_end_hour,
                                           config.display_holiday_active_start_hour_2,
                                           config.display_holiday_active_end_hour_2,
                                           out,
                                           outSize);
}

void DisplayManager::formatDisplayActivityIntervalsForCurrentDay(const tm& localTm, char* out, size_t outSize) const {
    if (isHolidayDay(localTm)) {
        formatDisplayActivityIntervalsForHolidays(out, outSize);
    } else {
        formatDisplayActivityIntervalsForWorkdays(out, outSize);
    }
}

void DisplayManager::attach(DisplayDriver* driver) {
    driver_ = driver;
}

void DisplayManager::begin() {
    // Роутинг backend по выбранному типу часов
    isNixie6_ = false;
    activeBackend_ = ActiveBackend::None;
    debugAvailable_ = false;

    if (config.clock_type == CLOCK_TYPE_NIXIE && config.clock_digits == 6) {
        attach(&g_nixie6);
        isNixie6_ = true;
        activeBackend_ = ActiveBackend::Nixie6;
        tickMode_ = DisplayTickMode::EverySecond;
        Serial.printf("\n[DISP] backend=Nixie6 74HC595 DATA=%u CLK=%u LATCH=%u OE=%u",
                      static_cast<unsigned>(HSPI_MOSI_PIN),
                      static_cast<unsigned>(HSPI_SCK_PIN),
                      static_cast<unsigned>(HSPI_CS_PIN),
                      static_cast<unsigned>(SR595_OE_PIN));
    } else {
        switch (config.clock_type) {
            case CLOCK_TYPE_NIXIE:
                attach(&g_nixieGeneric);
                activeBackend_ = ActiveBackend::NixieGeneric;
                tickMode_ = DisplayTickMode::EveryMinute;
                break;
            case CLOCK_TYPE_NIXIE_HAND:
                attach(&g_nixieHand);
                activeBackend_ = ActiveBackend::NixieHand;
                tickMode_ = DisplayTickMode::EventOnly;
                break;
            case CLOCK_TYPE_CYCLOTRON:
                attach(&g_cyclotron);
                activeBackend_ = ActiveBackend::Cyclotron;
                tickMode_ = DisplayTickMode::EveryMinute;
                break;
            case CLOCK_TYPE_VERTICAL:
                attach(&g_vertical);
                activeBackend_ = ActiveBackend::Vertical;
                tickMode_ = DisplayTickMode::EveryMinute;
                break;
            case CLOCK_TYPE_MECH_2:
                attach(&g_mech2);
                activeBackend_ = ActiveBackend::Mech2;
                tickMode_ = DisplayTickMode::EveryMinute;
                break;
            case CLOCK_TYPE_MECH_PEND:
                attach(&g_mechPend);
                activeBackend_ = ActiveBackend::MechPend;
                tickMode_ = DisplayTickMode::EveryMinute;
                break;
            default:
                attach(&g_nixieGeneric);
                activeBackend_ = ActiveBackend::NixieGeneric;
                tickMode_ = DisplayTickMode::EveryMinute;
                break;
        }

            Serial.printf("\n[DISP] backend=virtual type=%u digits=%u (74HC595 физически не обновляется)",
                      static_cast<unsigned>(config.clock_type),
                      static_cast<unsigned>(config.clock_digits));
    }

    if (driver_) {
        driver_->begin();
        debugAvailable_ = true;
    }
}

void DisplayManager::setBrightness(uint8_t level) {
    if (driver_) {
        driver_->setBrightness(level);
    }
}

void DisplayManager::showTime(uint8_t hours, uint8_t minutes, uint8_t seconds, bool showColon) {
    if (driver_) {
        driver_->showTime(hours, minutes, seconds, showColon);
    }
}

void DisplayManager::showUniformDigits(uint8_t digit) {
    if (!driver_) {
        return;
    }

    if (digit > 9) digit = 9;

    if (isNixie6_) {
        auto* d = static_cast<Nixie6SpiDriver*>(driver_);
        d->showUniformDigits(digit);
        return;
    }

    const uint8_t dd = static_cast<uint8_t>(digit * 11);
    driver_->showTime(dd, dd, dd, true);
}

void DisplayManager::testPattern() {
    if (driver_) {
        driver_->testPattern();
    }
}

void DisplayManager::updateFromLocalTime(const tm& localTm, uint32_t nowMs,
                                         uint8_t alarm1Hour, uint8_t alarm1Minute,
                                         uint8_t alarm2Hour, uint8_t alarm2Minute) {
    if (!driver_) {
        return;
    }

    if (isNixie6_) {
        auto* d = static_cast<Nixie6SpiDriver*>(driver_);
        d->updateFromLocalTime(localTm);
        d->setAlarm1(alarm1Hour, alarm1Minute);
        d->setAlarm2(alarm2Hour, alarm2Minute);
        d->tick(nowMs);
        d->pushFrame();
        return;
    }

    driver_->showTime(static_cast<uint8_t>(localTm.tm_hour),
                      static_cast<uint8_t>(localTm.tm_min),
                      static_cast<uint8_t>(localTm.tm_sec),
                      true);

    // Состояние диагностики для виртуальных backend'ов
    auto* vd = static_cast<VirtualDisplayDriver*>(driver_);
    debugStatus_ = vd->status();
    debugHh_ = vd->h();
    debugMm_ = vd->m();
    debugSs_ = vd->s();
    (void)nowMs;
    (void)alarm1Hour;
    (void)alarm1Minute;
    (void)alarm2Hour;
    (void)alarm2Minute;
}

void DisplayManager::serviceAnimations(uint32_t nowMs) {
    if (!driver_ || !isNixie6_) {
        return;
    }

    auto* d = static_cast<Nixie6SpiDriver*>(driver_);
    d->serviceTransition(nowMs);
}

void DisplayManager::stopAnimations() {
    if (!driver_ || !isNixie6_) {
        return;
    }

    auto* d = static_cast<Nixie6SpiDriver*>(driver_);
    d->cancelTransition();
}

bool DisplayManager::hasActiveAnimation() const {
    if (!driver_ || !isNixie6_) {
        return false;
    }

    const auto* d = static_cast<const Nixie6SpiDriver*>(driver_);
    return d->isTransitionActive();
}

bool DisplayManager::isNixieClockType() const {
    return (activeBackend_ == ActiveBackend::Nixie6 ||
            activeBackend_ == ActiveBackend::NixieGeneric ||
            activeBackend_ == ActiveBackend::NixieHand);
}

bool DisplayManager::supportsSoftTransition() const {
    // Soft-transition реализован только в Nixie6 backend.
    return isNixie6_;
}

bool DisplayManager::supportsAntiPoison() const {
    // Антиотравление имеет смысл только для Nixie-индикаторов.
    return isNixieClockType();
}

void DisplayManager::showOtaTransferStartMarker() {
    if (!isNixie6_ || !driver_) {
        return;
    }

    // Во время старта OTA принудительно показываем маркер на дисплее.
    showTime(12, 34, 56, true);

    if (config.nix6_output_mode == NIX6_OUTPUT_REVERSE_INVERT) {
        Serial.print("\n[DISP][OTA] 65:43:21 0x00");
    } else {
        Serial.print("\n[DISP][OTA] 12:34:56 0x00");
    }
}

bool DisplayManager::shouldUpdateOnSecond(uint8_t second) const {
    switch (tickMode_) {
        case DisplayTickMode::EverySecond:
            return true;
        case DisplayTickMode::EveryMinute:
            return second == 0;
        case DisplayTickMode::EventOnly:
        default:
            return false;
    }
}

DisplayTickMode DisplayManager::tickMode() const {
    return tickMode_;
}

DisplayDebugInfo DisplayManager::getDebugInfo() const {
    DisplayDebugInfo out;
    if (!driver_ || !isNixie6_) {
        out.available = debugAvailable_;
        out.reverseFormat = false;
        out.status = debugStatus_;
        out.hh = debugHh_;
        out.mm = debugMm_;
        out.ss = debugSs_;
        return out;
    }

    const auto* d = static_cast<const Nixie6SpiDriver*>(driver_);
    const uint32_t frame = d->packedFrame();
    const uint32_t frameOutput = d->packedFrameOutput();

    const bool reverseMode = (config.clock_type == CLOCK_TYPE_NIXIE &&
                              config.clock_digits == 6 &&
                              config.nix6_output_mode == NIX6_OUTPUT_REVERSE_INVERT);

    const uint32_t selected = reverseMode ? frameOutput : frame;

    out.available = true;
    out.reverseFormat = reverseMode;
    out.status = static_cast<uint8_t>((selected >> 24) & 0xFF);
    out.hh = static_cast<uint8_t>((selected >> 16) & 0xFF);
    out.mm = static_cast<uint8_t>((selected >> 8) & 0xFF);
    out.ss = static_cast<uint8_t>(selected & 0xFF);
    return out;
}

bool DisplayManager::hasActiveDriver() const {
    return driver_ != nullptr;
}

bool DisplayManager::handleAction(DisplayAction action) {
    if (action == DisplayAction::None || !driver_) {
        return false;
    }

    if (isNixie6_) {
        auto* d = static_cast<Nixie6SpiDriver*>(driver_);
        const bool allowAlarmViews = platformGetCapabilities().alarm_enabled;
        switch (action) {
            case DisplayAction::NextMainView:
                d->trigger1(allowAlarmViews);
                return true;
            case DisplayAction::NextAuxView:
                d->trigger2();
                return true;
            case DisplayAction::EnterEditMode:
                d->enterEditPlaceholder();
                d->pushFrame();
                return true;
            case DisplayAction::ExitEditMode:
                d->exitEditPlaceholder();
                d->pushFrame();
                return true;
            case DisplayAction::TestPattern:
                d->testPattern();
                return true;
            case DisplayAction::None:
            default:
                return false;
        }
    }

    // Для текущих заглушек поддерживаем только тест-шаблон
    if (action == DisplayAction::TestPattern) {
        driver_->testPattern();

        auto* vd = static_cast<VirtualDisplayDriver*>(driver_);
        debugStatus_ = vd->status();
        debugHh_ = vd->h();
        debugMm_ = vd->m();
        debugSs_ = vd->s();
        debugAvailable_ = true;
        return true;
    }

    return false;
}

const char* DisplayManager::activeBackendName() const {
    switch (activeBackend_) {
        case ActiveBackend::Nixie6: return "Nixie6";
        case ActiveBackend::NixieGeneric: return "NixieGeneric";
        case ActiveBackend::NixieHand: return "NixieHand";
        case ActiveBackend::Cyclotron: return "Cyclotron";
        case ActiveBackend::Vertical: return "Vertical";
        case ActiveBackend::Mech2: return "Mech2";
        case ActiveBackend::MechPend: return "MechPend";
        case ActiveBackend::None:
        default:
            return "None";
    }
}

const char* DisplayManager::activeViewName() const {
    if (!driver_) {
        return "none";
    }

    if (isNixie6_) {
        const auto* d = static_cast<const Nixie6SpiDriver*>(driver_);
        switch (d->currentView()) {
            case Nixie6View::DefaultTime: return "time";
            case Nixie6View::Date: return "date";
            case Nixie6View::Alarm1: return "al1";
            case Nixie6View::Alarm2: return "al2";
            case Nixie6View::Pressure: return "pressure";
            case Nixie6View::Humidity: return "humidity";
            case Nixie6View::Temperature: return "temperature";
            case Nixie6View::EditPlaceholder: return "edit";
            default: return "unknown";
        }
    }

    return "time";
}
