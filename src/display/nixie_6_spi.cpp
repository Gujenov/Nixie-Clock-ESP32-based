#include "display/nixie_6_spi.h"
#include "config.h"
#include <SPI.h>

namespace {
constexpr uint16_t DATA_SETUP_US = 1;
constexpr uint16_t SCK_HIGH_US = 1;
constexpr uint16_t LATCH_PULSE_US = 1;
constexpr uint16_t TRANSITION_SLOT_US = 500;  // 2 кГц мигания при смешивании кадров
constexpr uint8_t TRANSITION_SLOTS = 10;

// Назначение битов служебного байта (MSB -> LSB):
// b7: Reserve
// b6: AM/PM indicator (12-hour mode)
// b5: Load 1 enable
// b4: Load 2 enable
// b3: Alarm 1 indicator
// b2: Alarm 2 indicator
// b1: Separator between HH and MM
// b0: Separator between MM and SS
constexpr uint8_t STATUS_BIT_RESERVED = 0x80;
constexpr uint8_t STATUS_BIT_AM_PM = 0x40;
constexpr uint8_t STATUS_BIT_LOAD1 = 0x20;
constexpr uint8_t STATUS_BIT_LOAD2 = 0x10;
constexpr uint8_t STATUS_BIT_ALARM1 = 0x08;
constexpr uint8_t STATUS_BIT_ALARM2 = 0x04;
constexpr uint8_t STATUS_BIT_SEP_HM = 0x02;
constexpr uint8_t STATUS_BIT_SEP_MS = 0x01;
constexpr uint8_t STATUS_ALL_DEFINED_BITS =
    static_cast<uint8_t>(STATUS_BIT_RESERVED |
                         STATUS_BIT_AM_PM |
                         STATUS_BIT_LOAD1 |
                         STATUS_BIT_LOAD2 |
                         STATUS_BIT_ALARM1 |
                         STATUS_BIT_ALARM2 |
                         STATUS_BIT_SEP_HM |
                         STATUS_BIT_SEP_MS);

constexpr bool USE_HARDWARE_SPI = true;
constexpr uint32_t DISPLAY_SPI_HZ = 15000000;  // 15 МГц

// Временный диагностический флаг:
// true  -> в режиме Alarm показывать реальные секунды вместо "пустых" нибблов 0xF/0xF.
// false -> штатный режим (хвостовые нибблы alarm-экрана = 0xF/0xF).
constexpr bool DEBUG_KEEP_SECONDS_IN_ALARM_VIEW = false;

bool g_softTransitionEnabled = true;
uint16_t g_transitionDurationMs = 150;

SPIClass g_displaySpi(HSPI);
bool g_displaySpiReady = false;

inline uint8_t reverseNibbleBits(uint8_t nibble) {
    nibble &= 0x0F;
    return static_cast<uint8_t>(((nibble & 0x1U) << 3) |
                                ((nibble & 0x2U) << 1) |
                                ((nibble & 0x4U) >> 1) |
                                ((nibble & 0x8U) >> 3));
}
}

Nixie6SpiDriver::Nixie6SpiDriver(uint8_t latchPin, uint8_t sckPin, uint8_t mosiPin)
    : latchPin_(latchPin), sckPin_(sckPin), mosiPin_(mosiPin) {}

void nixie6SetSoftTransitionEnabled(bool enabled) {
    g_softTransitionEnabled = enabled;
}

bool nixie6IsSoftTransitionEnabled() {
    return g_softTransitionEnabled;
}

void nixie6SetTransitionDurationMs(uint16_t durationMs) {
    if (durationMs < 40) durationMs = 40;
    if (durationMs > 500) durationMs = 500;
    g_transitionDurationMs = durationMs;
}

uint16_t nixie6GetTransitionDurationMs() {
    return g_transitionDurationMs;
}

void Nixie6SpiDriver::begin() {
    pinMode(latchPin_, OUTPUT);
    pinMode(sckPin_, OUTPUT);
    pinMode(mosiPin_, OUTPUT);

    digitalWrite(latchPin_, LOW);
    digitalWrite(sckPin_, LOW);
    digitalWrite(mosiPin_, LOW);

    if (USE_HARDWARE_SPI) {
        g_displaySpi.begin(sckPin_, -1, mosiPin_, latchPin_);
        g_displaySpiReady = true;
    }

    pushFrame();
}

