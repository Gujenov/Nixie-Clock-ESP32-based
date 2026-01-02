#include "command_handler.h"
#include "config.h"
#include "time_utils.h"
#include "alarm_handler.h"
#include "input_handler.h"

// Объявляем внешние переменные из time_utils.cpp
extern WiFiUDP ntpUDP;
extern NTPClient *timeClient;
extern HardwareSource currentTimeSource;  // Из hardware.h
extern bool ds3231_available;             // Из hardware.h
extern bool printEnabled;

void printHelp();
void printSystemInfo();
void printESP32Info();
void printSettings();


void handleSerialCommands() {
  if(!Serial.available()) return;
  
  String command = Serial.readStringUntil('\n');
  command.trim();
  
  if(command.equals("help") || command.equals("?")) {
    printHelp();
  }
  else if(command.equals("info")) {
    printSystemInfo();
    printESP32Info();
  }
  else if (command.equals("setings")) {
    printEnabled=false;
    printSettings();
  }
  else if(command.equals("time") || command.equals("T")) {
    printTime();
  }
  else if(command.equals("WF")){ 
    Serial.printf("WiFi SSID: %s\n", config.wifi_ssid);
    Serial.printf("NTP сервер: %s\n", config.ntp_server);
  }
  /*else if(command.equals("timeinfo") || command.equals("ti")) {
    // Полная информация о времени
    Serial.println("\n=== Current Time Information ===");
    time_t currentTime = getCurrentUTCTime();
    
    // UTC время
    struct tm* utc_tm = gmtime(&currentTime);
    char buffer[32];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", utc_tm);
    Serial.print("UTC:    ");
    Serial.print(buffer);
    Serial.print(" (");
    Serial.print(currentTime);
    Serial.println(")");
    
    // Локальное время
    struct tm* local_tm = localtime(&currentTime);
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S %Z", local_tm);
    Serial.print("Local:  ");
    Serial.println(buffer);
    
    // Информация об источнике
    Serial.print("\nSource: ");
    if (currentTimeSource == EXTERNAL_DS3231 && ds3231_available) {
        Serial.println("DS3231 RTC (available)");
    } else if (currentTimeSource == INTERNAL_RTC) {
        Serial.println("ESP32 Internal RTC");
    } else {
        Serial.println("Unknown");
    }
    
    // Часовой пояс
    Serial.print("Timezone: UTC");
    if (config.time_config.timezone_offset >= 0) {
        Serial.print("+");
    }
    Serial.print(config.time_config.timezone_offset);
    if (config.time_config.dst_enabled) {
        Serial.print(" (DST enabled)");
    }
    Serial.println();
  }*/
  else if(command.equals("timesource") || command.equals("ts")) {
    // Информация об источнике времени
    Serial.print("Current time source: ");
    if (currentTimeSource == EXTERNAL_DS3231) {
        Serial.print("DS3231 (External RTC)");
    } else if (currentTimeSource == INTERNAL_RTC) {
        Serial.println("ESP32 Internal RTC");
    } 
  }
  else if(command.equals("sync") || command.equals("ntp")) {
    // Синхронизация через существующую функцию
    syncTime();
  }
  else if(command.startsWith("set ssid ")) {
    String ssid = command.substring(9);
    if(ssid.length() > 0 && ssid.length() < sizeof(config.wifi_ssid)) {
      ssid.toCharArray(config.wifi_ssid, sizeof(config.wifi_ssid));
      saveConfig();
      Serial.println("SSID обновлен");
    } else {
      Serial.println("Ошибка: недопустимая длина SSID");
    }
  }
  else if(command.startsWith("set pass ")) {
    String pass = command.substring(9);
    if(pass.length() > 0 && pass.length() < sizeof(config.wifi_pass)) {
      pass.toCharArray(config.wifi_pass, sizeof(config.wifi_pass));
      saveConfig();
      Serial.println("Пароль обновлен");
    } else {
      Serial.println("Ошибка: недопустимая длина пароля");
    }
  }
  else if(command.startsWith("set ntp ")) {
    String ntp = command.substring(8);
    if(ntp.length() > 0 && ntp.length() < sizeof(config.ntp_server)) {
        updateNTPServer(ntp.c_str());  // Используем новую функцию
        Serial.println("NTP сервер обновлен");
    }
}
  /*else if(command.startsWith("set tz ")) {
    int8_t tz = command.substring(7).toInt();
    if(tz >= -12 && tz <= 14) {
      config.time_config.timezone_offset = tz;
      saveConfig();
      // Устанавливаем часовой пояс через существующую функцию
      setTimeZone(config.time_config.timezone_offset, 
                  config.time_config.dst_enabled, 
                  config.time_config.dst_preset_index);
      Serial.print("Часовой пояс обновлен: UTC");
      if (tz >= 0) Serial.print("+");
      Serial.println(tz);
    } else {
        Serial.println("Ошибка: недопустимое значение часового пояса (-12..14)");
    }
  } 
  else if (command.startsWith("set dst ")) {
    String arg = command.substring(8);
    bool success = false;
    
    // Проверяем, является ли аргумент числом
    if (isDigit(arg.charAt(0))) {
        uint8_t idx = arg.toInt();
        success = setDstPreset(idx);  // Старый метод по индексу
    } else {
        success = setDstPresetByName(arg);  // Новый метод по коду страны
    }

    if (success) {
        Serial.println("Применяем новые настройки времени...");
        // Устанавливаем часовой пояс с новыми настройками DST
        setTimeZone(config.time_config.timezone_offset, 
                   config.time_config.dst_enabled, 
                   config.time_config.dst_preset_index);
        
        // Пробуем синхронизировать время
        syncTime();
        
        Serial.printf("Текущий DST: %s (%s)\n", 
               DST_PRESETS[config.time_config.dst_preset_index * 3], 
               DST_PRESETS[config.time_config.dst_preset_index * 3 + 2]);
    } else {
        Serial.println("Ошибка: укажите номер (0-4) или код страны (EU, US, UA и др.)");
        Serial.println("Используйте 'show dst' для списка доступных пресетов");
    }
  }
  else if (command.equals("show dst")) {
    Serial.println("Доступные DST пресеты:");
    for (int i = 0; DST_PRESETS[i] != nullptr; i += 2) {
        Serial.printf("%d: %s -> %s\n", i/2, DST_PRESETS[i], DST_PRESETS[i+1]);
    }
  }*/
  else if(command.startsWith("set sn ")) {
    String sn = command.substring(7);
    if(sn.length() == 11) {
      sn.toCharArray(config.serial_number, sizeof(config.serial_number));
      saveConfig();
      Serial.println("Серийный номер обновлен");
    } else {
      Serial.println("Ошибка: серийный номер должен быть 11 символов");
    }
  }
  else if(command.equals("show config") || command.equals("sc")) {
    Serial.println("\n=== Текущие настройки ===");
    Serial.printf("SSID: %s\n", config.wifi_ssid);
    Serial.printf("Пароль: %s\n", config.wifi_pass);
    Serial.printf("NTP сервер: %s\n", config.ntp_server);
  //Serial.printf("Часовой пояс: UTC%+d\n", config.time_config.timezone_offset);
  //  Serial.printf("DST enabled: %s\n", config.time_config.dst_enabled ? "Yes" : "No");
  //  Serial.printf("DST preset: %d\n", config.time_config.dst_preset_index);
    Serial.printf("Серийный номер: %s\n", config.serial_number);
    Serial.printf("Будильник 1: %s %02d:%02d\n", 
        config.alarm1.enabled ? "Вкл" : "Выкл",
        config.alarm1.hour, config.alarm1.minute);
    Serial.printf("Будильник 2: %s %02d:%02d\n", 
        config.alarm2.enabled ? "Вкл" : "Выкл",
        config.alarm2.hour, config.alarm2.minute);
    Serial.printf("Auto sync: %s\n", config.time_config.auto_sync_enabled ? "Enabled" : "Disabled");
    Serial.printf("Sync interval: %d hours\n", config.time_config.sync_interval_hours);
    Serial.printf("Time source: %s\n", 
        currentTimeSource == EXTERNAL_DS3231 ? "DS3231" : "Internal RTC");
    Serial.printf("DS3231 available: %s\n", ds3231_available ? "Yes" : "No");
  }
  else if(command.equals("reset config")|| command.equals("rc")) {
    preferences.begin("config", false);
    preferences.clear();
    preferences.end();
    setDefaultConfig();
    delete timeClient;
    timeClient = new NTPClient(ntpUDP, config.ntp_server, 0);
  }
  else if (command.startsWith("set time ") || command.startsWith("ST ")) {
    String timeStr;
    
    if (command.startsWith("set time ")) {
        timeStr = command.substring(9);  // "set time " = 9 символов
    } else {
        timeStr = command.substring(3);  // "ST " = 3 символа
    }
    
    //timeStr.trim(); // Убираем лишние пробелы
    setManualTime(timeStr);
}
else if (command.startsWith("set date ") || command.startsWith("SD ")) {
    String dateStr;
    
    if (command.startsWith("set date ")) {
        dateStr = command.substring(9);  // "set date " = 9 символов
    } else {
        dateStr = command.substring(3);  // "SD " = 3 символа  
    }
    
    //dateStr.trim();
    setManualDate(dateStr);
}
  else if (command.startsWith("set al 1 ")) {
    String timeStr = command.substring(8);
    setAlarm(1, timeStr);
  } 
  else if (command.startsWith("set al 2 ")) {
    String timeStr = command.substring(8);
    setAlarm(2, timeStr);
  }
  else if (command.equals("syncstatus")|| command.equals("ss")) {
    Serial.print("Last NTP sync: ");
    if (config.time_config.last_ntp_sync > 0) {
      time_t syncTime = (time_t)config.time_config.last_ntp_sync;
      struct tm* timeinfo = localtime(&syncTime);
      char buffer[32];
      strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
      Serial.println(buffer);
      
      time_t now = getCurrentUTCTime();
      time_t diff = now - config.time_config.last_ntp_sync;
      Serial.print("Time since last sync: ");
      Serial.print(diff);
      Serial.println(" seconds");
    } else {
      Serial.println("Never");
    }
  }
  else if (command.startsWith("out")) { 
    printEnabled=true;
  }
  else {
    Serial.println("Неизвестная команда. Введите 'help' для списка команд");
  }
}

