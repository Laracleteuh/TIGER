#pragma once
#include <Arduino.h>
#include <Arduino_FreeRTOS.h>
#include <semphr.h>
#include "hardware_config.h"

// -- Valeurs de repli si une macro est absente de hardware_config.h --
#ifndef HW_HAS_TRACTION_MDRVCT
  #define HW_HAS_TRACTION_MDRVCT 1
#endif
#ifndef HW_HAS_RC_HT10
  #define HW_HAS_RC_HT10 1
#endif
#ifndef HW_HAS_RC_HT06
  #define HW_HAS_RC_HT06 1
#endif
#ifndef HW_HAS_TOURELLE
  #define HW_HAS_TOURELLE 1
#endif
#ifndef HW_HAS_MAIN_BATTERY
  #define HW_HAS_MAIN_BATTERY 1
#endif
#ifndef HW_HAS_TURRET_BATTERY
  #define HW_HAS_TURRET_BATTERY 1
#endif
#ifndef HW_HAS_CURRENT_SENSOR
  #define HW_HAS_CURRENT_SENSOR 1
#endif
#ifndef HW_HAS_MCP9808
  #define HW_HAS_MCP9808 1
#endif
#ifndef HW_HAS_DHT11
  #define HW_HAS_DHT11 1
#endif
#ifndef HW_HAS_FAN_PWM
  #define HW_HAS_FAN_PWM 1
#endif
#ifndef HW_HAS_RFID_RC522
  #define HW_HAS_RFID_RC522 1
#endif
#ifndef HW_HAS_BT_HM10
  #define HW_HAS_BT_HM10 1
#endif
#ifndef HW_HAS_STATUS_LEDS
  #define HW_HAS_STATUS_LEDS 1
#endif
#ifndef HW_HAS_TEMPERATURE
  #define HW_HAS_TEMPERATURE (HW_HAS_MCP9808 || HW_HAS_DHT11)
#endif

// -- Vérification des dépendances matérielles --
#if HW_HAS_TOURELLE && !HW_HAS_RC_HT06
  #warning "HW_HAS_TOURELLE desactive automatiquement car HW_HAS_RC_HT06 est a 0."
  #undef HW_HAS_TOURELLE
  #define HW_HAS_TOURELLE 0
#endif

// -- Firmware information --
#define FIRMWARE_VERSION "TIGER MkI (Basé sur FreeRTOS pour microcontrôleur AVR)"
#define LAST_BUILD_DATE "18/11/2026 @ 21:00"
#define FIRMWARE_AUTHOR "Projet Tank Terminale - TID-70  - Specialite SI 2026 - 2027"

// -- RELAY STATE MACROS --
#define RELAY_ON HIGH
#define RELAY_OFF LOW

// Fonction utilitaire pour tous les relais
inline void relayWrite(int pin, bool state) {
  digitalWrite(pin, state ? RELAY_ON : RELAY_OFF);
}

// -- MOTOR SETUP (Traction) --
#define leftMotorFwdRelay 22
#define leftMotorBckRelay 23
#define rightMotorFwdRelay 24
#define rightMotorBckRelay 25
#define leftMotorOnOffSwitch 26
#define rightMotorOnOffSwitch 27

// -- MCP41100 SETUP (SPI) --
// SCK est matériellement sur la broche 52 de l'Arduino Mega
// SI (MOSI) est matériellement sur la broche 51 de l'Arduino Mega
#define CS_PIN1 42
#define CS_PIN2 43

// -- F-10A SETUP --
#define HT10_SPEED_PIN      A11   // joystick gauche, axe Y
#define HT10_STEER_PIN      A8    // joystick droit, axe X

// HT06
#define HT06_TURRET_X_PIN   A10
#define HT06_TURRET_Y_PIN   A9
#define HT06_FIRE_PIN       A13

// Sortie de tir
#define TIR_PIN             38
#define SWA_PIN 44
#define SWB_PIN 10
#define SWC_PIN 11
#define SWD_PIN 12
#define VRA_PIN 13
#define VRB_PIN 45

// -- TOURELLE & ARMEMENT --
#define switchPIN 31
#define servoAzimutTourelle 6
#define servo1Tourelle 2
#define servo2Tourelle 3
#define esc1Tourelle 4
#define esc2Tourelle 5

// -- CAPTEURS ET VENTILATEURS --
#define fanPWMPin 9

// -- STATUS LEDS --
#define RStatusLED 49
#define GStatusLED 47
#define BStatusLED 46
#define TurretControlStatusLED 48
#define adminStatusLED 39
#define lowBattStatusLED 35
#define overAmpsStatusLED 36
#define commStatusLED 34

// -- MDRVCT FAILSAFE --
#define MDRVCT_FAILSAFE 32

// -- HM-10 BLUETOOTH MODULE --
#define BT_SERIAL Serial1
#define BT_BRK_PIN 37

// -- DHT11 Sensor --
#define DHT_PIN   30

// -- LECTEUR RFID ADMIN (RC522, SPI, partage le bus SPI0 avec les MCP41100
// de traction.cpp : SCK=52, MOSI=51, MISO=50) --
#define RFID_SS_PIN 41
#define RFID_RST_PIN 40

// -- BATTERY & CURRENT SENSORS --
#define MAIN_BATTERY_PIN A1    // Batterie Châssis (Traction)
#define TURRET_BATTERY_PIN A2  // Batterie Tourelle (Armement/Servos)
#define CURRENT_SENSOR_PIN A0  // Capteur de courant
#define LOW_BATTERY_THRESHOLD_PCT 15
#define OVERCURRENT_THRESHOLD_AMPS 20.0f
#define ALERT_REPEAT_INTERVAL_MS 3000

// -- RC VARIABLES --
const int rxMin = 980;
const int rxMax = 2000;
const int rxCenter = 1500;
const int deadzone = 50;

// Fonction utilitaire pour la conversion RC
int16_t rcNormalize(int16_t us, int16_t minUs, int16_t centerUs, int16_t maxUs, int16_t dead);
inline int rcToSpeed(int rawValue) {
  return rcNormalize(rawValue, rxMin, rxCenter, rxMax, deadzone);
}

// -- Priorité des tâches --
#define PRIO_SAFETY 4
#define PRIO_TRACTION 3
#define PRIO_TOURELLE 3
#define PRIO_COMMUNICATION 2
#define PRIO_TEMPERATURE 2
#define PRIO_ADMIN 1

// -- Tailles de pile --
#define STACK_SAFETY        300
#define STACK_TRACTION      300
#define STACK_TOURELLE      300
#define STACK_COMMUNICATION 400
#define STACK_ADMIN         300
#define STACK_TEMPERATURE   256

// -- Handles de tâches (définis dans main.cpp) --
extern TaskHandle_t hTaskSafety;
extern TaskHandle_t hTaskTraction;
extern TaskHandle_t hTaskTourelle;
extern TaskHandle_t hTaskCommunication;
extern TaskHandle_t hTaskAdmin;
extern TaskHandle_t hTaskTemperature;

// -- Mutex --
extern SemaphoreHandle_t spiMutex;
extern SemaphoreHandle_t stateMutex;
extern SemaphoreHandle_t serialMutex;