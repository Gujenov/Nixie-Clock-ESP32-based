#pragma once

#include <Arduino.h>

void bleTerminalEnable();
void bleTerminalDisable();
bool bleTerminalIsEnabled();

void bleTerminalProcess();
bool bleTerminalHasCommand();
String bleTerminalReadCommand();

void bleTerminalLog(const String &message);
