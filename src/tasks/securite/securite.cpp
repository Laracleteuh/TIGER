/* SECURITE */

/* Librairies */
#include "config.h"
#include "tasks/securite/securite.h"
#include "tasks/systeme/systeme.h"
#include "hardware_drivers/tankos_leds/tankos_leds.h"
#include <Arduino_FreeRTOS.h>
#include <math.h>

/* Etats des alertes */
static bool mainBattAlertActive = false;
static TickType_t lastMainAlertTick = 0;

#if HW_HAS_TURRET_BATTERY
static bool turretBattAlertActive = false;
static TickType_t lastTurretAlertTick = 0;
#endif

#if HW_HAS_CURRENT_SENSOR
static bool overcurrentAlertActive = false;
static TickType_t lastOvercurrentAlertTick = 0;
#endif

/* Verrouillage du bus série / bluetooth dans certains cas */
static bool serialLock() {
  if (serialMutex == NULL) return true;
  return xSemaphoreTake(serialMutex, pdMS_TO_TICKS(20)) == pdTRUE;
}

static void serialUnlock() {
  if (serialMutex != NULL) xSemaphoreGive(serialMutex);
}

/* Gestion des alertes de batterie */
void gererAlertesBatterie(int mainPct, int turretPct, float currentAmps) {
  (void)turretPct;
  (void)currentAmps;
  TickType_t now = xTaskGetTickCount();

  if (mainPct <= LOW_BATTERY_THRESHOLD_PCT) {
    setLEDColor(lowBattStatusLED, HIGH);
  } else {
    setLEDColor(lowBattStatusLED, LOW);
  }

  /* -- Alerte batterie principale faible -- */
  if (mainPct <= LOW_BATTERY_THRESHOLD_PCT) {
    if (!mainBattAlertActive || (now - lastMainAlertTick) * portTICK_PERIOD_MS >= ALERT_REPEAT_INTERVAL_MS) {
      if (serialLock()) {
        Serial.print(F("W0BATT - batterie principale faible : "));
        Serial.print(mainPct);
        Serial.println(F("%"));
#if HW_HAS_BT_HM10
        BT_SERIAL.print(F("W0BATT - batterie principale faible : "));
        BT_SERIAL.print(mainPct);
        BT_SERIAL.println(F("%"));
#endif
        serialUnlock();
      }
      lastMainAlertTick = now;
      mainBattAlertActive = true;
    }
  } else if (mainBattAlertActive) {
    if (serialLock()) {
      Serial.println(F("I0BATT - batterie principale revenue a un niveau normal."));
#if HW_HAS_BT_HM10
      BT_SERIAL.println(F("I0BATT - batterie principale revenue a un niveau normal."));
#endif
      serialUnlock();
    }
    mainBattAlertActive = false;
  }

#if HW_HAS_TURRET_BATTERY
  /* -- Alerte batterie tourelle faible -- */
  if (turretPct <= LOW_BATTERY_THRESHOLD_PCT) {
    if (!turretBattAlertActive || (now - lastTurretAlertTick) * portTICK_PERIOD_MS >= ALERT_REPEAT_INTERVAL_MS) {
      if (serialLock()) {
        Serial.print(F("W0BATT - batterie tourelle faible : "));
        Serial.print(turretPct);
        Serial.println(F("%"));
#if HW_HAS_BT_HM10
        BT_SERIAL.print(F("W0BATT - batterie tourelle faible : "));
        BT_SERIAL.print(turretPct);
        BT_SERIAL.println(F("%"));
#endif
        serialUnlock();
      }
      lastTurretAlertTick = now;
      turretBattAlertActive = true;
    }
  } else if (turretBattAlertActive) {
    if (serialLock()) {
      Serial.println(F("I0BATT - batterie tourelle revenue a un niveau normal."));
#if HW_HAS_BT_HM10
      BT_SERIAL.println(F("I0BATT - batterie tourelle revenue a un niveau normal."));
#endif
      serialUnlock();
    }
    turretBattAlertActive = false;
  }
#endif

#if HW_HAS_CURRENT_SENSOR
  /* -- Alerte sur-intensité -- */
  if (fabs(currentAmps) >= OVERCURRENT_THRESHOLD_AMPS) {
    setLEDColor(overAmpsStatusLED, HIGH);
    if (!overcurrentAlertActive || (now - lastOvercurrentAlertTick) * portTICK_PERIOD_MS >= ALERT_REPEAT_INTERVAL_MS) {
      if (serialLock()) {
        Serial.print(F("E0CURR - surintensite detectee : "));
        Serial.print(currentAmps, 1);
        Serial.println(F("A"));
#if HW_HAS_BT_HM10
        BT_SERIAL.print(F("E0CURR - surintensite detectee : "));
        BT_SERIAL.print(currentAmps, 1);
        BT_SERIAL.println(F("A"));
#endif
        serialUnlock();
      }
      lastOvercurrentAlertTick = now;
      overcurrentAlertActive = true;
    }
  } else if (overcurrentAlertActive) {
    setLEDColor(overAmpsStatusLED, LOW);
    if (serialLock()) {
      Serial.println(F("I0CURR - intensite revenue a la normale."));
#if HW_HAS_BT_HM10
      BT_SERIAL.println(F("I0CURR - intensite revenue a la normale."));
#endif
      serialUnlock();
    }
    overcurrentAlertActive = false;
  }
#endif
}

/* Fonctions pour le bridage de la batterie */
float calculerBridageBatterie(int batteryPct) {
  if (batteryPct <= 20) {
    return map(batteryPct, 0, 20, 30, 100) / 100.0f;
  }
  return 1.0f;
}

float calculerBridageCourant(float currentAmps) {
#if HW_HAS_CURRENT_SENSOR
  if (fabs(currentAmps) >= OVERCURRENT_THRESHOLD_AMPS) {
    return 0.2f;
  }
#else
  (void)currentAmps;
#endif
  return 1.0f;
}