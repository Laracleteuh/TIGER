#include "tasks/batterie/batterie.h"
#include "config.h"
#include <Arduino_FreeRTOS.h>
#include "software_handlers/tankos_math/tankos_math.h"

#if HW_HAS_CURRENT_SENSOR
static float zeroAmpVoltage = 2.5f;
#endif

static Ema16 emaMain;
static volatile int16_t adcMainFiltered = 0;

#if HW_HAS_TURRET_BATTERY
static Ema16 emaTurret;
static volatile int16_t adcTurretFiltered = 0;
#endif

// Courbe de décharge de la batterie principale 6S.
static const LutPoint MAIN_BATT_LUT[] PROGMEM = {
  { 1980,   0 },
  { 2160,  10 },
  { 2208,  20 },
  { 2238,  40 },
  { 2316,  60 },
  { 2388,  80 },
  { 2460,  90 },
  { 2520, 100 }
};

static const uint8_t MAIN_BATT_LUT_SIZE = sizeof(MAIN_BATT_LUT) / sizeof(MAIN_BATT_LUT[0]);

void setupBatterie() {
  pinMode(MAIN_BATTERY_PIN, INPUT);
  emaInit(emaMain, 4);
  adcMainFiltered = emaUpdate(emaMain, analogRead(MAIN_BATTERY_PIN));

#if HW_HAS_TURRET_BATTERY
  pinMode(TURRET_BATTERY_PIN, INPUT);
  emaInit(emaTurret, 4);
  adcTurretFiltered = emaUpdate(emaTurret, analogRead(TURRET_BATTERY_PIN));
#endif

#if HW_HAS_CURRENT_SENSOR
  pinMode(CURRENT_SENSOR_PIN, INPUT);
#endif
}

void updateBatterie() {
  int16_t m = emaUpdate(emaMain, analogRead(MAIN_BATTERY_PIN));
#if HW_HAS_TURRET_BATTERY
  int16_t t = emaUpdate(emaTurret, analogRead(TURRET_BATTERY_PIN));
#endif

  taskENTER_CRITICAL();
  adcMainFiltered = m;
#if HW_HAS_TURRET_BATTERY
  adcTurretFiltered = t;
#endif
  taskEXIT_CRITICAL();
}

float getBatteryVoltage(uint8_t pin) {
  int valeurADC = 0;
  taskENTER_CRITICAL();
  if (pin == MAIN_BATTERY_PIN) {
    valeurADC = adcMainFiltered;
  }
#if HW_HAS_TURRET_BATTERY
  else if (pin == TURRET_BATTERY_PIN) {
    valeurADC = adcTurretFiltered;
  }
#endif
  taskEXIT_CRITICAL();

  float tensionBroche = valeurADC * (5.0f / 1023.0f);
  return tensionBroche * 6.0f;
}

int getBatteryPercentage(uint8_t pin) {
  if (pin == MAIN_BATTERY_PIN) {
    float tensionBatterie = getBatteryVoltage(pin);
    int16_t centivolts = (int16_t)(tensionBatterie * 100.0f + 0.5f);
    int pourcentage = lutInterp(MAIN_BATT_LUT, MAIN_BATT_LUT_SIZE, centivolts);
    return constrain(pourcentage, 0, 100);
  }
#if HW_HAS_TURRET_BATTERY
  if (pin == TURRET_BATTERY_PIN) {
    float tensionBatterie = getBatteryVoltage(pin);
    int tensionEntiere = (int)(tensionBatterie * 10);
    int pourcentage = map(tensionEntiere, 99, 126, 0, 100);
    return constrain(pourcentage, 0, 100);
  }
#endif
  return 100;
}

float getCurrentAmps() {
#if HW_HAS_CURRENT_SENSOR
  int valeurADC = analogRead(CURRENT_SENSOR_PIN);
  float tensionBroche = valeurADC * (5.0f / 1023.0f);
  return (tensionBroche - zeroAmpVoltage) / 0.020f;
#else
  return 0.0f;
#endif
}

void tareCurrentSensor() {
#if HW_HAS_CURRENT_SENSOR
  long sum = 0;
  for (int i = 0; i < 20; i++) {
    sum += analogRead(CURRENT_SENSOR_PIN);
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
      vTaskDelay(pdMS_TO_TICKS(2));
    } else {
      delay(2);
    }
  }
  float avgADC = sum / 20.0f;
  zeroAmpVoltage = avgADC * (5.0f / 1023.0f);
#endif
}