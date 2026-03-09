#pragma once

#include <Arduino.h>
#include "config.h"

struct PlatformCapabilities {
    bool display_enabled;
    bool display_supports_nixie_tuning;
    bool sound_enabled;
    bool alarm_enabled;

    bool controls_enabled;
    bool button_enabled;
    bool encoder_enabled;
    bool display_navigation_enabled;
};

void platformRefreshCapabilities();
const PlatformCapabilities& platformGetCapabilities();

const char* platformClockTypeName(ClockType type);
const char* platformUiControlModeName(UiControlMode mode);
