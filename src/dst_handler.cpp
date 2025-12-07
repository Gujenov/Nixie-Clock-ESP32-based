#include "dst_handler.h"
#include "config.h"

const char* DST_PRESETS[] = {
    // Европа
    "EU", "CET-1CEST-2,M3.5.0/02:00:00,M10.5.0/03:00:00", "Европа (кроме Украины, Польши, UK)",
    "UK", "GMT0BST,M3.5.0/01:00:00,M10.5.0/02:00:00",     "Великобритания",
    "UA", "EET-2EEST-3,M3.5.0/03:00:00,M10.5.0/04:00:00", "Украина",
    "PL", "CET-1CEST-2,M3.5.0/02:00:00,M10.5.0/03:00:00", "Польша",

    // Америка
    "US", "EST5EDT,M3.2.0/02:00:00,M11.1.0/02:00:00",     "США/Канада",
    "MX", "CST6CDT,M4.1.0/02:00:00,M10.5.0/02:00:00",     "Мексика",
    "BR", "BRT3BRST,M11.1.0/00:00:00,M2.3.0/00:00:00",    "Бразилия (часть регионов)",

    // Ближний Восток
    "IL", "IST-2IDT-3,M3.4.4/02:00:00,M10.5.0/02:00:00",  "Израиль",
    "LB", "EET-2EEST-3,M3.5.0/00:00:00,M10.5.0/00:00:00", "Ливан",

    // Океания
    "AU", "AEST-10AEDT-11,M10.1.0/02:00:00,M4.1.0/03:00:00", "Австралия (NSW/VIC/ACT/TAS)",
    "NZ", "NZST-12NZDT-13,M9.5.0/02:00:00,M4.1.0/03:00:00",  "Новая Зеландия",

    // Без DST
    "NONE", "UTC0", "Без DST (чистый UTC)",
    nullptr
};

bool setDstPreset(uint8_t index) {
    // Проверяем границы массива (учитываем, что теперь каждый пресет занимает 3 элемента)
    int max_presets = (sizeof(DST_PRESETS)/sizeof(DST_PRESETS[0])) / 3;
    
    if (index >= max_presets) {
        Serial.printf("Ошибка: допустимые значения 0-%d\n", max_presets-1);
        return false;
    }
    
        config.time_config.dst_preset_index = index;
        strlcpy(config.time_config.dst_rule, DST_PRESETS[index * 3 + 1], sizeof(config.time_config.dst_rule));
    saveConfig();
    Serial.printf("Установлен DST пресет: %s (%s)\n", 
                 DST_PRESETS[index * 3], 
                 DST_PRESETS[index * 3 + 2]);
    return true;
}

void printDstPresets() {
    Serial.println("Доступные DST пресеты:");
    for (int i = 0; DST_PRESETS[i] != nullptr; i += 3) {
        // Форматируем правило для читаемости
        String rule = String(DST_PRESETS[i+1]);
        rule.replace(",", ", "); // Добавляем пробелы после запятых
        
        Serial.printf("%s: (%d) %s - %s\n",
            DST_PRESETS[i],      // Код страны
            i/3,                 // Номер пресета
            rule.c_str(),        // Форматированное правило
            DST_PRESETS[i+2]     // Описание
        );
    }
}

bool setDstPresetByName(const String& name) {
    for (int i = 0; DST_PRESETS[i] != nullptr; i += 3) {
        if (name.equalsIgnoreCase(DST_PRESETS[i])) {
                config.time_config.dst_preset_index = i/3;
                strlcpy(config.time_config.dst_rule, DST_PRESETS[i+1], sizeof(config.time_config.dst_rule));
            saveConfig();
            Serial.printf("Установлен DST: %s (%s)\n", DST_PRESETS[i], DST_PRESETS[i+2]);
            return true;
        }
    }
    Serial.println("Ошибка: пресет не найден. Доступные варианты:");
    printDstPresets();
    return false;
}