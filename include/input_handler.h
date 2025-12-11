#pragma once
#include <Arduino.h>
#include "config.h"

// Константы кнопки (оставляем из config.h)
// BUTTON_NONE, BUTTON_PRESSED, BUTTON_LONG, BUTTON_VERY_LONG

// Инициализация
void initInputHandler();

// Обработка ввода
uint8_t checkButton();              // Проверка кнопки (старая CheckButton)
void handleEncoderRotation();       // Обработка поворота энкодера
void processAllInputs();            // Обработка всех входов (вызывается в loop)

// Вспомогательные
bool isDigit(char c);               // Оставляем

// Геттеры для состояния
int32_t getEncoderPosition();       // Текущая позиция энкодера
int32_t getEncoderDelta();          // Изменение с последней проверки
void resetEncoderPosition();        // Сброс позиции

// Коллбэки (можно будет добавить позже)
typedef void (*ButtonCallback)(uint8_t buttonEvent);
typedef void (*EncoderCallback)(int32_t delta, int32_t position);

void setButtonCallback(ButtonCallback callback);
void setEncoderCallback(EncoderCallback callback);