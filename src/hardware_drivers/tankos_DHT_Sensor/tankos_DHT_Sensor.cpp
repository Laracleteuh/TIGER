/*!
  @file DHT.cpp

Réécriture de la librairie DHT d'Adafruit pour l'adapter à l'usage dans TankOS.

This is a library for DHT series of low cost temperature/humidity sensors.
You must have Adafruit Unified Sensor Library library installed to use this
class.

Adafruit invests time and resources providing this open source code,
please support Adafruit andopen-source hardware by purchasing products
from Adafruit!

Author : 
Written by Adafruit Industries.
Rewritten by Projet Tank Terminale.

License : 
MIT license, all text above must be included in any redistribution


*/

#include "tankos_DHT_Sensor.h"
#include <Arduino_FreeRTOS.h>
#include <task.h>

#define MIN_INTERVAL_MS 2000
#define MIN_INTERVAL_TICKS pdMS_TO_TICKS(MIN_INTERVAL_MS)

#define TIMEOUT UINT32_MAX 


static void dhtWaitMs(uint32_t ms) {
  if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
    vTaskDelay(pdMS_TO_TICKS(ms));  
  } else {
    delay(ms);                   
  }
}

/*!
 *  @brief  Instantiates a new DHT class
 */
DHT::DHT(uint8_t pin, uint8_t type, uint8_t count) {
  (void)count;
  _pin = pin;
  _type = type;
#ifdef __AVR
  _bit = digitalPinToBitMask(pin);
  _port = digitalPinToPort(pin);
#endif
  _maxcycles = microsecondsToClockCycles(1000);
}

/*!
 *  @brief  Setup sensor pins and set pull timings
 */
void DHT::begin(uint8_t usec) {
  pinMode(_pin, INPUT_PULLUP);
  _lastreadtime = xTaskGetTickCount() - MIN_INTERVAL_TICKS;
  DEBUG_PRINT("DHT max clock cycles: ");
  DEBUG_PRINTLN(_maxcycles, DEC);
  pullTime = usec;
}

/*!
 *  @brief  Read temperature
 */
float DHT::readTemperature(bool S, bool force) {
  float f = NAN;

  if (read(force)) {
    switch (_type) {
      case DHT11:
        f = data[2];
        if (data[3] & 0x80) {
          f = -1 - f;
        }
        f += (data[3] & 0x0f) * 0.1;
        if (S) {
          f = convertCtoF(f);
        }
        break;
      case DHT12:
        f = data[2];
        f += (data[3] & 0x0f) * 0.1;
        if (data[2] & 0x80) {
          f *= -1;
        }
        if (S) {
          f = convertCtoF(f);
        }
        break;
      case DHT22:
      case DHT21:
        f = ((word)(data[2] & 0x7F)) << 8 | data[3];
        f *= 0.1;
        if (data[2] & 0x80) {
          f *= -1;
        }
        if (S) {
          f = convertCtoF(f);
        }
        break;
    }
  }
  return f;
}

float DHT::convertCtoF(float c) {
  return c * 1.8 + 32;
}

float DHT::convertFtoC(float f) {
  return (f - 32) * 0.55555;
}

/*!
 *  @brief  Read Humidity
 */
float DHT::readHumidity(bool force) {
  float f = NAN;
  if (read(force)) {
    switch (_type) {
      case DHT11:
      case DHT12:
        f = data[0] + data[1] * 0.1;
        break;
      case DHT22:
      case DHT21:
        f = ((word)data[0]) << 8 | data[1];
        f *= 0.1;
        break;
    }
  }
  return f;
}

float DHT::computeHeatIndex(bool isFahrenheit) {
  float hi = computeHeatIndex(readTemperature(isFahrenheit), readHumidity(),
                              isFahrenheit);
  return hi;
}

float DHT::computeHeatIndex(float temperature, float percentHumidity,
                            bool isFahrenheit) {
  float hi;

  if (!isFahrenheit)
    temperature = convertCtoF(temperature);

  hi = 0.5 * (temperature + 61.0 + ((temperature - 68.0) * 1.2) +
              (percentHumidity * 0.094));

  if (hi > 79) {
    hi = -42.379 + 2.04901523 * temperature + 10.14333127 * percentHumidity +
         -0.22475541 * temperature * percentHumidity +
         -0.00683783 * pow(temperature, 2) +
         -0.05481717 * pow(percentHumidity, 2) +
         0.00122874 * pow(temperature, 2) * percentHumidity +
         0.00085282 * temperature * pow(percentHumidity, 2) +
         -0.00000199 * pow(temperature, 2) * pow(percentHumidity, 2);

    if ((percentHumidity < 13) && (temperature >= 80.0) &&
        (temperature <= 112.0))
      hi -= ((13.0 - percentHumidity) * 0.25) *
            sqrt((17.0 - abs(temperature - 95.0)) * 0.05882);

    else if ((percentHumidity > 85.0) && (temperature >= 80.0) &&
             (temperature <= 87.0))
      hi += ((percentHumidity - 85.0) * 0.1) * ((87.0 - temperature) * 0.2);
  }

  return isFahrenheit ? hi : convertFtoC(hi);
}

