#pragma once

#include <Arduino.h>
#include "display/display_manager.h"

// SPI Nixie driver (заготовка)
class NixieSpiDriver : public DisplayDriver {
public:
    NixieSpiDriver(uint8_t csPin, uint8_t sckPin, uint8_t mosiPin);

    void begin() override;
    void setBrightness(uint8_t level) override;
    void showTime(uint8_t hours, uint8_t minutes, uint8_t seconds, bool showColon) override;
    void testPattern() override;

private:
    uint8_t csPin_;
    uint8_t sckPin_;
    uint8_t mosiPin_;
    uint8_t brightness_ = 128;
};
