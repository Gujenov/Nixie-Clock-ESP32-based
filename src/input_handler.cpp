#include "input_handler.h"
#include "hardware.h"
#include "time_utils.h"

bool printEnabled = true;

// Глобальные переменные для энкодера
static int32_t lastEncoderPos = 0;
static int32_t encoderDelta = 0;
static bool encoderInitialized = false;

// Коллбэки (пока не используем, но на будущее)
static ButtonCallback buttonCallback = nullptr;
static EncoderCallback encoderCallback = nullptr;

// Существующая логика кнопки (немного рефакторим)
static uint8_t buttonBounceCount = 0;
static bool buttonStableState = HIGH;
static uint32_t buttonPressStartTime = 0;
static bool longPressHandled = false;
static bool veryLongPressHandled = false;

// Инициализация
void initInputHandler() {
    // Энкодер уже инициализирован в setup(), просто сбрасываем состояние
    if (!encoderInitialized) {
        lastEncoderPos = encoder.getCount();
        encoderInitialized = true;
        Serial.print("\n[INPUT] Input handler initialized");
    }
}

// Проверка кнопки (переименовали CheckButton -> checkButton)
uint8_t checkButton() {
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
            } else {
                // Кнопка отпущена
                // Можно добавить обработку короткого отпускания
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
        else if (!veryLongPressHandled && pressDuration >= 3000) {  // 3 секунды
            veryLongPressHandled = true;
            result = BUTTON_VERY_LONG;
        }
    }

    // Вызов коллбэка если есть
    if (result != BUTTON_NONE && buttonCallback != nullptr) {
        buttonCallback(result);
    }

    return result;
}

// Обработка поворота энкодера
void handleEncoderRotation() {
    if (!encoderInitialized) {
        initInputHandler();
    }
    
    int32_t currentPos = encoder.getCount();
    
    if (currentPos != lastEncoderPos) {
        encoderDelta = currentPos - lastEncoderPos;
        lastEncoderPos = currentPos;
        
        // Фильтр от дребезга (только значительные изменения)
        if (abs(encoderDelta) >= 2) {
            Serial.printf("[ENC] Rotation: delta=%+d, total=%d\n", encoderDelta, currentPos);
            
            // Вызов коллбэка если есть
            if (encoderCallback != nullptr) {
                encoderCallback(encoderDelta, currentPos);
            }
            
            // Сбрасываем дельту после обработки
            encoderDelta = 0;
        }
    }
}

// Обработка всех входов (вызывать в loop)
void processAllInputs() {
    // 1. Проверяем кнопку
    uint8_t buttonEvent = checkButton();
    
    // Базовая обработка кнопки (если нет коллбэка)
    if (buttonEvent != BUTTON_NONE && buttonCallback == nullptr) {
        switch (buttonEvent) {
            case BUTTON_PRESSED:
                Serial.print("\n[BTN] Short press");
                break;
            case BUTTON_LONG:
                Serial.print("\n[BTN] Long press (1s)");
                break;
            case BUTTON_VERY_LONG:
                Serial.print("\n[BTN] Very long press (3s)");
                break;
        }
    }
    
    // 2. Проверяем энкодер
    handleEncoderRotation();
}

// Геттеры
int32_t getEncoderPosition() {
    return encoder.getCount();
}

int32_t getEncoderDelta() {
    return encoderDelta;
}

void resetEncoderPosition() {
    encoder.setCount(0);
    lastEncoderPos = 0;
    encoderDelta = 0;
}

// Коллбэки
void setButtonCallback(ButtonCallback callback) {
    buttonCallback = callback;
}

void setEncoderCallback(EncoderCallback callback) {
    encoderCallback = callback;
}

// Вспомогательная функция (оставляем)
bool isDigit(char c) {
    return c >= '0' && c <= '9';
}