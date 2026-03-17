#pragma once

#include <time.h>

// Обработка расписания боя (планирование на отметках 00/15/30/45).
void chimeSchedulerOnTick(const tm& localTm);

// Неблокирующее обслуживание очереди воспроизведения боя.
void chimeSchedulerService();

// Сброс внутреннего состояния планировщика.
void chimeSchedulerReset();
