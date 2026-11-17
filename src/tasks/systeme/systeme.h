#pragma once
#include <Arduino.h>

#define SYSTEM_MODULE_VERSION "SYSTEM_MODULE_2026.3-RTOS"

void setupSysteme();
void updateSwitch(int swcRaw);
void rebootTank();