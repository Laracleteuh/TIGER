/* TEMPERATURE : fusion DHT11 + MCP9808 */
#include <Arduino_FreeRTOS.h>
#include "config.h"
#include "tasks/temperature/temperature.h"

#if HW_HAS_TEMPERATURE

#if HW_HAS_DHT11
#include "hardware_drivers/tankos_DHT_Sensor/tankos_DHT_Sensor.h"
#endif

#if HW_HAS_MCP9808
#include "hardware_drivers/tankos_MCP9808_Sensor/tankos_MCP9808_Sensor.h"
#include <Wire.h>
#endif

#include <math.h>

/* ---------- Paramètres ---------- */
#if HW_HAS_MCP9808
#define MCP_I2C_ADDR   0x18
#endif

#if HW_HAS_DHT11
#define DHT_TYPE       DHT11
#endif

#if HW_HAS_MCP9808 && HW_HAS_DHT11
static const float W_MCP = 0.9f;
static const float W_DHT = 0.1f;
static const float MAX_ECART_C = 5.0f;
#endif

static const TickType_t SENSOR_TIMEOUT_TICKS = pdMS_TO_TICKS(5000);
static const TickType_t TEMP_PERIOD_TICKS = pdMS_TO_TICKS(2500);

/* ---------- Objets capteurs ---------- */
struct SensorState {
  bool ready;
  bool everValid;
  float value;
  TickType_t lastValid;
};

#if HW_HAS_MCP9808
static Adafruit_MCP9808 mcp;
static SensorState mcpState = {false, false, NAN, 0};
#endif

#if HW_HAS_DHT11
static DHT dht(DHT_PIN, DHT_TYPE);
static SensorState dhtState = {false, false, NAN, 0};
#endif

static volatile float fusedTemp = NAN;

/* ---------- Utilitaires ---------- */
static bool estValide(const SensorState &s, TickType_t now) {
  return s.everValid && ((now - s.lastValid) < SENSOR_TIMEOUT_TICKS);
}

static void enregistrer(SensorState &s, float valeur, TickType_t now) {
  s.value = valeur;
  s.lastValid = now;
  s.everValid = true;
}

/* ---------- Lectures ---------- */
#if HW_HAS_MCP9808
static void lectureMcp(TickType_t now) {
  if (!mcpState.ready) {
    if (!mcp.begin(MCP_I2C_ADDR)) return;
    mcp.setResolution(0);
    mcpState.ready = true;
  }
  float t = mcp.readTempC();
  if (!isnan(t)) {
    enregistrer(mcpState, t, now);
  }
}
#endif

#if HW_HAS_DHT11
static void lectureDht(TickType_t now) {
  float t = dht.readTemperature(false, false);
  if (!isnan(t)) {
    enregistrer(dhtState, t, now);
  }
}
#endif

/* ---------- Fusion ---------- */
static void fusionner(TickType_t now) {
  float resultat = NAN;

#if HW_HAS_MCP9808 && HW_HAS_DHT11
  bool mcpOk = estValide(mcpState, now);
  bool dhtOk = estValide(dhtState, now);

  if (mcpOk && dhtOk) {
    float ecart = fabs(mcpState.value - dhtState.value);
    if (ecart > MAX_ECART_C) {
      resultat = mcpState.value;
    } else {
      resultat = (W_MCP * mcpState.value + W_DHT * dhtState.value) / (W_MCP + W_DHT);
    }
  } else if (mcpOk) {
    resultat = mcpState.value;
  } else if (dhtOk) {
    resultat = dhtState.value;
  }
#elif HW_HAS_MCP9808
  if (estValide(mcpState, now)) {
    resultat = mcpState.value;
  }
#elif HW_HAS_DHT11
  if (estValide(dhtState, now)) {
    resultat = dhtState.value;
  }
#endif

  taskENTER_CRITICAL();
  fusedTemp = resultat;
  taskEXIT_CRITICAL();
}

/* ---------- Tâche FreeRTOS ---------- */
void TaskTemperature(void *pvParameters) {
  (void)pvParameters;
  TickType_t lastWake = xTaskGetTickCount();

  for (;;) {
    TickType_t now = xTaskGetTickCount();
#if HW_HAS_MCP9808
    lectureMcp(now);
#endif
#if HW_HAS_DHT11
    lectureDht(now);
#endif
    fusionner(now);
    vTaskDelayUntil(&lastWake, TEMP_PERIOD_TICKS);
  }
}

/* ---------- API publique ---------- */
void setupTemperature() {
#if HW_HAS_MCP9808
  mcpState.ready = mcp.begin(MCP_I2C_ADDR);
  if (mcpState.ready) {
    mcp.setResolution(0);
    Serial.println(F("MCP9808 detecte."));
  } else {
    Serial.println(F("MCP9808 absent ou non detecte."));
  }
#endif

#if HW_HAS_DHT11
  dht.begin();
  Serial.println(F("DHT11 initialise (detection au premier cycle)."));
#endif
}

float getTemperatureC() {
  float t;
  taskENTER_CRITICAL();
  t = fusedTemp;
  taskEXIT_CRITICAL();
  return t;
}

bool isTempSensorPresent() {
  return !isnan(getTemperatureC());
}

bool isMcpPresent() {
#if HW_HAS_MCP9808
  return estValide(mcpState, xTaskGetTickCount());
#else
  return false;
#endif
}

bool isDhtPresent() {
#if HW_HAS_DHT11
  return estValide(dhtState, xTaskGetTickCount());
#else
  return false;
#endif
}

#endif // HW_HAS_TEMPERATURE