void Nixie6SpiDriver::setBrightness(uint8_t level) {
    brightness_ = level;
    (void)brightness_;
}

void Nixie6SpiDriver::showTime(uint8_t hours, uint8_t minutes, uint8_t seconds, bool showColon) {
    (void)showColon;

    localTm_.tm_hour = hours;
    localTm_.tm_min = minutes;
    localTm_.tm_sec = seconds;

    if (!secondaryBranchActive_) {
        mainMode_ = MainMode::Time;
    }

    pushFrame();
}

void Nixie6SpiDriver::testPattern() {
    Nixie6Frame f;
    f.startFlags = statusFlagsWithSeparators();
    f.nibbles[0] = 8;
    f.nibbles[1] = 7;
    f.nibbles[2] = 6;
    f.nibbles[3] = 5;
    f.nibbles[4] = 4;
    f.nibbles[5] = 3;
    shiftOut32(f.pack());
}

void Nixie6SpiDriver::showUniformDigits(uint8_t digit) {
    if (digit > 9) digit = 9;
    transitionActive_ = false;  // антиотравление/ручные шаблоны — без soft-transition
    const Nixie6Frame f = applyOutputMode(buildUniformFrame(digit));
    shiftOut32(f.pack());
}

void Nixie6SpiDriver::trigger1(bool allowAlarmViews) {
    if (editPlaceholder_) {
        return;
    }

    if (secondaryBranchActive_) {
        secondaryBranchActive_ = false;
        mainMode_ = MainMode::Time;
        pushFrame();
        return;
    }

    switch (mainMode_) {
        case MainMode::Time:
            mainMode_ = MainMode::Date;
            break;
        case MainMode::Date:
            mainMode_ = allowAlarmViews ? MainMode::Alarm1 : MainMode::Time;
            break;
        case MainMode::Alarm1:
            mainMode_ = allowAlarmViews ? MainMode::Alarm2 : MainMode::Time;
            break;
        case MainMode::Alarm2:
        default:
            mainMode_ = MainMode::Time;
            break;
    }

    modeEnteredAtMs_ = millis();
    pushFrame();
}

void Nixie6SpiDriver::trigger2() {
    if (editPlaceholder_) {
        return;
    }

    if (!secondaryBranchActive_) {
        secondaryBranchActive_ = true;
        auxMode_ = AuxMode::Pressure;
        pushFrame();
        return;
    }

    switch (auxMode_) {
        case AuxMode::Pressure:
            auxMode_ = AuxMode::Humidity;
            break;
        case AuxMode::Humidity:
            auxMode_ = AuxMode::Temperature;
            break;
        case AuxMode::Temperature:
        default:
            secondaryBranchActive_ = false;
            mainMode_ = MainMode::Time;
            break;
    }

    pushFrame();
}

void Nixie6SpiDriver::tick(uint32_t nowMs) {
    if (editPlaceholder_ || secondaryBranchActive_) {
        return;
    }

    if (mainMode_ != MainMode::Time && (nowMs - modeEnteredAtMs_ >= mainModeTimeoutMs_)) {
        mainMode_ = MainMode::Time;
        pushFrame();
    }
}

void Nixie6SpiDriver::enterEditPlaceholder() {
    editPlaceholder_ = true;
}

void Nixie6SpiDriver::exitEditPlaceholder() {
    editPlaceholder_ = false;
}

void Nixie6SpiDriver::setStartFlags(uint8_t flags) {
    startFlags_ = flags;
}

void Nixie6SpiDriver::setAlarm1(uint8_t hour, uint8_t minute) {
    al1Hour_ = hour % 24;
    al1Minute_ = minute % 60;
}

void Nixie6SpiDriver::setAlarm2(uint8_t hour, uint8_t minute) {
    al2Hour_ = hour % 24;
    al2Minute_ = minute % 60;
}

void Nixie6SpiDriver::setPressureMmHg(uint16_t pressure) {
    pressureMmHg_ = pressure;
}

void Nixie6SpiDriver::setHumidityPercent(uint8_t humidity) {
    humidityPercent_ = (humidity > 99) ? 99 : humidity;
}

