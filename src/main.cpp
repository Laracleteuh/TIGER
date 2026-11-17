/* FICHIER PRINCIPAL */

/* Librairies systèmes */
#include <Arduino.h>
#include <Arduino_FreeRTOS.h>
#include <semphr.h>
#include <avr/wdt.h>
#include <avr/sleep.h>

/* Sécurité anti-watchdog */
#ifdef __cplusplus                                                                        // --C
extern "C" {
#endif
void wdt_init(void) __attribute__((naked, used, section(".init3")));
void wdt_init(void) {
    MCUSR = 0;
    wdt_disable();
}
#ifdef __cplusplus
}
#endif

/* Pilotes matériels du tank */
#include "config.h"

/* Tâches FreeRTOS */
#include "tasks/systeme/systeme.h"
#include "tasks/traction/traction.h"
#include "tasks/tourelle/tourelle.h"
#include "tasks/ISR/ISR.h"
#include "tasks/communication/communication.h"
#include "tasks/batterie/batterie.h"
#include "tasks/securite/securite.h"
#include "tasks/admin/admin.h"
#include "tasks/temperature/temperature.h"

/* Software Handlers */
#include "software_handlers/tankos_math/tankos_math.h"

/* Drivers Matériels */
#include "hardware_drivers/tankos_leds/tankos_leds.h"

/* Définition des handles et mutex (Propre à FreeRTOS) */
TaskHandle_t hTaskSafety = NULL;
TaskHandle_t hTaskTraction = NULL;
TaskHandle_t hTaskTourelle = NULL;
TaskHandle_t hTaskCommunication = NULL;
TaskHandle_t hTaskAdmin = NULL;
TaskHandle_t hTaskTemperature = NULL;

SemaphoreHandle_t spiMutex = NULL;
SemaphoreHandle_t stateMutex = NULL;
SemaphoreHandle_t serialMutex = NULL;

/* Définition des variables de sécurité pour la commande RC */
static volatile bool  g_failsafeHt10 = true;
static volatile bool  g_failsafeHt06 = true;
static volatile float g_safetyFactor = 0.0f;

/* Définitions des tâches FreeRTOS */

/* Tâche de sécurité */
void TaskSafety(void *pvParameters) {
  (void)pvParameters;
  for (;;) {
    updateBatterie();
    bool fsHt10 = checkFailsafeHt10();
    bool fsHt06 = checkFailsafeHt06();
    int mainPct = getBatteryPercentage(MAIN_BATTERY_PIN);
    float amps = getCurrentAmps();
    float battSafety = calculerBridageBatterie(mainPct);
    float currentSafety = calculerBridageCourant(amps);
    float safety = min(battSafety, currentSafety);

    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
      g_failsafeHt10 = fsHt10;
      g_failsafeHt06 = fsHt06;
      g_safetyFactor = safety;
      xSemaphoreGive(stateMutex);
    }

    if (fsHt10) {
      stopTraction();
      stopTourelle();
      stopTir();
      setTractionPowerSwitch(false);
      setStatusColor(255, 255, 0);
    } else {
      setTractionPowerSwitch(true);
      int turretPct = getBatteryPercentage(TURRET_BATTERY_PIN);
      gererAlertesBatterie(mainPct, turretPct, amps);
    }

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

/* Tâche de traction */
void TaskTraction(void *pvParameters) {
  (void)pvParameters;
  for (;;) {
    Ht10Data rc = getHt10Data();
    bool failsafe = true;
    float safetyFactor = 0.0f;

    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
      failsafe = g_failsafeHt10;
      safetyFactor = g_safetyFactor;
      xSemaphoreGive(stateMutex);
    }

    int speed = 0;
    int steering = 0;
    if (!failsafe) {
      float userMaxSpeed = getBluetoothSpeedLimit();
      float totalFactor = safetyFactor * userMaxSpeed;
      speed = rcToSpeed(rc.speedRaw) * totalFactor;
      steering = rcToSpeed(rc.steerRaw) * totalFactor;
    }

    updateTraction(speed, steering, rc.vrbRaw);
    vTaskDelay(pdMS_TO_TICKS(34));
  }
}

#if HW_HAS_TOURELLE
/* Tâche de tourelle */
void TaskTourelle(void *pvParameters) {
  (void)pvParameters;
  for (;;) {
    bool fsHt10 = true;
    bool fsHt06 = true;
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
      fsHt10 = g_failsafeHt10;
      fsHt06 = g_failsafeHt06;
      xSemaphoreGive(stateMutex);
    }

    Ht10Data rc10 = getHt10Data();
    if (!fsHt10) {
      updateSwitch(rc10.swcRaw);
    }

    if (!fsHt10 && !fsHt06) {
      Ht06Data rc06 = getHt06Data();
      updateEscPower(rc10.vraRaw);
      updateTourelleAzimut(rc06.turretX);
      updateTourelleTranslationElevation(rc06.turretY);
      updateTir(rc06.fireRaw);
    } else {
      stopTourelle();
      stopTir();
    }

    vTaskDelay(pdMS_TO_TICKS(34));
  }
}
#endif

