// Simple Utils compatibility header for DCF77 library
// Provides basic Log / LogLn wrappers using Serial so the original
// library's logging calls compile on ESP32/Arduino platforms.
#ifndef UTILS_H
#define UTILS_H

#include <Arduino.h>

namespace Utils {

inline void Log(const char* s, int /*base*/ = DEC) {
    Serial.print(s);
}

inline void Log(const String& s, int /*base*/ = DEC) {
    Serial.print(s);
}

inline void Log(unsigned long v, int base = DEC) {
    Serial.print(v, base);
}

inline void LogLn(const char* s = "") {
    Serial.println(s);
}

inline void LogLn(const String& s) {
    Serial.println(s);
}

} // namespace Utils

#endif // UTILS_H
