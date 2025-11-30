#include "button_handler.h"
#include "config.h"
#include "hardware.h"

bool printEnabled = false;

static uint8_t buttonBounceCount = 0;
static bool buttonStableState = HIGH;
static uint32_t buttonPressStartTime = 0;
static bool longPressHandled = false;
static bool veryLongPressHandled = false;

uint8_t CheckButton() {
  bool currentState = digitalRead(ENC_BTN);
  uint32_t currentTime = millis();
  uint8_t result = BUTTON_NONE;

  // Обработка антидребезга
  if (currentState != buttonStableState) {
    buttonBounceCount++;
    if (buttonBounceCount >= 3) {  // Стабильное состояние
      buttonStableState = currentState;
      buttonBounceCount = 0;

      if (buttonStableState == LOW) {  // Кнопка нажата
        buttonPressStartTime = currentTime;
        longPressHandled = false;
        veryLongPressHandled = false;
        result = BUTTON_PRESSED;
      }
    }
  } else {
    buttonBounceCount = 0;
  }

  // Проверка длинных нажатий (только если кнопка удерживается)
  if (buttonStableState == LOW) {
    uint32_t pressDuration = currentTime - buttonPressStartTime;

    if (!longPressHandled && pressDuration >= 1000) {  // 1 секунда
      longPressHandled = true;
      result = BUTTON_LONG;
    } 
    else if (!veryLongPressHandled && pressDuration >= 3000) {  // 5 секунд
      veryLongPressHandled = true;
      result = BUTTON_VERY_LONG;
    }
  }

  return result;
}

bool isDigit(char c) {
    return c >= '0' && c <= '9';
}