void Nixie6SpiDriver::setTemperatureC(int16_t temperature) {
    temperatureC_ = temperature;
}

void Nixie6SpiDriver::updateFromLocalTime(const tm& localTm) {
    const tm previousTm = localTm_;
    maybeStartTimeTransition(previousTm, localTm, millis());
    localTm_ = localTm;
}

void Nixie6SpiDriver::setMainModeTimeoutMs(uint32_t timeoutMs) {
    mainModeTimeoutMs_ = timeoutMs;
}

Nixie6View Nixie6SpiDriver::currentView() const {
    if (editPlaceholder_) {
        return Nixie6View::EditPlaceholder;
    }

    if (secondaryBranchActive_) {
        switch (auxMode_) {
            case AuxMode::Pressure: return Nixie6View::Pressure;
            case AuxMode::Humidity: return Nixie6View::Humidity;
            case AuxMode::Temperature: return Nixie6View::Temperature;
            default: return Nixie6View::Pressure;
        }
    }

    switch (mainMode_) {
        case MainMode::Time: return Nixie6View::DefaultTime;
        case MainMode::Date: return Nixie6View::Date;
        case MainMode::Alarm1: return Nixie6View::Alarm1;
        case MainMode::Alarm2: return Nixie6View::Alarm2;
        default: return Nixie6View::DefaultTime;
    }
}

Nixie6Frame Nixie6SpiDriver::currentFrame() const {
    switch (currentView()) {
        case Nixie6View::DefaultTime: return buildTimeFrame();
        case Nixie6View::Date: return buildDateFrame();
        case Nixie6View::Alarm1: return buildAlarmFrame(al1Hour_, al1Minute_);
        case Nixie6View::Alarm2: return buildAlarmFrame(al2Hour_, al2Minute_);
        case Nixie6View::Pressure: return buildPressureFrame();
        case Nixie6View::Humidity: return buildHumidityFrame();
        case Nixie6View::Temperature: return buildTemperatureFrame();
        case Nixie6View::EditPlaceholder:
        default:
            return secondaryBranchActive_ ? buildPressureFrame() : buildTimeFrame();
    }
}

uint32_t Nixie6SpiDriver::packedFrame() const {
    // Логический кадр для диагностики/отладки (без аппаратных трансформаций)
    return currentFrame().pack();
}

uint32_t Nixie6SpiDriver::packedFrameOutput() const {
    // Кадр после преобразования режима вывода (порядок разрядов)
    return applyOutputMode(currentFrame()).pack();
}

void Nixie6SpiDriver::pushFrame() {
    // На шину уходит кадр c учетом активной анимации soft-transition (если есть).
    if (transitionActive_ && (millis() - transitionStartMs_) >= transitionDurationMs_) {
        transitionActive_ = false;
    }
    const Nixie6Frame out = frameForOutputNow(millis());
    shiftOut32(out.pack());
}

void Nixie6SpiDriver::serviceTransition(uint32_t nowMs) {
    if (!transitionActive_) {
        return;
    }
    if ((nowMs - transitionStartMs_) >= transitionDurationMs_) {
        transitionActive_ = false;
        shiftOut32(transitionTo_.pack());
        return;
    }
    const Nixie6Frame out = frameForOutputNow(nowMs);
    shiftOut32(out.pack());
}

void Nixie6SpiDriver::cancelTransition() {
    transitionActive_ = false;
}

bool Nixie6SpiDriver::isTransitionActive() const {
    return transitionActive_;
}

uint8_t Nixie6SpiDriver::tens(uint8_t value) {
    return (value / 10) % 10;
}

uint8_t Nixie6SpiDriver::ones(uint8_t value) {
    return value % 10;
}

Nixie6Frame Nixie6SpiDriver::buildTimeFrame() const {
    return buildTimeFrameFromTm(localTm_);
}

