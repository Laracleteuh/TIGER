/*!
 *  @file Adafruit_MCP9808.cpp
 *
 *  @mainpage Adafruit MCP9808 I2C Temp Sensor
 *
 *  @section intro_sec Introduction
 *
 * 	I2C Driver for Microchip's MCP9808 I2C Temp sensor
 *
 * 	This is a library for the Adafruit MCP9808 breakout:
 * 	http://www.adafruit.com/products/1782
 *
 * 	Adafruit invests time and resources providing this open source code,
 *  please support Adafruit and open-source hardware by purchasing products from
 * 	Adafruit!
 *
 *  @section author Author
 *
 *  K.Townsend (Adafruit Industries)
 *  Rewritten by Projet Tank Terminale
 *
 * 	@section license License
 *
 * 	BSD (see license.txt)
 *
 * 	@section  HISTORY
 *
 *     v1.0 - First release
 */

#include "hardware_drivers/tankos_MCP9808_Sensor/tankos_MCP9808_Sensor.h"
#include <string.h>
#include <math.h>

#define MCP9808_WAKE_TIME_MS 260   ///< Temps de réveil après shutdown (datasheet ~250 ms)

/* Attente en millisecondes compatible avec et sans scheduler */
static void mcpWaitMs(uint32_t ms) {
  if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
    vTaskDelay(pdMS_TO_TICKS(ms));   // la tâche dort, le reste du système tourne
  } else {
    delay(ms);                       // avant le démarrage du scheduler
  }
}

/*!
 *    @brief  Instancie la classe MCP9808
 */
Adafruit_MCP9808::Adafruit_MCP9808() {}

/*!
 *    @brief  Initialise le matériel (adresse par défaut, Wire)
 *    @return true si l'initialisation réussit
 */
bool Adafruit_MCP9808::begin(TwoWire *theWire) {
  return begin(MCP9808_I2CADDR_DEFAULT, theWire);
}

bool Adafruit_MCP9808::begin(uint8_t addr) { return begin(addr, &Wire); }

bool Adafruit_MCP9808::begin() { return begin(MCP9808_I2CADDR_DEFAULT, &Wire); }

/*!
 *    @brief  Initialise le matériel (adresse et bus donnés)
 *    @return true si l'initialisation réussit
 */
bool Adafruit_MCP9808::begin(uint8_t addr, TwoWire *theWire) {
  if (i2c_dev) {
    delete i2c_dev;
  }
  i2c_dev = new Adafruit_I2CDevice(addr, theWire);
  return init();
}

/*!
 *    @brief  Vérifie l'identité du composant et désactive le mode config
 *    @return true si le capteur est détecté
 */
bool Adafruit_MCP9808::init() {
  if (!i2c_dev->begin()) {
    return false;
  }
  if (read16(MCP9808_REG_MANUF_ID) != 0x0054) return false;
  if (read16(MCP9808_REG_DEVICE_ID) != 0x0400) return false;

  write16(MCP9808_REG_CONFIG, 0x0);
  return true;
}

/*!
 *    @brief  Convertit la valeur brute du registre température en °C
 *    @param  raw  valeur 16 bits du registre
 *    @return température en °C
 */
float Adafruit_MCP9808::rawToCelsius(uint16_t raw) const {
  float temp = (raw & 0x0FFF) / 16.0f;
  if (raw & 0x1000) {
    temp -= 256.0f;
  }
  return temp;
}

/*!
 *   @brief  Lit la température ambiante en °C
 *   @return température en °C, NAN en cas d'erreur de lecture I2C
 */
float Adafruit_MCP9808::readTempC() {
  uint16_t t = read16(MCP9808_REG_AMBIENT_TEMP);
  if (t == 0xFFFF) {
    return NAN;
  }
  return rawToCelsius(t);
}

/*!
 *   @brief  Lit la température ambiante en °F
 *   @return température en °F, NAN en cas d'erreur de lecture I2C
 */
float Adafruit_MCP9808::readTempF() {
  float c = readTempC();
  if (isnan(c)) {
    return NAN;
  }
  return c * 9.0f / 5.0f + 32.0f;
}