void printHelp() {
  Serial.println("\n=== Доступные команды ===");

  Serial.println("\ntime, T        - Текущее время");
  Serial.println("WF             - Настройки WI-FI и NTP");
  Serial.println("timesource, ts - Источник текущего времени");
  Serial.println("sync, ntp      - Принудительная синхронизация с NTP");
  Serial.println("syncstatus, ss - Статус последней синхронизации");
  Serial.println("show config,sc - Показать текущие настройки");
  Serial.println("reset config   - сбросить настройки к значениям по умолчанию");

  Serial.println("\nhelp, ?         - показать это сообщение");
  Serial.println("info           - Информация о системе");

  Serial.println("\nsetings        - Вход в режим настройки");
  Serial.println("out            - Выход из режима настройки");

  Serial.println("==========================\n");
}

void printSystemInfo() {
  Serial.println("\n=== Системная информация ===");
  Serial.printf("Версия ПО: %s\n", FIRMWARE_VERSION);
  Serial.printf("Серийный номер устройства: %s\n", config.serial_number);
//  Serial.printf("Часовой пояс: UTC%+d\n", config.time_config.timezone_offset);
  Serial.printf("Источник времени: %s\n", 
               currentTimeSource == EXTERNAL_DS3231 ? "DS3231" : "Внутренний RTC");
   
  Serial.printf("\nWiFi SSID: %s\n", config.wifi_ssid);
  Serial.printf("NTP сервер: %s\n", config.ntp_server);
  
  Serial.println("==========================");
}