Nixie6Frame Nixie6SpiDriver::buildTimeFrameFromTm(const tm& sourceTm) const {
    Nixie6Frame f;
    // Для режима времени по умолчанию оба разделителя включены.
    f.startFlags = statusFlagsWithSeparators(true, true);

    const uint8_t hour = static_cast<uint8_t>((sourceTm.tm_hour < 0) ? 0 : sourceTm.tm_hour % 24);
    const uint8_t minute = static_cast<uint8_t>((sourceTm.tm_min < 0) ? 0 : sourceTm.tm_min % 60);
    const uint8_t second = static_cast<uint8_t>((sourceTm.tm_sec < 0) ? 0 : sourceTm.tm_sec % 60);

    // Стандартный порядок (режим 1):
    // ЧАС-МИН-СЕК + служебный байт (0x03)
    f.nibbles[0] = tens(hour);
    f.nibbles[1] = ones(hour);
    f.nibbles[2] = tens(minute);
    f.nibbles[3] = ones(minute);
    f.nibbles[4] = tens(second);
    f.nibbles[5] = ones(second);
    return f;
}

Nixie6Frame Nixie6SpiDriver::buildUniformFrame(uint8_t digit) const {
    Nixie6Frame f;
    f.startFlags = statusFlagsWithSeparators();
    const uint8_t d = digit & 0x0F;  // 0..9, 0x0A=blank
    for (uint8_t i = 0; i < 6; ++i) {
        f.nibbles[i] = d;
    }
    return f;
}

Nixie6Frame Nixie6SpiDriver::buildDateFrame() const {
    Nixie6Frame f;
    f.startFlags = statusFlagsWithSeparators();

    const uint8_t day = static_cast<uint8_t>((localTm_.tm_mday < 1) ? 1 : (localTm_.tm_mday > 31 ? 31 : localTm_.tm_mday));
    const uint8_t month = static_cast<uint8_t>((localTm_.tm_mon < 0) ? 1 : ((localTm_.tm_mon % 12) + 1));
    const uint8_t year2 = static_cast<uint8_t>((localTm_.tm_year + 1900) % 100);

    f.nibbles[0] = tens(day);
    f.nibbles[1] = ones(day);
    f.nibbles[2] = tens(month);
    f.nibbles[3] = ones(month);
    f.nibbles[4] = tens(year2);
    f.nibbles[5] = ones(year2);
    return f;
}

Nixie6Frame Nixie6SpiDriver::buildAlarmFrame(uint8_t hour, uint8_t minute) const {
    Nixie6Frame f;
    // Для режима будильника гасим разделитель MM:SS (b0=0), оставляем HH:MM (b1=1).
    f.startFlags = statusFlagsWithSeparators(true, false);

    f.nibbles[0] = tens(hour);
    f.nibbles[1] = ones(hour);
    f.nibbles[2] = tens(minute);
    f.nibbles[3] = ones(minute);

    if (DEBUG_KEEP_SECONDS_IN_ALARM_VIEW) {
        // Диагностика: не гасим секунды, а выводим текущие.
        const uint8_t second = static_cast<uint8_t>((localTm_.tm_sec < 0) ? 0 : localTm_.tm_sec % 60);
        f.nibbles[4] = tens(second);
        f.nibbles[5] = ones(second);
    } else {
        // Штатный режим: хвостовые два ниббла = 0xFF (гашение индикаторов).
        // В режиме reverse/invert это окажется в первых двух нибблах после applyOutputMode().
        f.nibbles[4] = 0x0F;
        f.nibbles[5] = 0x0F;
    }
    return f;
}

Nixie6Frame Nixie6SpiDriver::buildPressureFrame() const {
    Nixie6Frame f;
    f.startFlags = statusFlagsWithSeparators();

    uint16_t p = pressureMmHg_;
    if (p > 999) {
        p = 999;
    }

    f.nibbles[0] = (p / 100) % 10;
    f.nibbles[1] = (p / 10) % 10;
    f.nibbles[2] = p % 10;
    f.nibbles[3] = 0;
    f.nibbles[4] = 0;
    f.nibbles[5] = 0;
    return f;
}

Nixie6Frame Nixie6SpiDriver::buildHumidityFrame() const {
    Nixie6Frame f;
    f.startFlags = statusFlagsWithSeparators();

    const uint8_t h = (humidityPercent_ > 99) ? 99 : humidityPercent_;
    f.nibbles[0] = tens(h);
    f.nibbles[1] = ones(h);
    f.nibbles[2] = 0;
    f.nibbles[3] = 0;
    f.nibbles[4] = 0;
    f.nibbles[5] = 0;
    return f;
}

