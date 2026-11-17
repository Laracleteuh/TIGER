#pragma once
#include <Arduino.h>

#define BATT_DRIVER_VERSION "BATT_DRIVER_2026.4-RTOS"

void setupBatterie();
float getBatteryVoltage(uint8_t pin);
int getBatteryPercentage(uint8_t pin);
float getCurrentAmps();
void tareCurrentSensor();
void updateBatterie();