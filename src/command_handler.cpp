#include "command_handler.h"
#include "config.h"
#include "time_utils.h"
#include "dst_handler.h"
#include "alarm_handler.h"
#include "button_handler.h"

// Объявляем внешние переменные из time_utils.cpp
extern WiFiUDP ntpUDP;
extern NTPClient *timeClient;
extern HardwareSource currentTimeSource;  // Из hardware.h
extern bool ds3231_available;             // Из hardware.h

void handleSerialCommands() {
  if(!Serial.available()) return;
  
  String command = Serial.readStringUntil('\n');
  command.trim();
  
  if(command.equals("help") || command.equals("?")) {
    printHelp();
  }
  else if(command.equals("info")) {
    printSystemInfo();
  }
  else if(command.equals("espinfo")) {
    printESP32Info();
  }
  else if (command.startsWith("setup")) {
    printEnabled=false;
    printSettings();
  }
  else if(command.equals("time") || command.equals("LT")) {
    // Локальное время (Local Time)
    Serial.println("=== Local Time ===");
    time_t currentTime = getCurrentTime();  // Используем существующую функцию
    struct tm* timeinfo = localtime(&currentTime);
    char buffer[32];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S %Z", timeinfo);
    Serial.println(buffer);
    Serial.print("(Timestamp: ");
    Serial.print(currentTime);
    Serial.println(")");
  }
  else if(command.equals("utc") || command.equals("UTC")) {
    // UTC время
    Serial.println("=== UTC Time ===");
    time_t utcTime = getCurrentTime();  // getCurrentTime уже возвращает UTC
    struct tm* timeinfo = gmtime(&utcTime);
    char buffer[32];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S UTC", timeinfo);
    Serial.println(buffer);
    Serial.print("(Timestamp: ");
    Serial.print(utcTime);
    Serial.println(")");
  }
  else if(command.equals("WF")){ 
    Serial.printf("WiFi SSID: %s\n", config.wifi_ssid);
    Serial.printf("NTP сервер: %s\n", config.ntp_server);
  }
  else if(command.equals("timeinfo") || command.equals("ti")) {
    // Полная информация о времени
    Serial.println("\n=== Current Time Information ===");
    time_t currentTime = getCurrentTime();
    
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
  }
  else if(command.equals("timesource") || command.equals("ts")) {
    // Информация об источнике времени
    Serial.print("Current time source: ");
    if (currentTimeSource == EXTERNAL_DS3231) {
        Serial.print("DS3231 (External RTC)");
        Serial.println(ds3231_available ? " [Available]" : " [Not available]");
    } else if (currentTimeSource == INTERNAL_RTC) {
        Serial.println("ESP32 Internal RTC");
    } else {
        Serial.println("Unknown");
    }
    
    Serial.print("External RTC available: ");
    Serial.println(ds3231_available ? "Yes" : "No");
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
      ntp.toCharArray(config.ntp_server, sizeof(config.ntp_server));
      saveConfig();
      delete timeClient;
      timeClient = new NTPClient(ntpUDP, config.ntp_server, 0);
      Serial.println("NTP сервер обновлен");
    } else {
      Serial.println("Ошибка: недопустимая длина адреса NTP сервера");
    }
  }
  else if(command.startsWith("set tz ")) {
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
  }
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
    Serial.printf("Часовой пояс: UTC%+d\n", config.time_config.timezone_offset);
    Serial.printf("DST enabled: %s\n", config.time_config.dst_enabled ? "Yes" : "No");
    Serial.printf("DST preset: %d\n", config.time_config.dst_preset_index);
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
    Serial.println("Конфигурация сброшена к значениям по умолчанию");
  }
  else if (command.startsWith("set time ")) {
    String timeStr = command.substring(9);
    setManualTime(timeStr);
  }
  else if (command.startsWith("set date ")) {
    String dateStr = command.substring(9);
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
      
      time_t now = getCurrentTime();
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
  else if (command.equals("debug")) {
    // Отладочная информация
    Serial.println("\n=== Debug Information ===");
    Serial.printf("Current time source: %s\n", 
        currentTimeSource == EXTERNAL_DS3231 ? "EXTERNAL_DS3231" : "INTERNAL_RTC");
    Serial.printf("DS3231 available: %s\n", ds3231_available ? "true" : "false");
    Serial.printf("WiFi SSID: %s\n", config.wifi_ssid);
    Serial.printf("NTP server: %s\n", config.ntp_server);
    Serial.printf("Timezone offset: %d\n", config.time_config.timezone_offset);
    Serial.printf("DST enabled: %s\n", config.time_config.dst_enabled ? "true" : "false");
    
    time_t now = getCurrentTime();
    struct tm* utc = gmtime(&now);
    char buffer[32];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S UTC", utc);
    Serial.printf("Current UTC: %s\n", buffer);
    
    struct tm* local = localtime(&now);
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S %Z", local);
    Serial.printf("Current local: %s\n", buffer);
    
    Serial.println("========================");
  }
  else {
    Serial.println("Неизвестная команда. Введите 'help' для списка команд");
  }
}