/*!
 *   @brief  Met le capteur en veille (bit SHUTDOWN) ou le réveille
 *   @param  sw  true = veille, false = réveil
 */
void Adafruit_MCP9808::shutdown_wake(boolean sw) {
  uint16_t conf = read16(MCP9808_REG_CONFIG);
  conf = sw ? (conf | MCP9808_REG_CONFIG_SHUTDOWN)
            : (conf & ~MCP9808_REG_CONFIG_SHUTDOWN);
  write16(MCP9808_REG_CONFIG, conf);
}

/*!
 *   @brief  Met le capteur en veille
 */
void Adafruit_MCP9808::shutdown() { shutdown_wake(true); }

/*!
 *   @brief  Réveille le capteur et attend sa stabilisation (non bloquant pour les autres tâches)
 */
void Adafruit_MCP9808::wake() {
  shutdown_wake(false);
  mcpWaitMs(MCP9808_WAKE_TIME_MS);
}

/*!
 *   @brief  Lit la résolution courante
 */
uint8_t Adafruit_MCP9808::getResolution() {
  return read8(MCP9808_REG_RESOLUTION);
}

/*!
 *   @brief  Règle la résolution (0 = 0.5 °C, 1 = 0.25 °C, 2 = 0.125 °C, 3 = 0.0625 °C)
 */
void Adafruit_MCP9808::setResolution(uint8_t value) {
  write8(MCP9808_REG_RESOLUTION, value & 0x03);
}

/*!
 *    @brief  Écriture 16 bits (MSB first)
 */
void Adafruit_MCP9808::write16(uint8_t reg, uint16_t value) {
  Adafruit_BusIO_Register reg16 =
      Adafruit_BusIO_Register(i2c_dev, reg, 2, MSBFIRST);
  reg16.write(value);
}

/*!
 *    @brief  Lecture 16 bits (MSB first)
 */
uint16_t Adafruit_MCP9808::read16(uint8_t reg) {
  Adafruit_BusIO_Register reg16 =
      Adafruit_BusIO_Register(i2c_dev, reg, 2, MSBFIRST);
  return reg16.read();
}

/*!
 *    @brief  Écriture 8 bits
 */
void Adafruit_MCP9808::write8(uint8_t reg, uint8_t value) {
  Adafruit_BusIO_Register reg8 = Adafruit_BusIO_Register(i2c_dev, reg, 1);
  reg8.write(value);
}

/*!
 *    @brief  Lecture 8 bits
 */
uint8_t Adafruit_MCP9808::read8(uint8_t reg) {
  Adafruit_BusIO_Register reg8 = Adafruit_BusIO_Register(i2c_dev, reg, 1);
  return reg8.read();
}

/*!
 *    @brief  Remplit un événement Unified Sensor avec la température
 *    @param  temp  objet événement à remplir
 *    @return true
 */
bool Adafruit_MCP9808::getEvent(sensors_event_t *temp) {
  memset(temp, 0, sizeof(sensors_event_t));
  temp->version = sizeof(sensors_event_t);
  temp->sensor_id = _sensorID;
  temp->type = SENSOR_TYPE_AMBIENT_TEMPERATURE;
  // Horodatage en millisecondes, basé sur le tick FreeRTOS
  temp->timestamp = (uint32_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
  temp->temperature = readTempC();
  return true;
}

/*!
 *    @brief  Remplit la description du capteur Unified Sensor
 *    @param  sensor  objet descriptif à remplir
 */
void Adafruit_MCP9808::getSensor(sensor_t *sensor) {
  memset(sensor, 0, sizeof(sensor_t));

  strncpy(sensor->name, "MCP9808", sizeof(sensor->name) - 1);
  sensor->name[sizeof(sensor->name) - 1] = 0;
  sensor->version = 1;
  sensor->sensor_id = _sensorID;
  sensor->type = SENSOR_TYPE_AMBIENT_TEMPERATURE;
  sensor->min_delay = 0;
  sensor->max_value = 100.0;
  sensor->min_value = -20.0;
  sensor->resolution = 0.0625;
}