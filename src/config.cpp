#include "config.h"
#include "hardware.h"
#include "time_utils.h"  // Для ntpUDP

WiFiUDP ntpUDP;           
NTPClient *timeClient = nullptr;

Preferences preferences;
Config config;

void initConfiguration() {
  preferences.begin("config", false);
  
  // Проверяем, есть ли сохраненная конфигурация
  if(preferences.getBytesLength("data") != sizeof(config)) {
    // Устанавливаем новые значения по умолчанию
    setDefaultConfig();
  } else {
    // Загружаем сохраненную конфигурацию
    preferences.getBytes("data", &config, sizeof(config));
    Serial.print("\n\n[SYSTEM] Конфигурация загружена из памяти");
  }
  
  preferences.end();

  initNTPClient();
}

void initNTPClient() {
    if (!timeClient) {
        timeClient = new NTPClient(ntpUDP, config.ntp_server, 0);
        Serial.printf("\n[NTP] Клиент инициализирован с сервером: %s", config.ntp_server);
    }
}

void updateNTPServer(const char* server) {
    if (timeClient) {
        delete timeClient;
        timeClient = nullptr;
    }
    strlcpy(config.ntp_server, server, sizeof(config.ntp_server));
    initNTPClient();
    saveConfig();
    Serial.printf("\n[Config] NTP сервер обновлён: %s", server);
}

void setDefaultConfig() {
    memset(&config, 0, sizeof(config));
    
    // WiFi
    strlcpy(config.wifi_ssid, "Hogwarts-2.4", sizeof(config.wifi_ssid));
    strlcpy(config.wifi_pass, "Alohomora!", sizeof(config.wifi_pass));
    strlcpy(config.ntp_server, "pool.ntp.org", sizeof(config.ntp_server));
    
     // Timezone settings
    config.time_config.manual_time_set = false;
    config.time_config.auto_timezone = true;      // Включаем автоопределение
    config.time_config.auto_sync_enabled = true;
    config.time_config.auto_dst = true;           // Авто DST включено
    config.time_config.dcf77_enabled = true;      // DCF77 включён
    
    // Имя пояса по умолчанию
    strcpy(config.time_config.timezone_name, DEFAULT_TIMEZONE_NAME);
    config.time_config.manual_offset = DEFAULT_TIMEZONE_OFFSET;
    config.time_config.dst_enabled = false;       // Управляется библиотекой!
    
    // Состояние
    config.time_config.location_detected = false;
    config.time_config.detected_tz[0] = '\0';
    config.time_config.dst_active = false;
       
    // Синхронизация
    config.time_config.sync_interval_hours = 12;  // Синхронизировать каждые 12 часов
    config.time_config.last_ntp_sync = 0;         // Никогда не синхронизировались
    config.time_config.last_dcf77_sync = 0;
    config.time_config.sync_failures = 0;
    
    // Системные
    strlcpy(config.serial_number, "NC111115861", sizeof(config.serial_number));
    
    // Будильники
    config.alarm1 = {0, 0, false};
    config.alarm2 = {0, 0, false};
    
    saveConfig();
    Serial.print("\n\n[SYSTEM] Установлены настройки по умолчанию");
}

void saveConfig() {
  preferences.begin("config", false);
  preferences.putBytes("data", &config, sizeof(config));
  preferences.end();
}