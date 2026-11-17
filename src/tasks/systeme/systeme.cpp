/* SYSTEME */

/* Librairies */
#include "config.h"
#include "tasks/systeme/systeme.h"
#include <avr/wdt.h>
#include <Arduino_FreeRTOS.h>

#if HW_HAS_FAN_PWM
/* Fonction d'initialisation du PWM pour le ventilateur (Timer 2) */
void setupFanPWM() {
  pinMode(fanPWMPin, OUTPUT);
  cli();
  TCCR2A = (1 << COM2B1) | (1 << WGM21) | (1 << WGM20);
  TCCR2B = (1 << WGM22) | (1 << CS21);
  OCR2A = 79;
  OCR2B = 0;
  sei();
}
#endif

/* Fonction d'initialisation du système */
void setupSysteme() {
#if HW_HAS_FAN_PWM
  setupFanPWM();
#endif
  pinMode(SWA_PIN, INPUT);
  pinMode(SWB_PIN, INPUT);
  pinMode(SWC_PIN, INPUT);
  pinMode(SWD_PIN, INPUT);
  pinMode(VRA_PIN, INPUT);

#if HW_HAS_TOURELLE
  pinMode(switchPIN, OUTPUT);
  digitalWrite(switchPIN, LOW);
#endif
}

/* Fonction de mise à jour de l'état du switch contrôlant l'opérateur de la tourelle */
void updateSwitch(int swcRaw) {
#if HW_HAS_TOURELLE
  if (swcRaw > 1500) {
    digitalWrite(switchPIN, LOW);
  } else if (swcRaw < 1500) {
    digitalWrite(switchPIN, HIGH);
  }
#else
  (void)swcRaw;
#endif
}

/* Fonction de redémarrage logiciel du tank */
void rebootTank() {
  if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
    vTaskDelay(pdMS_TO_TICKS(100));
  } else {
    delay(100);
  }
  wdt_enable(WDTO_15MS);
  while (1) {}
}