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