void printSettings() {
  Serial.println("\n=== Доступные настройки ===");
  Serial.println("\n-=ВНИМАНИЕ! До подачи команды ''out'' выдача времени остановлена=-\n");

  Serial.println("\n========TIME=============");
  Serial.println("set time, ST [HH:MM:SS] - установить время вручную");
  Serial.println("set date, SD [DD.MM.YYYY] - установить дату вручную");
  Serial.println("set tz [OFFSET] - изменить часовой пояс (-12..14)");
  Serial.println("show dst - показать доступные DST пресеты");
  Serial.println("set dst [0-4] - выбрать DST пресет");
  Serial.println("set al 1 [HH:MM] - установить время будильника 1");
  Serial.println("set al 2 [HH:MM] - установить время будильника 2");
  
  Serial.println("\n========WIFI=============");
  Serial.println("set ssid [SSID] - изменить SSID WiFi");
  Serial.println("set pass [PASSWORD] - изменить пароль WiFi");
  Serial.println("set ntp [SERVER] - изменить NTP сервер");
  
  Serial.println("\n========DEVICE==========");
  Serial.println("set sn [SERIAL] - изменить серийный номер (11 символов)");
  Serial.println("reset config - сбросить настройки к значениям по умолчанию");
  Serial.println("==========================\n"); 
  Serial.println("\n out - выход из меню настроек"); 
}

void printESP32Info() {
    Serial.println("\n=== ESP32-S3 Информация ===");
    
    // Информация о чипе
    Serial.printf("ESP-ROM: %s\n", ESP.getChipModel());
    Serial.printf("CPU Частота: %d MHz\n", ESP.getCpuFreqMHz());

    Serial.printf("IDF версия: %s\n", esp_get_idf_version());
    Serial.printf("Cores: %d\n", ESP.getChipCores());
    Serial.printf("Revision: %d\n", ESP.getChipRevision());
    
    // Информация о флеш памяти
    Serial.printf("Flash Size: %d MB\n", ESP.getFlashChipSize() / (1024 * 1024));
    Serial.printf("Flash usage: %.1f%%\n", 
              (ESP.getSketchSize() * 100.0) / ESP.getFlashChipSize());
    
    
    Serial.println("==========================\n");
}
