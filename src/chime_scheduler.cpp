#include "chime_scheduler.h"

#include <Arduino.h>

#include "audio_task.h"
#include "config.h"
#include "platform_profile.h"

namespace {

enum class PendingChimeType : uint8_t {
    None = 0,
    Hourly,
    Quarter
};

struct PendingChimeState {
    PendingChimeType type = PendingChimeType::None;
    uint8_t repeatsRemaining = 0;
};

static PendingChimeState pendingChime;
static int32_t lastChimeQuarterMarker = -1;

static bool isHourInHalfOpenRange(uint8_t hour, uint8_t startHour, uint8_t endHour) {
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

static bool isBellActiveBySchedule(const tm& localTm) {
    const uint8_t hour = static_cast<uint8_t>((localTm.tm_hour < 0) ? 0 : (localTm.tm_hour % 24));
    return isHourInHalfOpenRange(hour,
                                 config.chime_active_start_hour,
                                 config.chime_active_end_hour);
}

static void scheduleChime(PendingChimeType type, uint8_t repeats) {
    if (type == PendingChimeType::None || repeats == 0) {
        return;
    }

    if (pendingChime.repeatsRemaining != 0) {
        Serial.println("[AUDIO][BELL] Предыдущая серия ещё не завершена, новая заменяет старую");
    }

    pendingChime.type = type;
    pendingChime.repeatsRemaining = repeats;
    Serial.printf("[AUDIO][BELL] Запланировано: type=%s, repeats=%u\n",
                  type == PendingChimeType::Hourly ? "hourly" : "quarter",
                  static_cast<unsigned>(repeats));
}

} // namespace

void chimeSchedulerReset() {
    pendingChime.type = PendingChimeType::None;
    pendingChime.repeatsRemaining = 0;
    lastChimeQuarterMarker = -1;
}

void chimeSchedulerService() {
    if (pendingChime.repeatsRemaining == 0 || pendingChime.type == PendingChimeType::None) {
        return;
    }

    if (!platformGetCapabilities().sound_enabled) {
        pendingChime.type = PendingChimeType::None;
        pendingChime.repeatsRemaining = 0;
        Serial.println("[AUDIO][BELL] Пропуск: аудио отключено");
        return;
    }

    if (!audioTaskIsRunning()) {
        audioTaskStart();
    }

    if (audioIsPlaying()) {
        return;
    }

    bool started = false;
    if (pendingChime.type == PendingChimeType::Hourly) {
        started = audioPlayChimeHourly();
    } else if (pendingChime.type == PendingChimeType::Quarter) {
        started = audioPlayChimeQuarter();
    }

    if (started) {
        if (pendingChime.repeatsRemaining > 0) {
            pendingChime.repeatsRemaining--;
        }
        if (pendingChime.repeatsRemaining == 0) {
            pendingChime.type = PendingChimeType::None;
        }
    } else {
        Serial.println("\n[AUDIO][BELL] Не удалось запустить воспроизведение, серия отменена");
        pendingChime.type = PendingChimeType::None;
        pendingChime.repeatsRemaining = 0;
    }
}

void chimeSchedulerOnTick(const tm& localTm) {
    const bool quarterBoundary = (localTm.tm_sec == 0) && ((localTm.tm_min % 15) == 0);
    if (!quarterBoundary) {
        return;
    }

    const int32_t quarterMarker = (localTm.tm_yday * 24 * 4) + (localTm.tm_hour * 4) + (localTm.tm_min / 15);
    if (quarterMarker == lastChimeQuarterMarker) {
        return;
    }
    lastChimeQuarterMarker = quarterMarker;

    const uint8_t chimeMode = config.chimes_per_hour;
    const bool chimeEnabled = (chimeMode == 1 || chimeMode == 2 || chimeMode == 4);
    const bool chimeWindowActive = isBellActiveBySchedule(localTm);
    if (!chimeEnabled || !chimeWindowActive) {
        return;
    }

    if (localTm.tm_min == 0) {
        uint8_t hour12 = static_cast<uint8_t>(localTm.tm_hour % 12);
        if (hour12 == 0) hour12 = 12;
        scheduleChime(PendingChimeType::Hourly, hour12);
        return;
    }

    if (chimeMode == 2 && localTm.tm_min == 30) {
        scheduleChime(PendingChimeType::Quarter, 1);
        return;
    }

    if (chimeMode == 4) {
        if (localTm.tm_min == 15) {
            scheduleChime(PendingChimeType::Quarter, 1);
        } else if (localTm.tm_min == 30) {
            scheduleChime(PendingChimeType::Quarter, 2);
        } else if (localTm.tm_min == 45) {
            scheduleChime(PendingChimeType::Quarter, 3);
        }
    }
}

bool bellSchedulerIsEnabled() {
    const uint8_t chimeMode = config.chimes_per_hour;
    return (chimeMode == 1 || chimeMode == 2 || chimeMode == 4);
}

bool bellSchedulerIsWindowActive(const tm& localTm) {
    return isBellActiveBySchedule(localTm);
}

bool bellSchedulerIsActiveNow(const tm& localTm) {
    return bellSchedulerIsEnabled() && bellSchedulerIsWindowActive(localTm);
}