Nixie6Frame Nixie6SpiDriver::buildTemperatureFrame() const {
    Nixie6Frame f;
    f.startFlags = statusFlagsWithSeparators();

    int16_t t = temperatureC_;
    if (t < 0) {
        t = -t;
    }
    if (t > 999) {
        t = 999;
    }

    f.nibbles[0] = (t / 100) % 10;
    f.nibbles[1] = (t / 10) % 10;
    f.nibbles[2] = t % 10;
    f.nibbles[3] = 0;
    f.nibbles[4] = 0;
    f.nibbles[5] = 0;
    return f;
}

Nixie6Frame Nixie6SpiDriver::applyOutputMode(const Nixie6Frame& frame) const {
    Nixie6Frame out = frame;

    // Режим применяется только для Nixie 6, выбранного в инженерном меню.
    if (config.clock_type != CLOCK_TYPE_NIXIE || config.clock_digits != 6) {
        return out;
    }

    if (config.nix6_output_mode == NIX6_OUTPUT_REVERSE_INVERT) {
        // Обратный порядок разрядов данных.
        for (uint8_t i = 0; i < 6; ++i) {
            const uint8_t src = frame.nibbles[5 - i] & 0x0F;
            out.nibbles[i] = src;
        }
    }

    // NIX6_OUTPUT_STD: без преобразований (стандартный порядок)
    return out;
}

void Nixie6SpiDriver::maybeStartTimeTransition(const tm& previousTm, const tm& newTm, uint32_t nowMs) {
    softTransitionEnabled_ = g_softTransitionEnabled;
    transitionDurationMs_ = g_transitionDurationMs;

    if (!softTransitionEnabled_) {
        transitionActive_ = false;
        return;
    }

    // Эффект только для обычного режима времени, без веток/редактирования.
    if (editPlaceholder_ || secondaryBranchActive_ || mainMode_ != MainMode::Time) {
        transitionActive_ = false;
        return;
    }

    const Nixie6Frame prevOut = applyOutputMode(buildTimeFrameFromTm(previousTm));
    const Nixie6Frame nextOut = applyOutputMode(buildTimeFrameFromTm(newTm));

    if (prevOut.pack() == nextOut.pack()) {
        // В режиме частых апдейтов (например, во время sync) одно и то же время
        // может приходить многократно. Не сбрасываем активный переход,
        // если он уже ведёт к этому же целевому кадру.
        if (transitionActive_ && transitionTo_.pack() == nextOut.pack()) {
            return;
        }

        transitionActive_ = false;
        return;
    }

    transitionFrom_ = prevOut;
    transitionTo_ = nextOut;
    transitionStartMs_ = nowMs;
    transitionActive_ = true;
}

Nixie6Frame Nixie6SpiDriver::frameForOutputNow(uint32_t nowMs) const {
    if (!transitionActive_) {
        return applyOutputMode(currentFrame());
    }

    const uint32_t elapsed = nowMs - transitionStartMs_;
    if (elapsed >= transitionDurationMs_) {
        // const-функция: возврат конечного кадра; сброс флага выполняется в push/service ниже
        return transitionTo_;
    }

    // Линейный рост доли нового кадра: 1/10 -> 10/10 за transitionDurationMs.
    uint8_t newSlots = static_cast<uint8_t>(1 + (elapsed * (TRANSITION_SLOTS - 1)) / transitionDurationMs_);
    if (newSlots > TRANSITION_SLOTS) {
        newSlots = TRANSITION_SLOTS;
    }

    const uint32_t slot = (micros() / TRANSITION_SLOT_US) % TRANSITION_SLOTS;
    return (slot < newSlots) ? transitionTo_ : transitionFrom_;
}

void Nixie6SpiDriver::writeBit(bool value) {
    digitalWrite(mosiPin_, value ? HIGH : LOW);
    delayMicroseconds(DATA_SETUP_US);
    digitalWrite(sckPin_, HIGH);
    delayMicroseconds(SCK_HIGH_US);
    digitalWrite(sckPin_, LOW);
}

