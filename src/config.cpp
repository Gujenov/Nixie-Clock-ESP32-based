#include "config.h"
#include "hardware.h"
#include "time_utils.h"  // Для ntpUDP
#include "platform_profile.h"

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

    // Инициализация офлайн правил (POSIX)
    if (config.time_config.tz_posix[0] == '\0') {
      config.time_config.tz_posix[0] = '\0';
      config.time_config.tz_posix_zone[0] = '\0';
      config.time_config.tz_posix_updated = 0;
    }

    // Инициализация новых NTP серверов (NTP2/NTP3)
    if (config.ntp_server_1[0] == '\0') {
      strlcpy(config.ntp_server_1, "pool.ntp.org", sizeof(config.ntp_server_1));
    }
    if (config.ntp_server_2[0] == '\0') {
      strlcpy(config.ntp_server_2, "time.google.com", sizeof(config.ntp_server_2));
    }
    if (config.ntp_server_3[0] == '\0') {
      strlcpy(config.ntp_server_3, "time.cloudflare.com", sizeof(config.ntp_server_3));
    }

    // Инициализация настроек типа часов
    if (config.clock_digits == 0 || config.clock_digits > 6) {
      config.clock_digits = 6;
    }
    if (config.clock_type > CLOCK_TYPE_MECH_PEND) {
      config.clock_type = CLOCK_TYPE_NIXIE;
    }
    if (config.nix6_output_mode > NIX6_OUTPUT_REVERSE_INVERT) {
      config.nix6_output_mode = NIX6_OUTPUT_STD;
    }

    // Новые платформенные поля отсутствовали в старой версии структуры
    if (stored_size < sizeof(config)) {
      config.audio_module_enabled = true;
      config.ui_control_mode = UI_CONTROL_ENCODER_BUTTON;
    }

    if (config.ui_control_mode < UI_CONTROL_BUTTON_ONLY || config.ui_control_mode > UI_CONTROL_ENCODER_BUTTON) {
      config.ui_control_mode = UI_CONTROL_ENCODER_BUTTON;
    }

    // Инициализация новых полей будильников
    if (config.alarm1.melody == 0) config.alarm1.melody = 1;
    if (config.alarm2.melody == 0) config.alarm2.melody = 1;
    if (config.alarm1.days_mask == 0) config.alarm1.days_mask = 0x7F;
    if (config.alarm2.days_mask == 0) config.alarm2.days_mask = 0x7F;
    // alarm1.once по умолчанию = false
    
    // Пересохраняем с новым размером
    preferences.putBytes("data", &config, sizeof(config));
    Serial.print("\n[SYSTEM] Миграция завершена");
  } else {
    // Загружаем сохраненную конфигурацию
    preferences.getBytes("data", &config, sizeof(config));
    Serial.print("\n\n[SYSTEM] Конфигурация загружена из памяти");
  }
  
  preferences.end();

  // Гарантируем корректные значения новых полей
  if (config.alarm1.melody == 0) config.alarm1.melody = 1;
  if (config.alarm2.melody == 0) config.alarm2.melody = 1;
  if (config.alarm1.days_mask == 0) config.alarm1.days_mask = 0x7F;
  if (config.alarm2.days_mask == 0) config.alarm2.days_mask = 0x7F;

  if (config.ntp_server_1[0] == '\0') {
    strlcpy(config.ntp_server_1, "pool.ntp.org", sizeof(config.ntp_server_1));
  }
  if (config.ntp_server_2[0] == '\0') {
    strlcpy(config.ntp_server_2, "time.google.com", sizeof(config.ntp_server_2));
  }
  if (config.ntp_server_3[0] == '\0') {
    strlcpy(config.ntp_server_3, "time.cloudflare.com", sizeof(config.ntp_server_3));
  }

  if (config.nix6_output_mode > NIX6_OUTPUT_REVERSE_INVERT) {
    config.nix6_output_mode = NIX6_OUTPUT_STD;
  }

  if (config.ui_control_mode < UI_CONTROL_BUTTON_ONLY || config.ui_control_mode > UI_CONTROL_ENCODER_BUTTON) {
    config.ui_control_mode = UI_CONTROL_ENCODER_BUTTON;
  }

  platformRefreshCapabilities();
  initNTPClient();
}

void initNTPClient() {
    if (!timeClient) {
    timeClient = new NTPClient(ntpUDP, config.ntp_server_1, 0);
    Serial.printf("\n[NTP] Клиент инициализирован. Базовый сервер: %s", config.ntp_server_1);
    }
}