/* Tâche de communication */
void TaskCommunication(void *pvParameters) {
  (void)pvParameters;
  for (;;) {
    Ht10Data rc = getHt10Data();
    updateCommunication(rc.swdRaw);
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

#if HW_HAS_RFID_RC522
/* Tâche d'administration */
void TaskAdmin(void *pvParameters) {
  (void)pvParameters;
  for (;;) {
    updateAdmin();
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}
#endif

/* Gestions des erreurs FreeRTOS */
extern "C" void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
  (void)xTask;
  Serial.print(F("E0STACK - overflow tache: "));
  Serial.println(pcTaskName);
  Serial.flush();
  while (1) {}
}

/* Fonction pour le mode veille */
extern "C" void vApplicationIdleHook(void) {
  set_sleep_mode(SLEEP_MODE_IDLE);
  sleep_enable();
  sleep_cpu();
  sleep_disable();
}

/* Initialisation */
void setup() {
  _delay_ms(1500);
  Serial.begin(115200);

  Serial.print(F("Demarrage de "));                                // SM
  Serial.println(F(FIRMWARE_VERSION));                             // SM
  Serial.print(F("Last build : "));                                // SM
  Serial.println(F(LAST_BUILD_DATE));                              // SM
  Serial.print(F("Auteur : "));                                    // SM
  Serial.println(F(FIRMWARE_AUTHOR));                              // SM

  initLED();
  initStatusLED();
  setStatusColor(255, 0, 0);
  _delay_ms(500);

  /* Initialisation des modules de sécurité et de communication */
  Serial.println(F("Initialisation des modules..."));              // SM
  setupCommunication();
  setupBatterie();
  setupAdmin();
  setStatusColor(0, 0, 255);

  /* Initialisation des modules de contrôle, puis initialisation des moteurs */
  setupSysteme();
  setupTemperature();
  setupTraction();

  /* Initialisation de la carte MDRVCT002M60A-MCP4110-ESN */
#if HW_HAS_TRACTION_MDRVCT
  pinMode(MDRVCT_FAILSAFE, OUTPUT);
  digitalWrite(MDRVCT_FAILSAFE, LOW);
#endif

  /* Initialisation de la tourelle puis des modules de réception RC */
  setupTourelle();
  setupRecepteurs();

  /* Initialisation des mutex pour la gestion des ressources partagées */
  spiMutex = xSemaphoreCreateMutex();
  stateMutex = xSemaphoreCreateMutex();
  serialMutex = xSemaphoreCreateMutex();

  if (spiMutex == NULL || stateMutex == NULL || serialMutex == NULL) {
    Serial.println(F("E0RTOS - echec creation des mutex. Arret."));           // SM
    while (1) {}
  }

  /* Création des tâches FreeRTOS selon hardware_config.h */
  BaseType_t okCommunication = xTaskCreate(TaskCommunication, "Communication", STACK_COMMUNICATION, NULL, PRIO_COMMUNICATION, &hTaskCommunication);
  BaseType_t okSafety        = xTaskCreate(TaskSafety,        "Safety",        STACK_SAFETY,        NULL, PRIO_SAFETY,        &hTaskSafety);
  BaseType_t okTraction      = xTaskCreate(TaskTraction,      "Traction",      STACK_TRACTION,      NULL, PRIO_TRACTION,      &hTaskTraction);

#if HW_HAS_TOURELLE
  BaseType_t okTourelle      = xTaskCreate(TaskTourelle,      "Tourelle",      STACK_TOURELLE,      NULL, PRIO_TOURELLE,      &hTaskTourelle);
#else
  BaseType_t okTourelle      = pdPASS;
#endif

#if HW_HAS_RFID_RC522
  BaseType_t okAdmin         = xTaskCreate(TaskAdmin,         "Admin",         STACK_ADMIN,         NULL, PRIO_ADMIN,         &hTaskAdmin);
#else
  BaseType_t okAdmin         = pdPASS;
#endif

#if HW_HAS_TEMPERATURE
  BaseType_t okTemperature   = xTaskCreate(TaskTemperature,   "Temperature",   STACK_TEMPERATURE,   NULL, PRIO_TEMPERATURE,   &hTaskTemperature);
#else
  BaseType_t okTemperature   = pdPASS;
#endif

  /* Gestion d'erreur de création des tâches */
  if (okCommunication != pdPASS || okSafety != pdPASS || okTraction != pdPASS ||
      okTourelle != pdPASS || okAdmin != pdPASS || okTemperature != pdPASS) {
    Serial.print(F("E0RTOS - echec creation tache(s) : "));                        // SM
    if (okCommunication != pdPASS) Serial.print(F("Communication "));              // SM
    if (okSafety != pdPASS)        Serial.print(F("Safety "));                     // SM
    if (okTraction != pdPASS)      Serial.print(F("Traction "));                   // SM
    if (okTourelle != pdPASS)      Serial.print(F("Tourelle "));                   // SM
    if (okAdmin != pdPASS)         Serial.print(F("Admin "));                      // SM
    if (okTemperature != pdPASS)   Serial.print(F("Temperature "));                // SM
    Serial.println();
    while (1) {}
  }

  _delay_ms(500);

  /* Initialisation du capteur de courant */
  tareCurrentSensor();

  /* Fin de la routine d'initialisation du firmware et des pilotes matériels */
  setStatusColor(0, 255, 0);
  Serial.println(F("Initialisation terminee. Demarrage du scheduler FreeRTOS"));   // SM
  markSetupComplete();

  /* Démarrage du scheduler FreeRTOS */
  vTaskStartScheduler();

  /* Gestion d'erreur de démarrage du scheduler */
  Serial.println(F("E0RTOS - echec demarrage du scheduler."));
  while (1) {}
}

void loop() {
  /* Laisser vide, le scheduler FreeRTOS gère l'exécution des tâches */
}