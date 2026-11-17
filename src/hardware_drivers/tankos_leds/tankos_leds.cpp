/* Contrôle des LED */

#include "config.h"
#include "hardware_drivers/tankos_leds/tankos_leds.h"

#if HW_HAS_STATUS_LEDS

#include <Arduino_FreeRTOS.h>

void initStatusLED() {
  pinMode(RStatusLED, OUTPUT);
  pinMode(GStatusLED, OUTPUT);
  pinMode(BStatusLED, OUTPUT);
  analogWrite(RStatusLED, 0);
  analogWrite(GStatusLED, 0);
  analogWrite(BStatusLED, 0);
}

void setStatusColor(int r, int g, int b) {
  analogWrite(RStatusLED, r);
  analogWrite(GStatusLED, g);
  analogWrite(BStatusLED, b);
}

void setLEDColor(int pin, int state) {
  digitalWrite(pin, state);
}

void blinkLED(int pin, int onDuration, int offDuration, int repeatCount) {
  for (int i = 0; i < repeatCount; i++) {
    digitalWrite(pin, HIGH);
    vTaskDelay(pdMS_TO_TICKS(onDuration));
    digitalWrite(pin, LOW);
    vTaskDelay(pdMS_TO_TICKS(offDuration));
  }
}

void initLED() {
  pinMode(adminStatusLED, OUTPUT);
  digitalWrite(adminStatusLED, LOW);
  pinMode(TurretControlStatusLED, OUTPUT);
  digitalWrite(TurretControlStatusLED, HIGH);
}

#endif // HW_HAS_STATUS_LEDS