uint8_t Nixie6SpiDriver::statusFlagsWithSeparators(bool showHmSeparator,
                                                   bool showMsSeparator) const {
    uint8_t flags = static_cast<uint8_t>(startFlags_ & STATUS_ALL_DEFINED_BITS);

    if (showHmSeparator) {
        flags = static_cast<uint8_t>(flags | STATUS_BIT_SEP_HM);
    } else {
        flags = static_cast<uint8_t>(flags & ~STATUS_BIT_SEP_HM);
    }

    if (showMsSeparator) {
        flags = static_cast<uint8_t>(flags | STATUS_BIT_SEP_MS);
    } else {
        flags = static_cast<uint8_t>(flags & ~STATUS_BIT_SEP_MS);
    }

    return flags;
}

void Nixie6SpiDriver::shiftOut32(uint32_t value) {
    const bool reverseBitsInNibble =
        (config.clock_type == CLOCK_TYPE_NIXIE && config.clock_digits == 6 &&
         config.nix6_output_mode == NIX6_OUTPUT_REVERSE_INVERT);

    if (USE_HARDWARE_SPI && g_displaySpiReady) {
        // Порядок нибблов полностью повторяет текущий протокол bit-bang:
        // n0..n5 (данные), затем n6..n7 (служебный байт).
        // Сдвиги: 20,16,12,8,4,0,28,24
        const int8_t nibbleShifts[8] = {20, 16, 12, 8, 4, 0, 28, 24};
        uint8_t nibbles[8] = {0};
        for (uint8_t i = 0; i < 8; ++i) {
            uint8_t n = static_cast<uint8_t>((value >> nibbleShifts[i]) & 0x0FU);
            if (reverseBitsInNibble) {
                n = reverseNibbleBits(n);
            }
            nibbles[i] = n;
        }

        uint8_t tx[4] = {
            static_cast<uint8_t>((nibbles[0] << 4) | nibbles[1]),
            static_cast<uint8_t>((nibbles[2] << 4) | nibbles[3]),
            static_cast<uint8_t>((nibbles[4] << 4) | nibbles[5]),
            static_cast<uint8_t>((nibbles[6] << 4) | nibbles[7])
        };

        g_displaySpi.beginTransaction(SPISettings(DISPLAY_SPI_HZ, MSBFIRST, SPI_MODE0));
        digitalWrite(latchPin_, LOW);
        g_displaySpi.transfer(tx, sizeof(tx));
        digitalWrite(mosiPin_, LOW);
        digitalWrite(latchPin_, HIGH);
        delayMicroseconds(LATCH_PULSE_US);
        digitalWrite(latchPin_, LOW);
        g_displaySpi.endTransaction();
        return;
    }

    // Протокол:
    // 1) DATA выставляется до фронта CLOCK
    // 2) CLOCK импульс для каждого бита (всего 32 бита)
    // 3) LATCH импульс
    digitalWrite(latchPin_, LOW);
    digitalWrite(sckPin_, LOW);

    // Передаём сначала 24 бита данных (6 нибблов),
    // а служебный байт (startFlags, включая 0x03) — последним.
    // Режим 1: прямые биты (b3..b0), режим 2: обратные биты (b0..b3).
    auto writeNibbleByMode = [this, reverseBitsInNibble](uint8_t nibble) {
        if (reverseBitsInNibble) {
            for (uint8_t bit = 0; bit < 4; ++bit) {
                const bool b = ((nibble >> bit) & 0x1U) != 0;
                writeBit(b);
            }
            return;
        }

        for (int8_t bit = 3; bit >= 0; --bit) {
            const bool b = ((nibble >> bit) & 0x1U) != 0;
            writeBit(b);
        }
    };

    // 6 нибблов данных: bits [23:0]
    for (int8_t shift = 20; shift >= 0; shift -= 4) {
        writeNibbleByMode(static_cast<uint8_t>((value >> shift) & 0x0FU));
    }

    // 2 ниббла служебного байта: bits [31:24]
    for (int8_t shift = 28; shift >= 24; shift -= 4) {
        writeNibbleByMode(static_cast<uint8_t>((value >> shift) & 0x0FU));
    }

    digitalWrite(mosiPin_, LOW);

    // Импульс фиксации (LATCH) длительностью 2 мкс
    digitalWrite(latchPin_, HIGH);
    delayMicroseconds(LATCH_PULSE_US);
    digitalWrite(latchPin_, LOW);
}
