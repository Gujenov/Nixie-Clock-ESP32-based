#include "runtime_counter.h"

#include <Preferences.h>

namespace {

constexpr const char* RUNTIME_NS = "runtime";
constexpr const char* RUNTIME_KEY_A = "rec0";
constexpr const char* RUNTIME_KEY_B = "rec1";
constexpr uint32_t RUNTIME_MAGIC = 0x52544D45; // RTME
constexpr uint32_t RUNTIME_VERSION_V1 = 1;
constexpr uint32_t RUNTIME_VERSION_V2 = 2;
constexpr uint32_t RUNTIME_VERSION = RUNTIME_VERSION_V2;
constexpr uint32_t SAVE_PERIOD_SECONDS = 1800;  // 30 минут

struct RuntimeRecordV1 {
    uint32_t magic;
    uint32_t version;
    uint32_t sequence;
    uint32_t bootCount;
    uint64_t totalRunSeconds;
    uint32_t crc;
};

struct RuntimeRecord {
    uint32_t magic;
    uint32_t version;
    uint32_t sequence;
    uint32_t bootCount;
    uint64_t totalRunSeconds;
    uint32_t lastServiceDateYmd;       // YYYYMMDD
    uint64_t lastServiceRunSeconds;    // Моточасы (в секундах) на момент сервиса
    uint32_t crc;
};

Preferences g_prefs;
RuntimeRecord g_state = {};
bool g_initialized = false;
bool g_activeSlotIsA = true;
uint32_t g_unsavedSeconds = 0;

uint32_t fnv1aHash(const uint8_t* bytes, size_t len) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < len; ++i) {
        hash ^= bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

uint32_t hashRecordV1(const RuntimeRecordV1& rec) {
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&rec);
    const size_t len = sizeof(RuntimeRecordV1) - sizeof(rec.crc);
    return fnv1aHash(bytes, len);
}

uint32_t hashRecordV2(const RuntimeRecord& rec) {
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&rec);
    const size_t len = sizeof(RuntimeRecord) - sizeof(rec.crc);
    return fnv1aHash(bytes, len);
}

bool isValidRecordV1(const RuntimeRecordV1& rec) {
    if (rec.magic != RUNTIME_MAGIC) return false;
    if (rec.version != RUNTIME_VERSION_V1) return false;
    return rec.crc == hashRecordV1(rec);
}

bool isValidRecordV2(const RuntimeRecord& rec) {
    if (rec.magic != RUNTIME_MAGIC) return false;
    if (rec.version != RUNTIME_VERSION_V2) return false;
    return rec.crc == hashRecordV2(rec);
}

bool readRecord(const char* key, RuntimeRecord& out) {
    if (!g_prefs.begin(RUNTIME_NS, true)) {
        return false;
    }

    size_t len = g_prefs.getBytesLength(key);
    if (len == sizeof(RuntimeRecord)) {
        RuntimeRecord rec = {};
        size_t got = g_prefs.getBytes(key, &rec, sizeof(RuntimeRecord));
        g_prefs.end();
        if (got == sizeof(RuntimeRecord) && isValidRecordV2(rec)) {
            out = rec;
            return true;
        }
        return false;
    }

    if (len == sizeof(RuntimeRecordV1)) {
        RuntimeRecordV1 rec = {};
        size_t got = g_prefs.getBytes(key, &rec, sizeof(RuntimeRecordV1));
        g_prefs.end();
        if (got == sizeof(RuntimeRecordV1) && isValidRecordV1(rec)) {
            out.magic = rec.magic;
            out.version = RUNTIME_VERSION_V2;
            out.sequence = rec.sequence;
            out.bootCount = rec.bootCount;
            out.totalRunSeconds = rec.totalRunSeconds;
            out.lastServiceDateYmd = 0;
            out.lastServiceRunSeconds = 0;
            out.crc = 0;
            return true;
        }
        return false;
    }

    g_prefs.end();
    return false;
}

bool writeRecord(const char* key, RuntimeRecord& rec) {
    rec.magic = RUNTIME_MAGIC;
    rec.version = RUNTIME_VERSION;
    rec.crc = hashRecordV2(rec);

    if (!g_prefs.begin(RUNTIME_NS, false)) {
        return false;
    }

    size_t written = g_prefs.putBytes(key, &rec, sizeof(RuntimeRecord));
    g_prefs.end();
    return written == sizeof(RuntimeRecord);
}