void updateNTPServer(uint8_t index, const char* server) {
  char* target = nullptr;
  size_t targetSize = 0;

  switch (index) {
    case 1:
      target = config.ntp_server_1;
      targetSize = sizeof(config.ntp_server_1);
      break;
    case 2:
      target = config.ntp_server_2;
      targetSize = sizeof(config.ntp_server_2);
      break;
    case 3:
      target = config.ntp_server_3;
      targetSize = sizeof(config.ntp_server_3);
      break;
    default:
      Serial.print("\n[Config] Ошибка: неверный индекс NTP сервера");
      return;
  }

  strlcpy(target, server, targetSize);

  if (index == 1) {
    if (timeClient) {
      delete timeClient;
      timeClient = nullptr;
    }
    initNTPClient();
  }

  saveConfig();
  Serial.printf("\n[Config] NTP сервер %d обновлён: %s", index, server);
}

void setDefaultConfig() {
  char saved_serial[sizeof(config.serial_number)];
  strlcpy(saved_serial, config.serial_number, sizeof(saved_serial));

  ClockType saved_clock_type = config.clock_type;
  uint8_t saved_clock_digits = config.clock_digits;
  Nix6OutputMode saved_nix6_output_mode = config.nix6_output_mode;

  memset(&config, 0, sizeof(config));
    
    // WiFi
    strlcpy(config.wifi_ssid, "Hogwarts-2.4", sizeof(config.wifi_ssid));  // Первая сеть
    strlcpy(config.wifi_pass, "Alohomora!", sizeof(config.wifi_pass));
    strlcpy(config.wifi_ssid_2, "Hogwarts", sizeof(config.wifi_ssid_2));  // Вторая сеть
    strlcpy(config.wifi_pass_2, "Alohomora!", sizeof(config.wifi_pass_2));
    strlcpy(config.ntp_server_1, "pool.ntp.org", sizeof(config.ntp_server_1));
    strlcpy(config.ntp_server_2, "time.google.com", sizeof(config.ntp_server_2));
    strlcpy(config.ntp_server_3, "time.cloudflare.com", sizeof(config.ntp_server_3));
    
     // Timezone settings
    strcpy(config.time_config.timezone_name, DEFAULT_TIMEZONE_NAME);  // "Europe/Warsaw"
    config.time_config.automatic_localtime = true;     // По умолчанию используем ezTime
    
    // Вычисляемые значения (будут обновлены автоматически)
    config.time_config.current_offset = DEFAULT_TIMEZONE_OFFSET;  // +1 для Варшавы
    config.time_config.current_dst_active = false;
    
    // Синхронизация
    config.time_config.auto_sync_enabled = true;
    config.time_config.last_ntp_sync = 0;         // Никогда не синхронизировались
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

    // Офлайн правила (POSIX)
    config.time_config.tz_posix[0] = '\0';
    config.time_config.tz_posix_zone[0] = '\0';
    config.time_config.tz_posix_updated = 0;
    
    // Дополнительные настройки
    config.time_config.manual_time_set = false;
    
    // Системные (серийный номер не сбрасываем)
    if (saved_serial[0] != '\0') {
      strlcpy(config.serial_number, saved_serial, sizeof(config.serial_number));
    } else {
      strlcpy(config.serial_number, "NC111115861", sizeof(config.serial_number));
    }

    // Тип часов и количество разрядов (не сбрасываем)
    if (saved_clock_type <= CLOCK_TYPE_MECH_PEND) {
      config.clock_type = saved_clock_type;
    } else {
      config.clock_type = CLOCK_TYPE_NIXIE;
    }
    if (saved_clock_digits >= 1 && saved_clock_digits <= 6) {
      config.clock_digits = saved_clock_digits;
    } else {
      config.clock_digits = 6;
    }

    if (saved_nix6_output_mode <= NIX6_OUTPUT_REVERSE_INVERT) {
      config.nix6_output_mode = saved_nix6_output_mode;
    } else {
      config.nix6_output_mode = NIX6_OUTPUT_STD;
    }

    config.audio_module_enabled = true;
    config.ui_control_mode = UI_CONTROL_ENCODER_BUTTON;
    
    // Будильники
    config.alarm1 = {0, 0, false, 1, 0x7F, false};
    config.alarm2 = {0, 0, false, 1, 0x7F, false};
    
    saveConfig();
    Serial.print("\n[SYSTEM] Установлены настройки по умолчанию\n");
}

void saveConfig() {
  preferences.begin("config", false);
  preferences.putBytes("data", &config, sizeof(config));
  preferences.end();
  platformRefreshCapabilities();
}