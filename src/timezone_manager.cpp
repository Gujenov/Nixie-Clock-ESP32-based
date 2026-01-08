#include "config.h"
#include <string.h>
#include <ezTime.h>

// Логируем, когда используем ezTime vs ручной режим
static void logEzTimeState(const char* msg) {
  Serial.print("[TZ]");
  if (msg) Serial.print(" "), Serial.println(msg);
}

// Use ezTime when available, fall back to manual offset if not
static Timezone localTZ;
static bool tzAvailable = false;

bool initTimezone() {
  // Configure ezTime NTP server and interval from config
  setServer(String(config.ntp_server));
  setInterval(config.time_config.sync_interval_hours * 3600);

  // If we have a timezone name, try to load it
  if (config.time_config.timezone_name[0] != '\0') {
    if (localTZ.setLocation(String(config.time_config.timezone_name))) {
      localTZ.setDefault();
      tzAvailable = true;
      // DST is manual-only; do not auto-update dst_active here
      config.time_config.location_detected = true;
      strlcpy(config.time_config.detected_tz, config.time_config.timezone_name, sizeof(config.time_config.detected_tz));
      logEzTimeState("ezTime загружен и применён (авто пояс)");
      return true;
    } else {
      // couldn't load remote timezone (network/cache), fall back
      tzAvailable = false;
      logEzTimeState("ezTime недоступен, fallback на ручной смещение");
    }
  }

  // Ensure timezone name and manual offset are sane
  if (config.time_config.timezone_name[0] == '\0') {
    strncpy(config.time_config.timezone_name, DEFAULT_TIMEZONE_NAME, sizeof(config.time_config.timezone_name));
    config.time_config.manual_offset = DEFAULT_TIMEZONE_OFFSET;
  }

  config.time_config.dst_active = false;
  return true;
}

time_t utcToLocal(time_t utc) {
  if (tzAvailable) {
    String tzname;
    bool isdst = false;
    int16_t offset = 0;
    time_t local = localTZ.tzTime(utc, UTC_TIME, tzname, isdst, offset);
    // DST is manual-only; do not override manual DST flag based on tz rules
    return local;
  }
  int32_t offset_seconds = (int32_t)config.time_config.manual_offset * 3600;
  if (config.time_config.dst_enabled && config.time_config.dst_active) offset_seconds += 3600;
  return utc + offset_seconds;
}

time_t localToUtc(time_t local) {
  if (tzAvailable) {
    String tzname;
    bool isdst = false;
    int16_t offset = 0;
    time_t utc = localTZ.tzTime(local, LOCAL_TIME, tzname, isdst, offset);
    // DST is manual-only; do not override manual DST flag based on tz rules
    return utc;
  }
  int32_t offset_seconds = (int32_t)config.time_config.manual_offset * 3600;
  if (config.time_config.dst_enabled && config.time_config.dst_active) offset_seconds += 3600;
  return local - offset_seconds;
}

bool setTimezone(const char* tz_name) {
  if (!tz_name) return false;
  strncpy(config.time_config.timezone_name, tz_name, sizeof(config.time_config.timezone_name));
  config.time_config.timezone_name[sizeof(config.time_config.timezone_name)-1] = '\0';

  // Try to load via ezTime
  if (localTZ.setLocation(String(tz_name))) {
    localTZ.setDefault();
    tzAvailable = true;
    // DST is manual-only; do not auto-set DST active
    config.time_config.location_detected = true;
    strlcpy(config.time_config.detected_tz, config.time_config.timezone_name, sizeof(config.time_config.detected_tz));
    Serial.printf("[TZ] ezTime применён: %s\n", tz_name);
    return true;
  }

  // If loading failed, switch to manual offset mode
  tzAvailable = false;
  Serial.printf("[TZ] ezTime не загрузил %s, переключаюсь на ручной режим\n", tz_name);
  return false;
}

bool setTimezoneOffset(int8_t offset) {
  config.time_config.manual_offset = offset;
  config.time_config.dst_active = false;
  tzAvailable = false; // respect manual override
  Serial.printf("[TZ] Ручное смещение UTC%+d, ezTime отключён\n", offset);
  return true;
}

void updateDSTStatus() {
  // DST is user-controlled (manual). No automatic updates performed.
}
