#pragma once
#include <Arduino_FreeRTOS.h>   // TOUJOURS en premier, avant tout autre header FreeRTOS
#include <Arduino.h>
#include <math.h>
#include "config.h"

#define TEMPERATURE_MODULE_VERSION "TEMPERATURE_MODULE_2026.1-RTOS"

#if HW_HAS_TEMPERATURE
void setupTemperature();
void TaskTemperature(void *pvParameters);
float getTemperatureC();
bool isTempSensorPresent();
bool isMcpPresent();
bool isDhtPresent();
#else
inline void setupTemperature() {}
inline float getTemperatureC() { return NAN; }
inline bool isTempSensorPresent() { return false; }
inline bool isMcpPresent() { return false; }
inline bool isDhtPresent() { return false; }
#endif