#include "config.h"
#include "hardware.h"
#include "time_utils.h"  // Для ntpUDP

WiFiUDP ntpUDP;           
NTPClient *timeClient = nullptr;

Preferences preferences;
Config config;

void initConfiguration() {
  preferences.begin("config", false);
  
  size_t stored_size = preferences.getBytesLength("data");
  
  // Проверяем, есть ли сохраненная конфигурация
  if(stored_size == 0) {
    // Нет конфигурации - устанавливаем defaults
    Serial.print("\n\n[SYSTEM] Конфигурация не найдена, создаём новую");
    setDefaultConfig();
  } else if (stored_size != sizeof(config)) {
    // Размер изменился - нужна миграция
    Serial.printf("\n\n[SYSTEM] Размер конфигурации изменился: %d -> %d байт", stored_size, sizeof(config));
    Serial.print("\n[SYSTEM] Выполняется миграция конфигурации...");
    
    // Загружаем старые данные (сколько влезет)
    size_t copy_size = (stored_size < sizeof(config)) ? stored_size : sizeof(config);
    memset(&config, 0, sizeof(config));  // Сначала обнуляем всё
    preferences.getBytes("data", &config, copy_size);
    
    // Инициализируем новые поля значениями по умолчанию
    if (config.time_config.manual_std_offset == 0 && config.time_config.manual_dst_offset == 0) {
      // Новые поля не были инициализированы - устанавливаем defaults
      config.time_config.manual_std_offset = 0;
      config.time_config.manual_dst_offset = 0;
      config.time_config.manual_dst_start_month = 0;
      config.time_config.manual_dst_start_week = 0;
      config.time_config.manual_dst_start_dow = 0;
      config.time_config.manual_dst_start_hour = 0;
      config.time_config.manual_dst_end_month = 0;
      config.time_config.manual_dst_end_week = 0;
      config.time_config.manual_dst_end_dow = 0;
      config.time_config.manual_dst_end_hour = 0;
    }
    
    // Пересохраняем с новым размером
    preferences.putBytes("data", &config, sizeof(config));
    Serial.print("\n[SYSTEM] Миграция завершена");
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
    strlcpy(config.wifi_ssid, "Hogwarts-2.4", sizeof(config.wifi_ssid));  // Первая сеть
    strlcpy(config.wifi_pass, "Alohomora!", sizeof(config.wifi_pass));
    strlcpy(config.wifi_ssid_2, "Hogwarts", sizeof(config.wifi_ssid_2));  // Вторая сеть
    strlcpy(config.wifi_pass_2, "Alohomora!", sizeof(config.wifi_pass_2));
    strlcpy(config.ntp_server, "pool.ntp.org", sizeof(config.ntp_server));
    
     // Timezone settings
    strcpy(config.time_config.timezone_name, DEFAULT_TIMEZONE_NAME);  // "Europe/Warsaw"
    config.time_config.automatic_localtime = true;     // По умолчанию используем ezTime
    
    // Вычисляемые значения (будут обновлены автоматически)
    config.time_config.current_offset = DEFAULT_TIMEZONE_OFFSET;  // +1 для Варшавы
    config.time_config.current_dst_active = false;
    
    // Синхронизация
    config.time_config.auto_sync_enabled = true;
    config.time_config.sync_interval_hours = 12;  // Синхронизировать каждые 12 часов
    config.time_config.last_ntp_sync = 0;         // Никогда не синхронизировались
    config.time_config.last_dcf77_sync = 0;
    config.time_config.sync_failures = 0;
    
    // Ручная настройка timezone (для опции 100)
    config.time_config.manual_std_offset = 0;
    config.time_config.manual_dst_offset = 0;
    config.time_config.manual_dst_start_month = 0;
    config.time_config.manual_dst_start_week = 0;
    config.time_config.manual_dst_start_dow = 0;
    config.time_config.manual_dst_start_hour = 0;
    config.time_config.manual_dst_end_month = 0;
    config.time_config.manual_dst_end_week = 0;
    config.time_config.manual_dst_end_dow = 0;
    config.time_config.manual_dst_end_hour = 0;
    
    // Дополнительные настройки
    config.time_config.manual_time_set = false;
    config.time_config.dcf77_enabled = true;      // DCF77 включён
    
    // Устаревшие поля (для совместимости)
    config.time_config.manual_offset = DEFAULT_TIMEZONE_OFFSET;
    config.time_config.dst_enabled = false;
    config.time_config.dst_active = false;
    config.time_config.auto_timezone = true;
    config.time_config.auto_dst = false;
    config.time_config.location_detected = false;
    config.time_config.detected_tz[0] = '\0';
    
    // Системные
    strlcpy(config.serial_number, "NC111115861", sizeof(config.serial_number));
    
    // Будильники
    config.alarm1 = {0, 0, false};
    config.alarm2 = {0, 0, false};
    
    saveConfig();
    Serial.print("\n[SYSTEM] Установлены настройки по умолчанию\n");
}

void saveConfig() {
  preferences.begin("config", false);
  preferences.putBytes("data", &config, sizeof(config));
  preferences.end();
}