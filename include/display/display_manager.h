#pragma once

#include <Arduino.h>

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
class DisplayManager {
public:
    void attach(DisplayDriver* driver);
    void begin();
    void setBrightness(uint8_t level);
    void showTime(uint8_t hours, uint8_t minutes, uint8_t seconds, bool showColon);
    void testPattern();

private:
    DisplayDriver* driver_ = nullptr;
};
