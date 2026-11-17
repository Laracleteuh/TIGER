/*!
 *  @file Adafruit_MCP9808.h
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
 * Rewritten by Projet Tank Terminale
 * 
 *	BSD license (see license.txt)
 */

#ifndef _ADAFRUIT_MCP9808_H
#define _ADAFRUIT_MCP9808_H

#include <Arduino_FreeRTOS.h>   // TOUJOURS en premier, avant tout autre include FreeRTOS
#include <Arduino.h>
#include "3rd_party_dependency/Adafruit_BusIO_Register/Adafruit_BusIO_Register.h"
#include "3rd_party_dependency/Adafruit_I2CDevice/Adafruit_I2CDevice.h"
#include "3rd_party_dependency/Adafruit_Sensor/Adafruit_Sensor.h"

#define MCP9808_I2CADDR_DEFAULT 0x18 ///< Adresse I2C par défaut
#define MCP9808_REG_CONFIG 0x01      ///< Registre de configuration

#define MCP9808_REG_CONFIG_SHUTDOWN 0x0100   ///< Bit shutdown
#define MCP9808_REG_CONFIG_CRITLOCKED 0x0080 ///< Verrou température critique
#define MCP9808_REG_CONFIG_WINLOCKED 0x0040  ///< Verrou fenêtre d'alarme
#define MCP9808_REG_CONFIG_INTCLR 0x0020     ///< Effacement interruption
#define MCP9808_REG_CONFIG_ALERTSTAT 0x0010  ///< État sortie alerte
#define MCP9808_REG_CONFIG_ALERTCTRL 0x0008  ///< Contrôle sortie alerte
#define MCP9808_REG_CONFIG_ALERTSEL 0x0004   ///< Sélection alerte
#define MCP9808_REG_CONFIG_ALERTPOL 0x0002   ///< Polarité alerte
#define MCP9808_REG_CONFIG_ALERTMODE 0x0001  ///< Mode alerte

#define MCP9808_REG_UPPER_TEMP 0x02   ///< Limite haute d'alerte
#define MCP9808_REG_LOWER_TEMP 0x03   ///< Limite basse d'alerte
#define MCP9808_REG_CRIT_TEMP 0x04    ///< Température critique
#define MCP9808_REG_AMBIENT_TEMP 0x05 ///< Température ambiante
#define MCP9808_REG_MANUF_ID 0x06     ///< Identifiant fabricant
#define MCP9808_REG_DEVICE_ID 0x07    ///< Identifiant composant
#define MCP9808_REG_RESOLUTION 0x08   ///< Résolution

/*!
 *    @brief  Classe qui gère le capteur de température MCP9808
 */
class Adafruit_MCP9808 : public Adafruit_Sensor {
public:
  Adafruit_MCP9808();
  bool begin();
  bool begin(TwoWire *theWire);
  bool begin(uint8_t addr);
  bool begin(uint8_t addr, TwoWire *theWire);

  bool init();
  float readTempC();
  float readTempF();
  uint8_t getResolution(void);
  void setResolution(uint8_t value);

  void shutdown_wake(boolean sw);
  void shutdown();
  void wake();

  void write16(uint8_t reg, uint16_t val);
  uint16_t read16(uint8_t reg);

  void write8(uint8_t reg, uint8_t val);
  uint8_t read8(uint8_t reg);

  /* API Unified Sensor */
  bool getEvent(sensors_event_t *);
  void getSensor(sensor_t *);

private:
  float rawToCelsius(uint16_t raw) const;

  uint16_t _sensorID = 9808;
  Adafruit_I2CDevice *i2c_dev = NULL;
};

#endif