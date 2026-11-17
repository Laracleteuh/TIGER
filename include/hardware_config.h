#pragma once
// Fichier généré automatiquement par TankOS Configurator v1.7.1
// Date : 16/11/2026 21:54:50

// Carte traction MDRVCT002 + 2x MCP41100
#define HW_HAS_TRACTION_MDRVCT 1

// Récepteur RC principal HT-10 (Conduite)
#define HW_HAS_RC_HT10 1

// Récepteur RC secondaire HT-06 (Tourelle)
#define HW_HAS_RC_HT06 1

// Tourelle complète (Azimut, Trans/Élév, ESC & Tir)
#define HW_HAS_TOURELLE 1

// Pont diviseur Batterie Principale (6S LiPo)
#define HW_HAS_MAIN_BATTERY 1

// Pont diviseur Batterie Tourelle (3S LiPo)
#define HW_HAS_TURRET_BATTERY 1

// Capteur de courant châssis (20 mV/A)
#define HW_HAS_CURRENT_SENSOR 1

// Capteur de température haute précision MCP9808
#define HW_HAS_MCP9808 1

// Capteur température/humidité DHT11
#define HW_HAS_DHT11 1

// Ventilation refroidissement (PWM Timer 2)
#define HW_HAS_FAN_PWM 1

// Lecteur badge RFID Admin MFRC522
#define HW_HAS_RFID_RC522 1

// Module Bluetooth BLE HM-10
#define HW_HAS_BT_HM10 1

// Panneau LEDs de statut & LED RGB
#define HW_HAS_STATUS_LEDS 1

// Macro dérivée : active TaskTemperature si au moins un capteur thermique est présent
#define HW_HAS_TEMPERATURE 1
