#include "config.h"
#include "hardware.h"

Preferences preferences;
Config config;

void initConfiguration() {
  preferences.begin("config", false);
  
  // Проверяем, есть ли сохраненная конфигурация
  if(preferences.getBytesLength("data") != sizeof(config)) {
    // Устанавливаем новые значения по умолчанию
    setDefaultConfig();
    Serial.println("Установлены настройки по умолчанию");
  } else {
    // Загружаем сохраненную конфигурацию
    preferences.getBytes("data", &config, sizeof(config));
    Serial.println("Конфигурация загружена из памяти");
  }
  
  preferences.end();
}

void setDefaultConfig() {
  memset(&config, 0, sizeof(config));
  strlcpy(config.wifi_ssid, "Hogwarts-2.4", sizeof(config.wifi_ssid));
  strlcpy(config.wifi_pass, "Alohomora!", sizeof(config.wifi_pass));
  strlcpy(config.ntp_server, "pool.ntp.org", sizeof(config.ntp_server));
  config.dst_enabled = false;
  config.dst_preset_index = 0; //Europe
  strlcpy(config.dst_rule, "UTC+0", sizeof(config.dst_rule));
  strlcpy(config.serial_number, "NC111115861", sizeof(config.serial_number));
  config.alarm1 = {0, 0, false};
  config.alarm2 = {0, 0, false};
  saveConfig();
}

void saveConfig() {
  preferences.begin("config", false);
  preferences.putBytes("data", &config, sizeof(config));
  preferences.end();
}