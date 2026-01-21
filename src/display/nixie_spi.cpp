#include "display/nixie_spi.h"

NixieSpiDriver::NixieSpiDriver(uint8_t csPin, uint8_t sckPin, uint8_t mosiPin)
    : csPin_(csPin), sckPin_(sckPin), mosiPin_(mosiPin) {}

void NixieSpiDriver::begin() {
    pinMode(csPin_, OUTPUT);
    pinMode(sckPin_, OUTPUT);
    pinMode(mosiPin_, OUTPUT);
    digitalWrite(csPin_, HIGH);
}

void NixieSpiDriver::setBrightness(uint8_t level) {
    brightness_ = level;
}

void NixieSpiDriver::showTime(uint8_t hours, uint8_t minutes, uint8_t seconds, bool showColon) {
    (void)hours;
    (void)minutes;
    (void)seconds;
    (void)showColon;
    // TODO: реализация SPI вывода для Nixie
}

void NixieSpiDriver::testPattern() {
    // TODO: тестовый шаблон дисплея
}
