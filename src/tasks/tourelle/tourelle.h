#pragma once
#include "config.h"

#define TOURELLE_DRIVER_VERSION "TOURELLE_DRIVER_2026.3-RTOS-SERVO_AZIMUT-TIR_HT06"
#define TOURELLE_HARDWARE "STD Cannon Turret - POGO Interface"

#if HW_HAS_TOURELLE
void setupTourelle();
void updateTourelleAzimut(int turretX);
void updateTourelleTranslationElevation(int turretY);
void updateEscPower(int vraRaw);
void updateTir(int fireRaw);
void stopTir();
void stopTourelle();
#else
inline void setupTourelle() {}
inline void updateTourelleAzimut(int) {}
inline void updateTourelleTranslationElevation(int) {}
inline void updateEscPower(int) {}
inline void updateTir(int) {}
inline void stopTir() {}
inline void stopTourelle() {}
#endif