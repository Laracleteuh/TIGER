#pragma once
#include <Arduino.h>
#include "config.h"

#define LED_DRIVER_VERSION "2026.1-RTOS"

#if HW_HAS_STATUS_LEDS
void initStatusLED();
void setStatusColor(int r, int g, int b);
void setLEDColor(int pin, int state);
void blinkLED(int pin, int onDuration, int offDuration, int repeatCount);
void initLED();
#else
inline void initStatusLED() {}
inline void setStatusColor(int, int, int) {}
inline void setLEDColor(int, int) {}
inline void blinkLED(int, int, int, int) {}
inline void initLED() {}
#endif