#include "command_handler.h"
#include "config.h"
#include "time_utils.h"
#include "dst_handler.h"
#include "alarm_handler.h"
#include "button_handler.h"

void handleSerialCommands() {
  if(!Serial.available()) return;
  
  String command = Serial.readStringUntil('\n');
  command.trim();
  
  if(command.equals("help")) {
    printHelp();
  }
  else if(command.equals("time")) {
    printTime();
  }
  else if(command.equals("sync")) {
    syncTime();
  }
  else if (command.startsWith("setup")) {
  printEnabled=false;
  printSettings();
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
        config.timezone_offset = tz;
        saveConfig();
        setTimeZone(config.timezone_offset, config.dst_enabled, config.dst_preset_index);
        Serial.println("Часовой пояс обновлен");
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
        syncTime();
        Serial.printf("Текущий DST: %s (%s)\n", 
                     DST_PRESETS[config.dst_preset_index * 3], 
                     DST_PRESETS[config.dst_preset_index * 3 + 2]);
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
  else if(command.equals("show config")) {
    Serial.println("\n=== Текущие настройки ===");
    Serial.printf("SSID: %s\n", config.wifi_ssid);
    Serial.printf("Пароль: %s\n", config.wifi_pass);
    Serial.printf("NTP сервер: %s\n", config.ntp_server);
    Serial.printf("Часовой пояс: UTC%+d\n", config.timezone_offset);
    Serial.printf("Серийный номер: %s\n", config.serial_number);
    Serial.printf("Будильник 1: %s %02d:%02d\n", 
        config.alarm1.enabled ? "Вкл" : "Выкл",
        config.alarm1.hour, config.alarm1.minute);
    Serial.printf("Будильник 2: %s %02d:%02d\n", 
        config.alarm2.enabled ? "Вкл" : "Выкл",
        config.alarm2.hour, config.alarm2.minute);  
  }
  else if(command.equals("reset config")) {
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
  else if (command.startsWith("out")) {
  printHelp(); 
  printEnabled=true;
  }
  else {
    Serial.println("Неизвестная команда. Введите 'help' для списка команд");
  }
}

void printHelp() {
  Serial.println("\n=== Доступные команды ===");
  Serial.println("help - показать это сообщение");
  Serial.println("time - показать текущее время");
  Serial.println("sync - синхронизировать время");
  Serial.println("show config - показать текущие настройки");
  Serial.println("setup - отобразить команды доступных настроек");
  Serial.println("reset config - сбросить настройки к значениям по умолчанию");
  Serial.println("==========================\n");
}

void printSystemInfo() {
  Serial.println("\n=== Системная информация ===");
  Serial.printf("Версия ПО: %s\n", FIRMWARE_VERSION);
  Serial.printf("IDF версия: %s\n", esp_get_idf_version());
  Serial.printf("CPU частота: %d MHz\n", ESP.getCpuFreqMHz());
  Serial.printf("Свободная память: %d байт\n", ESP.getFreeHeap());
  Serial.printf("Серийный номер: %s\n", config.serial_number);
  Serial.printf("WiFi SSID: %s\n", config.wifi_ssid);
  Serial.printf("NTP сервер: %s\n", config.ntp_server);
  Serial.printf("Часовой пояс: UTC%+d\n", config.timezone_offset);
  Serial.printf("Источник времени: %s\n", 
               currentTimeSource == EXTERNAL_DS3231 ? "DS3231" : "Внутренний RTC");
  Serial.println("==========================");
}

void printSettings() {
  Serial.println("\n=== Доступные настройки ===");
  Serial.println("\n-=ВНИМАНИЕ! До подачи команды ''out'' время в терминал не выдаётся=-\n");
  Serial.println("set time [HH:MM:SS] - установить время вручную");
  Serial.println("set date [DD.MM.YYYY] - установить дату вручную");
  Serial.println("set al 1 [HH:MM] - установить время будильника 1");
  Serial.println("set al 2 [HH:MM] - установить время будильника 2");
  Serial.println("set ssid [SSID] - изменить SSID WiFi");
  Serial.println("set pass [PASSWORD] - изменить пароль WiFi");
  Serial.println("set ntp [SERVER] - изменить NTP сервер");
  Serial.println("set tz [OFFSET] - изменить часовой пояс (-12..14)");
  Serial.println("set dst [0-4] - выбрать DST пресет");
  Serial.println("show dst - показать доступные DST пресеты");
  Serial.println("set sn [SERIAL] - изменить серийный номер (11 символов)");
  Serial.println("reset config - сбросить настройки к значениям по умолчанию");
  Serial.println("out - выход из меню настроек");
  Serial.println("==========================\n");  
}