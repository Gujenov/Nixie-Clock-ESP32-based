// Minimal TimeLib compatibility shim for DCF77 library
// Provides tmElements_t, now(), makeTime(), and common constants
#ifndef TIMELIB_H
#define TIMELIB_H

#include <time.h>
#include <stdint.h>

#define SECS_PER_MIN 60
#define SECS_PER_HOUR 3600

typedef time_t time_t_t; // alias to avoid conflicts if needed

// tmElements_t uses Year as years since 1970 (to match TimeLib behaviour used by DCF77)
typedef struct {
  uint8_t Second;
  uint8_t Minute;
  uint8_t Hour;
  uint8_t Day;
  uint8_t Month;
  int Year; // years since 1970
} tmElements_t;

inline time_t now() {
  return time(NULL);
}

inline time_t makeTime(const tmElements_t &te) {
  struct tm t;
  t.tm_sec = te.Second;
  t.tm_min = te.Minute;
  t.tm_hour = te.Hour;
  t.tm_mday = te.Day;
  t.tm_mon = (te.Month > 0) ? (te.Month - 1) : 0;
  // tmElements_t::Year is years since 1970; struct tm expects years since 1900
  t.tm_year = te.Year + 70;
  t.tm_isdst = -1;
  return mktime(&t);
}

#endif // TIMELIB_H
