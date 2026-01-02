#include "command_handler.h"
#include "config.h"
#include "time_utils.h"
#include "alarm_handler.h"
#include "input_handler.h"
#include "hardware.h"

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
    printEnabled=false;
    printHelp();
  }
  else if(command.equals("info")) {
    printSystemInfo();
    printESP32Info();
  }
  else if (command.equals("settings")) {
    printEnabled=false;
    printSettings();
  }
  else if(command.equals("time") || command.equals("T")) {
    printTime();
  }
  else if(command.equals("WF")){ 
    Serial.printf("\nWiFi SSID: %s", config.wifi_ssid);
    Serial.printf("\nNTP сервер: %s", config.ntp_server);
  }
  /*else if(command.equals("timeinfo") || command.equals("ti")) {
    // Полная информация о времени
    Serial.print("\n\n=== Current Time Information ===");
    time_t currentTime = getCurrentUTCTime();
    
    // UTC время
    struct tm* utc_tm = gmtime(&currentTime);
    char buffer[32];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", utc_tm);
    Serial.print("UTC:    ");
    Serial.print(buffer);
    Serial.print(" (");
    Serial.print(currentTime);
    Serial.print("\n)");
    
    // Локальное время
    struct tm* local_tm = localtime(&currentTime);
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S %Z", local_tm);
    Serial.print("Local:  ");
    Serial.print("\n");
    Serial.print(buffer);
    
    // Информация об источнике
    Serial.print("\nSource: ");
    if (currentTimeSource == EXTERNAL_DS3231 && ds3231_available) {
        Serial.print("\nDS3231 RTC (available)");
    } else if (currentTimeSource == INTERNAL_RTC) {
        Serial.print("\nESP32 Internal RTC");
    } else {
        Serial.print("\nUnknown");
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
    Serial.print("\n");
  }*/
  else if(command.equals("timesource") || command.equals("TS")) {
    // Информация об источнике времени
    Serial.print("Current time source: ");
    if (currentTimeSource == EXTERNAL_DS3231) {
        Serial.print("DS3231 (External RTC)");
    } else if (currentTimeSource == INTERNAL_RTC) {
        Serial.print("\nESP32 Internal RTC");
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
      Serial.print("\nSSID обновлен");
    } else {
      Serial.print("\nОшибка: недопустимая длина SSID");
    }
  }
  else if(command.startsWith("set pass ")) {
    String pass = command.substring(9);
    if(pass.length() > 0 && pass.length() < sizeof(config.wifi_pass)) {
      pass.toCharArray(config.wifi_pass, sizeof(config.wifi_pass));
      saveConfig();
      Serial.print("\nПароль обновлен");
    } else {
        Serial.print("\nОшибка: недопустимая длина пароля");
    }
  }
  else if (command.equals("temp")) {
        printDS3231Temperature();
  }
  else if(command.startsWith("set ntp ")) {
    String ntp = command.substring(8);
    if(ntp.length() > 0 && ntp.length() < sizeof(config.ntp_server)) {
        updateNTPServer(ntp.c_str());  // Используем новую функцию
        Serial.print("\nNTP сервер обновлен");
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
      Serial.print("\n");
      Serial.print(tz);
    } else {
        Serial.print("\nОшибка: недопустимое значение часового пояса (-12..14)");
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
        Serial.print("\nПрименяем новые настройки времени...");
        // Устанавливаем часовой пояс с новыми настройками DST
        setTimeZone(config.time_config.timezone_offset, 
                   config.time_config.dst_enabled, 
                   config.time_config.dst_preset_index);
        
        // Пробуем синхронизировать время
        syncTime();
        
        Serial.printf("\nТекущий DST: %s (%s)", 
               DST_PRESETS[config.time_config.dst_preset_index * 3], 
               DST_PRESETS[config.time_config.dst_preset_index * 3 + 2]);
    } else {
        Serial.print("\nОшибка: укажите номер (0-4) или код страны (EU, US, UA и др.)");
        Serial.print("\nИспользуйте 'show dst' для списка доступных пресетов");
    }
  }
  else if (command.equals("show dst")) {
    Serial.print("\nДоступные DST пресеты:");
    for (int i = 0; DST_PRESETS[i] != nullptr; i += 2) {
        Serial.printf("\n%d: %s -> %s", i/2, DST_PRESETS[i], DST_PRESETS[i+1]);
    }
  }*/
  else if(command.startsWith("set sn ")) {
    String sn = command.substring(7);
    if(sn.length() == 11) {
      sn.toCharArray(config.serial_number, sizeof(config.serial_number));
      saveConfig();
      Serial.print("\nСерийный номер обновлен");
    } else {
      Serial.print("\nОшибка: серийный номер должен быть 11 символов");
    }
  }
  else if(command.equals("show config") || command.equals("SC")) {
    Serial.print("\n=== Текущие настройки ===");
    Serial.printf("\nSSID: %s", config.wifi_ssid);
    Serial.printf("\nПароль: %s", config.wifi_pass);
    Serial.printf("NTP сервер: %s\n", config.ntp_server);
  //Serial.printf("\nЧасовой пояс: UTC%+d", config.time_config.timezone_offset);
  //  Serial.printf("\nDST enabled: %s", config.time_config.dst_enabled ? "Yes" : "No");
  //  Serial.printf("\nDST preset: %d", config.time_config.dst_preset_index);
    Serial.printf("\nСерийный номер: %s", config.serial_number);
    Serial.printf("\nБудильник 1: %s %02d:%02d", 
        config.alarm1.enabled ? "Вкл" : "Выкл",
        config.alarm1.hour, config.alarm1.minute);
    Serial.printf("\nБудильник 2: %s %02d:%02d", 
        config.alarm2.enabled ? "Вкл" : "Выкл",
        config.alarm2.hour, config.alarm2.minute);
    Serial.printf("\nAuto sync: %s", config.time_config.auto_sync_enabled ? "Enabled" : "Disabled");
    Serial.printf("\nSync interval: %d hours", config.time_config.sync_interval_hours);
    Serial.printf("\nTime source: %s", 
        currentTimeSource == EXTERNAL_DS3231 ? "DS3231" : "Internal RTC");
    Serial.printf("\nDS3231 available: %s", ds3231_available ? "Yes" : "No");
  }
  else if(command.equals("reset config")|| command.equals("RC")) {
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
  else if (command.equals("syncstatus")|| command.equals("SS")) {
    Serial.print("Last NTP sync: ");
    if (config.time_config.last_ntp_sync > 0) {
      time_t syncTime = (time_t)config.time_config.last_ntp_sync;
      struct tm* timeinfo = localtime(&syncTime);
      char buffer[32];
      strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
      Serial.print("\n");
      Serial.print(buffer);
      
      time_t now = getCurrentUTCTime();
      time_t diff = now - config.time_config.last_ntp_sync;
      Serial.print("Time since last sync: ");
      Serial.print(diff);
      Serial.print("\n seconds");
    } else {
      Serial.print("\nNever");
    }
  }
  else if (command.startsWith("out")) { 
    printEnabled=true;
  }
  else {
    Serial.print("\nНеизвестная команда. Введите 'help' для списка команд");
  }
}

