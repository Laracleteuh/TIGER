/* MODULE DE GESTION DES INTERRUPTIONS */

/* Librairies */
#include "config.h"
#include "tasks/ISR/ISR.h"
#include <Arduino.h>
#include <Arduino_FreeRTOS.h>
#include <EnableInterrupt.h>

#define RC_TIMEOUT_MS 200  // Délai sans impulsion au-delà duquel une radio est considérée perdue

/* -- Variables globales Manette HT10 -- */
static volatile int speedRaw_v = 1500, steerRaw_v = 1500;
static volatile int swcRaw_v = 1500, swdRaw_v = 1500;
static volatile int vraRaw_v = 1500, vrbRaw_v = 1500;

/* -- Chronomètres de mesure d'impulsion HT10 -- */
static volatile unsigned long timer_speed, timer_steer, timer_swc, timer_swd, timer_vra, timer_vrb;
static volatile TickType_t lastHt10Tick = 0;
static volatile bool ht10Seen = false;

#if HW_HAS_RC_HT06
/* -- Variables globales Manette HT06 -- */
static volatile int turretX_v = 1500, turretY_v = 1500, fireRaw_v = 1000;
static volatile unsigned long timer_tX, timer_tY, timer_fire;
static volatile TickType_t lastHt06Tick = 0;
static volatile bool ht06Seen = false;
#endif

/* Routines d'interruption HT10 */
void isr_speed() {
  if (digitalRead(HT10_SPEED_PIN) == HIGH) timer_speed = micros();
  else {
    speedRaw_v = (int)(micros() - timer_speed);
    lastHt10Tick = xTaskGetTickCountFromISR();
    ht10Seen = true;
  }
}

void isr_steer() {
  if (digitalRead(HT10_STEER_PIN) == HIGH) timer_steer = micros();
  else steerRaw_v = (int)(micros() - timer_steer);
}

void isr_swc() {
  if (digitalRead(SWC_PIN) == HIGH) timer_swc = micros();
  else swcRaw_v = (int)(micros() - timer_swc);
}

void isr_swd() {
  if (digitalRead(SWD_PIN) == HIGH) timer_swd = micros();
  else swdRaw_v = (int)(micros() - timer_swd);
}

void isr_vra() {
  if (digitalRead(VRA_PIN) == HIGH) timer_vra = micros();
  else vraRaw_v = (int)(micros() - timer_vra);
}

void isr_vrb() {
  if (digitalRead(VRB_PIN) == HIGH) timer_vrb = micros();
  else vrbRaw_v = (int)(micros() - timer_vrb);
}

#if HW_HAS_RC_HT06
/* Routines d'interruption HT06 */
void isr_turretX() {
  if (digitalRead(HT06_TURRET_X_PIN) == HIGH) timer_tX = micros();
  else turretX_v = (int)(micros() - timer_tX);
}

void isr_turretY() {
  if (digitalRead(HT06_TURRET_Y_PIN) == HIGH) timer_tY = micros();
  else {
    turretY_v = (int)(micros() - timer_tY);
    lastHt06Tick = xTaskGetTickCountFromISR();
    ht06Seen = true;
  }
}

void isr_fire() {
  if (digitalRead(HT06_FIRE_PIN) == HIGH) timer_fire = micros();
  else fireRaw_v = (int)(micros() - timer_fire);
}
#endif

/* Initialisation du module ISR */
void setupRecepteurs() {
  pinMode(HT10_SPEED_PIN, INPUT);
  pinMode(HT10_STEER_PIN, INPUT);
  pinMode(SWC_PIN, INPUT);
  pinMode(SWD_PIN, INPUT);
  pinMode(VRA_PIN, INPUT);
  pinMode(VRB_PIN, INPUT);

  enableInterrupt(HT10_SPEED_PIN, isr_speed, CHANGE);
  enableInterrupt(HT10_STEER_PIN, isr_steer, CHANGE);
  enableInterrupt(SWC_PIN, isr_swc, CHANGE);
  enableInterrupt(SWD_PIN, isr_swd, CHANGE);
  enableInterrupt(VRA_PIN, isr_vra, CHANGE);
  enableInterrupt(VRB_PIN, isr_vrb, CHANGE);

#if HW_HAS_RC_HT06
  pinMode(HT06_TURRET_X_PIN, INPUT);
  pinMode(HT06_TURRET_Y_PIN, INPUT);
  pinMode(HT06_FIRE_PIN, INPUT);

  enableInterrupt(HT06_TURRET_X_PIN, isr_turretX, CHANGE);
  enableInterrupt(HT06_TURRET_Y_PIN, isr_turretY, CHANGE);
  enableInterrupt(HT06_FIRE_PIN, isr_fire, CHANGE);
#endif
}

/* Gestion des sécurités de liaison radio des manettes HT10 et HT06 */
static bool linkLost(const volatile TickType_t& lastTick, const volatile bool& seen) {
  TickType_t last, now;
  bool s;
  taskENTER_CRITICAL();
  last = lastTick;
  s = seen;
  now = xTaskGetTickCount();
  taskEXIT_CRITICAL();
  if (!s) return true;
  return ((TickType_t)(now - last) * portTICK_PERIOD_MS > RC_TIMEOUT_MS);
}

bool checkFailsafeHt10() {
  return linkLost(lastHt10Tick, ht10Seen);
}

bool checkFailsafeHt06() {
#if HW_HAS_RC_HT06
  return linkLost(lastHt06Tick, ht06Seen);
#else
  return true;
#endif
}

/* Lecture des données de la manette HT10 */
Ht10Data getHt10Data() {
  Ht10Data d;
  taskENTER_CRITICAL();
  d.speedRaw = speedRaw_v;
  d.steerRaw = steerRaw_v;
  d.swcRaw = swcRaw_v;
  d.swdRaw = swdRaw_v;
  d.vraRaw = vraRaw_v;
  d.vrbRaw = vrbRaw_v;
  taskEXIT_CRITICAL();
  return d;
}

/* Lecture des données de la manette HT06 */
Ht06Data getHt06Data() {
  Ht06Data d;
#if HW_HAS_RC_HT06
  taskENTER_CRITICAL();
  d.turretX = turretX_v;
  d.turretY = turretY_v;
  d.fireRaw = fireRaw_v;
  taskEXIT_CRITICAL();
#else
  d.turretX = 1500;
  d.turretY = 1500;
  d.fireRaw = 1000;
#endif
  return d;
}