#include "display/display_manager.h"

void DisplayManager::attach(DisplayDriver* driver) {
    driver_ = driver;
}

void DisplayManager::begin() {
    if (driver_) {
        driver_->begin();
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