void printHelp() {
  Serial.print("\n\n=== Доступные команды ===");
  Serial.print("\n\n-=ВНИМАНИЕ! До подачи команды ''out'' выдача времени остановлена=-\n");

  Serial.print("\n\ntime, T        - Текущее время");
  Serial.print("\nWF             - Настройки WI-FI и NTP");
  Serial.print("\ntimesource, TS - Источник текущего времени");
  Serial.print("\nsync, ntp      - Принудительная синхронизация с NTP");
  Serial.print("\nsyncstatus, SS - Статус последней синхронизации");
  Serial.print("\nshow config, SC - Показать текущие настройки");
  Serial.print("\nreset config   - сбросить настройки к значениям по умолчанию");
  Serial.print("\ntemp           - показать температуру DS3231");
  Serial.print("\n\nhelp, ?         - показать это сообщение");
  Serial.print("\ninfo           - Информация о системе");

  Serial.print("\n\nsettings        - Вход в режим настройки");
  Serial.print("\nout            - Выход из режима настройки");

  Serial.print("\n==========================\n");
}

void printSystemInfo() {
  Serial.print("\n\n=== Системная информация ===");
  Serial.printf("\nВерсия ПО: %s", FIRMWARE_VERSION);
  Serial.printf("\nСерийный номер устройства: %s", config.serial_number);
//  Serial.printf("Часовой пояс: UTC%+d\n", config.time_config.timezone_offset);
  Serial.printf("\nИсточник времени: %s", 
               currentTimeSource == EXTERNAL_DS3231 ? "DS3231" : "Внутренний RTC");
  
  printDS3231Temperature(); // Вывод температуры DS3231, если доступно             
  Serial.printf("\nWiFi SSID: %s\n", config.wifi_ssid);
  Serial.printf("NTP сервер: %s\n", config.ntp_server);
  
  printDS3231Temperature(); // Вывод температуры DS3231, если доступно
  Serial.print("\n==========================");
}

void printSettings() {
  Serial.print("\n\n=== Доступные настройки ===");
  Serial.print("\n\n-=ВНИМАНИЕ! До подачи команды ''out'' выдача времени остановлена=-\n");

  Serial.print("\n\n========TIME=============");
  Serial.print("\nset time, ST [HH:MM:SS] - установить время вручную");
  Serial.print("\nset date, SD [DD.MM.YYYY] - установить дату вручную");
  Serial.print("\nset tz [OFFSET] - изменить часовой пояс (-12..14)");
  Serial.print("\nshow dst - показать доступные DST пресеты");
  Serial.print("\nset dst [0-4] - выбрать DST пресет");
  Serial.print("\nset al 1 [HH:MM] - установить время будильника 1");
  Serial.print("\nset al 2 [HH:MM] - установить время будильника 2");
  
  Serial.print("\n\n========WIFI=============");
  Serial.print("\nset ssid [SSID] - изменить SSID WiFi");
  Serial.print("\nset pass [PASSWORD] - изменить пароль WiFi");
  Serial.print("\nset ntp [SERVER] - изменить NTP сервер");
  
  Serial.print("\n\n========DEVICE==========");
  Serial.print("\nset sn [SERIAL] - изменить серийный номер (11 символов)");
  Serial.print("\nreset config - сбросить настройки к значениям по умолчанию");
  Serial.print("\n==========================\n"); 
  Serial.print("\n out - выход из меню настроек"); 
}

void printESP32Info() {
    Serial.print("\n\n=== ESP32-S3 Информация ===");
    
    // Информация о чипе
Serial.printf("\nESP-ROM: %s", ESP.getChipModel());
    Serial.printf("\nCPU Частота: %d MHz", ESP.getCpuFreqMHz());

    Serial.printf("\nIDF версия: %s", esp_get_idf_version());
    Serial.printf("\nCores: %d", ESP.getChipCores());
    Serial.printf("\nRevision: %d", ESP.getChipRevision());

    // Информация о флеш памяти
    Serial.printf("\nFlash Size: %d MB", ESP.getFlashChipSize() / (1024 * 1024));
    Serial.printf("\nFlash usage: %.1f%%", 
              (ESP.getSketchSize() * 100.0) / ESP.getFlashChipSize());
    
    
    Serial.println("==========================\n");
}
