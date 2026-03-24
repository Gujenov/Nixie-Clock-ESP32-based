#include "config.h"
#include "hardware.h"
#include "time_utils.h"  // Для ntpUDP
#include "platform_profile.h"

WiFiUDP ntpUDP;           
NTPClient *timeClient = nullptr;

Preferences preferences;
Config config;

namespace {

// Layout до добавления пользовательских настроек звука/дисплея.
// Нужен для корректной миграции без сдвига alarm1/alarm2.
struct ConfigLegacyV1 {
  char wifi_ssid[32];
  char wifi_pass[32];
  char wifi_ssid_2[32];
  char wifi_pass_2[32];
  char ntp_server_1[NTP_SERVER_SIZE];
  char ntp_server_2[NTP_SERVER_SIZE];
  char ntp_server_3[NTP_SERVER_SIZE];
  TimeConfig time_config;
  char serial_number[12];
  ClockType clock_type;
  uint8_t clock_digits;
  Nix6OutputMode nix6_output_mode;
  bool audio_module_enabled;
  bool ir_sensor_enabled;
  UiControlMode ui_control_mode;
  AlarmSettings alarm1;
  AlarmSettings alarm2;
};

void applyNewUserSettingsDefaults() {
  config.alarm_volume = 50;
  config.chime_volume = 50;
  config.chimes_per_hour = 1;
  config.chime_active_start_hour = 0;
  config.chime_active_end_hour = 24;

  config.brightness_control_enabled = false;
  config.brightness_sensor_max = 900;
  config.brightness_sensor_min = 100;
  config.display_active_start_hour = 0;
  config.display_active_end_hour = 24;
  config.display_active_start_hour_2 = 0;
  config.display_active_end_hour_2 = 0;
  config.display_holiday_active_start_hour = 0;
  config.display_holiday_active_end_hour = 24;
  config.display_holiday_active_start_hour_2 = 0;
  config.display_holiday_active_end_hour_2 = 0;

  config.light_filter_samples = 8;
  config.light_sensor_resolution_bits = 10;
}

} // namespace

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

    // Специальная миграция со старого layout, чтобы не потерять будильники.
    if (stored_size == sizeof(ConfigLegacyV1)) {
      ConfigLegacyV1 oldCfg = {};
      preferences.getBytes("data", &oldCfg, sizeof(oldCfg));
      memset(&config, 0, sizeof(config));

      memcpy(config.wifi_ssid, oldCfg.wifi_ssid, sizeof(config.wifi_ssid));
      memcpy(config.wifi_pass, oldCfg.wifi_pass, sizeof(config.wifi_pass));
      memcpy(config.wifi_ssid_2, oldCfg.wifi_ssid_2, sizeof(config.wifi_ssid_2));
      memcpy(config.wifi_pass_2, oldCfg.wifi_pass_2, sizeof(config.wifi_pass_2));
      memcpy(config.ntp_server_1, oldCfg.ntp_server_1, sizeof(config.ntp_server_1));
      memcpy(config.ntp_server_2, oldCfg.ntp_server_2, sizeof(config.ntp_server_2));
      memcpy(config.ntp_server_3, oldCfg.ntp_server_3, sizeof(config.ntp_server_3));

      config.time_config = oldCfg.time_config;
      memcpy(config.serial_number, oldCfg.serial_number, sizeof(config.serial_number));

      config.clock_type = oldCfg.clock_type;
      config.clock_digits = oldCfg.clock_digits;
      config.nix6_output_mode = oldCfg.nix6_output_mode;
      config.audio_module_enabled = oldCfg.audio_module_enabled;
      config.ir_sensor_enabled = oldCfg.ir_sensor_enabled;
      config.ui_control_mode = oldCfg.ui_control_mode;

      config.alarm1 = oldCfg.alarm1;
      config.alarm2 = oldCfg.alarm2;

      applyNewUserSettingsDefaults();
      Serial.print("\n[SYSTEM] Применена точная миграция legacy-конфигурации (с сохранением будильников)");
    } else {
      // Универсальная миграция (best effort)
      size_t copy_size = (stored_size < sizeof(config)) ? stored_size : sizeof(config);
      memset(&config, 0, sizeof(config));
      preferences.getBytes("data", &config, copy_size);

      if (stored_size < sizeof(config)) {
        applyNewUserSettingsDefaults();
      }
    }
    
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
      config.ir_sensor_enabled = false;
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

  if (config.alarm_volume > 100) config.alarm_volume = 50;
  if (config.chime_volume > 100) config.chime_volume = 50;
  if (!(config.chimes_per_hour == 0 || config.chimes_per_hour == 1 || config.chimes_per_hour == 2 || config.chimes_per_hour == 4)) {
    config.chimes_per_hour = 1;
  }
  if (config.chime_active_start_hour > 24) config.chime_active_start_hour = 0;
  if (config.chime_active_end_hour > 24) config.chime_active_end_hour = 24;

  if (config.brightness_sensor_max > 1023) config.brightness_sensor_max = 900;
  if (config.brightness_sensor_min > 1023) config.brightness_sensor_min = 100;
  if (config.brightness_sensor_min >= config.brightness_sensor_max) {
    config.brightness_sensor_min = 100;
    config.brightness_sensor_max = 900;
  }
  if (config.display_active_start_hour > 24) config.display_active_start_hour = 0;
  if (config.display_active_end_hour > 24) config.display_active_end_hour = 24;
  if (config.display_active_start_hour_2 > 24) config.display_active_start_hour_2 = 0;
  if (config.display_active_end_hour_2 > 24) config.display_active_end_hour_2 = 0;
  if (config.display_holiday_active_start_hour > 24) config.display_holiday_active_start_hour = 0;
  if (config.display_holiday_active_end_hour > 24) config.display_holiday_active_end_hour = 24;
  if (config.display_holiday_active_start_hour_2 > 24) config.display_holiday_active_start_hour_2 = 0;
  if (config.display_holiday_active_end_hour_2 > 24) config.display_holiday_active_end_hour_2 = 0;

  if (config.light_filter_samples == 0 || config.light_filter_samples > 64) {
    config.light_filter_samples = 8;
  }
  if (config.light_sensor_resolution_bits < 9 || config.light_sensor_resolution_bits > 12) {
    config.light_sensor_resolution_bits = 10;
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
    config.ir_sensor_enabled = false;
    config.ui_control_mode = UI_CONTROL_ENCODER_BUTTON;

    config.alarm_volume = 50;
    config.chime_volume = 50;
    config.chimes_per_hour = 1;
    config.chime_active_start_hour = 0;
    config.chime_active_end_hour = 24;

    config.brightness_control_enabled = false;
    config.brightness_sensor_max = 900;
    config.brightness_sensor_min = 100;
    config.display_active_start_hour = 0;
    config.display_active_end_hour = 24;
    config.display_active_start_hour_2 = 0;
    config.display_active_end_hour_2 = 0;
    config.display_holiday_active_start_hour = 0;
    config.display_holiday_active_end_hour = 24;
    config.display_holiday_active_start_hour_2 = 0;
    config.display_holiday_active_end_hour_2 = 0;

    config.light_filter_samples = 8;
    config.light_sensor_resolution_bits = 10;
    
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