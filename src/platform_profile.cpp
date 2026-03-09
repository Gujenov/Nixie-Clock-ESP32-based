#include "platform_profile.h"
#include "config.h"

static PlatformCapabilities g_caps = {
    true,  // display_enabled
    true,  // display_supports_nixie_tuning
    true,  // sound_enabled
    true,  // alarm_enabled
    true,  // controls_enabled
    true,  // button_enabled
    true,  // encoder_enabled
    true   // display_navigation_enabled
};

const char* platformClockTypeName(ClockType type) {
    switch (type) {
        case CLOCK_TYPE_NIXIE: return "Nixie";
        case CLOCK_TYPE_NIXIE_HAND: return "Nixie hand";
        case CLOCK_TYPE_CYCLOTRON: return "Cyclotron";
        case CLOCK_TYPE_VERTICAL: return "Vertical";
        case CLOCK_TYPE_MECH_2: return "Mech 2";
        case CLOCK_TYPE_MECH_PEND: return "Mech pend";
        default: return "Unknown";
    }
}

const char* platformUiControlModeName(UiControlMode mode) {
    switch (mode) {
        case UI_CONTROL_BUTTON_ONLY: return "Кнопка";
        case UI_CONTROL_ENCODER_ONLY: return "Энкодер";
        case UI_CONTROL_ENCODER_BUTTON: return "Энкодер + кнопка";
        default: return "Unknown";
    }
}

void platformRefreshCapabilities() {
    // Базовые модули
    g_caps.display_enabled = true;
    g_caps.sound_enabled = config.audio_module_enabled;
    g_caps.alarm_enabled = config.audio_module_enabled;

    // Управление (переключаемое инженерным меню)
    g_caps.button_enabled =
        (config.ui_control_mode == UI_CONTROL_BUTTON_ONLY) ||
        (config.ui_control_mode == UI_CONTROL_ENCODER_BUTTON);

    g_caps.encoder_enabled =
        (config.ui_control_mode == UI_CONTROL_ENCODER_ONLY) ||
        (config.ui_control_mode == UI_CONTROL_ENCODER_BUTTON);
    g_caps.controls_enabled = g_caps.button_enabled || g_caps.encoder_enabled;

    // Возможности, зависящие от типа часов
    g_caps.display_supports_nixie_tuning = (config.clock_type == CLOCK_TYPE_NIXIE);

    // Навигация по экранам актуальна только для Nixie 6
    g_caps.display_navigation_enabled =
        (config.clock_type == CLOCK_TYPE_NIXIE) &&
        (config.clock_digits == 6) &&
        g_caps.controls_enabled;
}

const PlatformCapabilities& platformGetCapabilities() {
    return g_caps;
}