void writeNextSlot() {
    const bool writeToA = !g_activeSlotIsA;
    const char* key = writeToA ? RUNTIME_KEY_A : RUNTIME_KEY_B;

    RuntimeRecord copy = g_state;
    if (writeRecord(key, copy)) {
        g_activeSlotIsA = writeToA;
        g_unsavedSeconds = 0;
    }
}

bool writeCurrentToNextSlot() {
    const bool nextIsA = !g_activeSlotIsA;
    const char* key = nextIsA ? RUNTIME_KEY_A : RUNTIME_KEY_B;

    RuntimeRecord copy = g_state;
    if (writeRecord(key, copy)) {
        g_activeSlotIsA = nextIsA;
        g_unsavedSeconds = 0;
        return true;
    }

    return false;
}

} // namespace

void runtimeCounterInit() {
    RuntimeRecord recA = {};
    RuntimeRecord recB = {};
    const bool validA = readRecord(RUNTIME_KEY_A, recA);
    const bool validB = readRecord(RUNTIME_KEY_B, recB);

    if (validA && validB) {
        if (recA.sequence >= recB.sequence) {
            g_state = recA;
            g_activeSlotIsA = true;
        } else {
            g_state = recB;
            g_activeSlotIsA = false;
        }
    } else if (validA) {
        g_state = recA;
        g_activeSlotIsA = true;
    } else if (validB) {
        g_state = recB;
        g_activeSlotIsA = false;
    } else {
        g_state.magic = RUNTIME_MAGIC;
        g_state.version = RUNTIME_VERSION;
        g_state.sequence = 0;
        g_state.bootCount = 0;
        g_state.totalRunSeconds = 0;
        g_state.lastServiceDateYmd = 0;
        g_state.lastServiceRunSeconds = 0;
        g_state.crc = 0;
        g_activeSlotIsA = true;
    }

    g_state.bootCount += 1;
    g_state.sequence += 1;
    g_unsavedSeconds = 0;
    g_initialized = true;

    writeNextSlot();
}

void runtimeCounterOnSecondTick() {
    if (!g_initialized) {
        return;
    }

    g_state.totalRunSeconds += 1;
    g_unsavedSeconds += 1;

    if (g_unsavedSeconds >= SAVE_PERIOD_SECONDS) {
        g_state.sequence += 1;
        writeNextSlot();
    }
}

bool runtimeCounterSaveNow() {
    if (!g_initialized) {
        return false;
    }

    g_state.sequence += 1;
    return writeCurrentToNextSlot();
}

bool runtimeCounterMarkService(uint32_t serviceDateYmd) {
    if (!g_initialized) {
        return false;
    }

    g_state.lastServiceDateYmd = serviceDateYmd;
    g_state.lastServiceRunSeconds = g_state.totalRunSeconds;
    g_state.sequence += 1;
    return writeCurrentToNextSlot();
}

bool runtimeCounterResetAll() {
    if (!g_initialized) {
        return false;
    }

    g_state.bootCount = 0;
    g_state.totalRunSeconds = 0;
    g_state.lastServiceDateYmd = 0;
    g_state.lastServiceRunSeconds = 0;
    g_state.sequence += 1;
    g_unsavedSeconds = 0;
    return writeCurrentToNextSlot();
}

uint32_t runtimeCounterGetBootCount() {
    return g_state.bootCount;
}

uint64_t runtimeCounterGetTotalRunSeconds() {
    return g_state.totalRunSeconds;
}

float runtimeCounterGetMotorHours() {
    return static_cast<float>(g_state.totalRunSeconds) / 3600.0f;
}

uint32_t runtimeCounterGetUnsavedSeconds() {
    return g_unsavedSeconds;
}

uint32_t runtimeCounterGetLastServiceDate() {
    return g_state.lastServiceDateYmd;
}

uint64_t runtimeCounterGetLastServiceRunSeconds() {
    return g_state.lastServiceRunSeconds;
}

float runtimeCounterGetLastServiceMotorHours() {
    return static_cast<float>(g_state.lastServiceRunSeconds) / 3600.0f;
}
