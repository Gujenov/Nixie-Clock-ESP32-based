#include "dcf77_handler.h"
#include "time_utils.h"

// Глобальные объекты
static DCF77* dcf = nullptr;
static bool dcfEnabled = false;
static uint32_t lastSyncMillis = 0;

void initDCF77() {
    if (!config.time_config.dcf77_enabled) {
        return;
    }
    
    // Включаем модуль
    if (DCF_ENABLE_PIN > 0) {
        pinMode(DCF_ENABLE_PIN, OUTPUT);
        digitalWrite(DCF_ENABLE_PIN, HIGH);
        delay(50);
    }
    
    // Инициализация DCF77
    dcf = new DCF77(DCF_DATA_PIN, digitalPinToInterrupt(DCF_DATA_PIN));
    dcf->Start();
    
    dcfEnabled = true;
    lastSyncMillis = 0;
    
    Serial.print("\n[DCF77] Запущен. Ожидание сигнала...");
}

void updateDCF77() {
    if (!dcfEnabled || !dcf) return;
    
    // Проверяем наличие нового времени
    time_t dcfTime = dcf->getUTCTime();  // ТОЛЬКО UTC!
    
    if (dcfTime != 0 && dcfTime > 1609459200) {  // Проверка что время корректное
        lastSyncMillis = millis();
        
        // Устанавливаем время во все источники
        setTimeToAllSources(dcfTime);
        
        // Сохраняем время последней синхронизации
        config.time_config.last_dcf77_sync = dcfTime;
        saveConfig();
        
        // Выводим информацию
        struct tm* tm_info = gmtime(&dcfTime);
        Serial.printf("[DCF77] Синхронизировано: %02d:%02d:%02d UTC\n",
                     tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
    }
}

bool isDCF77SignalAvailable() {
    if (!dcfEnabled || !dcf) return false;
    return (dcf->getUTCTime() != 0);
}

time_t getDCF77Time() {
    if (!dcfEnabled || !dcf) return 0;
    return dcf->getUTCTime();
}

void dcf77Enable(bool enable) {
    if (enable && !dcfEnabled) {
        initDCF77();
    } else if (!enable && dcfEnabled) {
        if (dcf) {
            dcf->Stop();
            delete dcf;
            dcf = nullptr;
        }
        if (DCF_ENABLE_PIN > 0) {
            digitalWrite(DCF_ENABLE_PIN, LOW);
        }
        dcfEnabled = false;
    }
}

const char* getDCF77Status() {
    if (!dcfEnabled) return "Выключен";
    if (lastSyncMillis == 0) return "Ожидание сигнала";
    
    uint32_t secondsAgo = (millis() - lastSyncMillis) / 1000;
    static char buffer[40];
    snprintf(buffer, sizeof(buffer), "Активен, синхронизация %lu сек назад", secondsAgo);
    return buffer;
}