/*!
 *  @brief  Read value from sensor or return last one if read less than 2 s ago.
 *  @param  force  true pour ignorer l'intervalle minimal
 */
bool DHT::read(bool force) {
  TickType_t now = xTaskGetTickCount();
  if (!force && ((TickType_t)(now - _lastreadtime) < MIN_INTERVAL_TICKS)) {
    return _lastresult;
  }
  _lastreadtime = now;

  data[0] = data[1] = data[2] = data[3] = data[4] = 0;

#if defined(ESP8266)
  yield();
#endif

  // Signal de démarrage
  pinMode(_pin, INPUT_PULLUP);
  dhtWaitMs(1);

  pinMode(_pin, OUTPUT);
  digitalWrite(_pin, LOW);
  switch (_type) {
    case DHT22:
    case DHT21:
      delayMicroseconds(1100);
      break;
    case DHT11:
    default:
      dhtWaitMs(20);
      break;
  }

  static uint32_t cycles[80];
  {
    pinMode(_pin, INPUT_PULLUP);
    delayMicroseconds(pullTime);

    // Section critique courte
    InterruptLock lock;

    if (expectPulse(LOW) == TIMEOUT) {
      DEBUG_PRINTLN(F("DHT timeout waiting for start signal low pulse."));
      _lastresult = false;
      return _lastresult;
    }
    if (expectPulse(HIGH) == TIMEOUT) {
      DEBUG_PRINTLN(F("DHT timeout waiting for start signal high pulse."));
      _lastresult = false;
      return _lastresult;
    }

    for (int i = 0; i < 80; i += 2) {
      cycles[i] = expectPulse(LOW);
      cycles[i + 1] = expectPulse(HIGH);
    }
  }  // fin de la section critique

  for (int i = 0; i < 40; ++i) {
    uint32_t lowCycles = cycles[2 * i];
    uint32_t highCycles = cycles[2 * i + 1];
    if ((lowCycles == TIMEOUT) || (highCycles == TIMEOUT)) {
      DEBUG_PRINTLN(F("DHT timeout waiting for pulse."));
      _lastresult = false;
      return _lastresult;
    }
    data[i / 8] <<= 1;
    if (highCycles > lowCycles) {
      data[i / 8] |= 1;
    }
  }

  DEBUG_PRINTLN(F("Received from DHT:"));
  DEBUG_PRINT(data[0], HEX);
  DEBUG_PRINT(F(", "));
  DEBUG_PRINT(data[1], HEX);
  DEBUG_PRINT(F(", "));
  DEBUG_PRINT(data[2], HEX);
  DEBUG_PRINT(F(", "));
  DEBUG_PRINT(data[3], HEX);
  DEBUG_PRINT(F(", "));
  DEBUG_PRINT(data[4], HEX);
  DEBUG_PRINT(F(" =? "));
  DEBUG_PRINTLN((data[0] + data[1] + data[2] + data[3]) & 0xFF, HEX);

  if (data[4] == ((data[0] + data[1] + data[2] + data[3]) & 0xFF)) {
    _lastresult = true;
    return _lastresult;
  }

  DEBUG_PRINTLN(F("DHT checksum failure!"));
  _lastresult = false;
  return _lastresult;
}

uint32_t DHT::expectPulse(bool level) {
#if (F_CPU > 16000000L) || (F_CPU == 0L)
  uint32_t count = 0;
#else
  uint16_t count = 0;
#endif
#ifdef __AVR
  uint8_t portState = level ? _bit : 0;
  while ((*portInputRegister(_port) & _bit) == portState) {
    if (count++ >= _maxcycles) {
      return TIMEOUT;
    }
  }
#else
  while (digitalRead(_pin) == level) {
    if (count++ >= _maxcycles) {
      return TIMEOUT;
    }
  }
#endif
  return count;
}