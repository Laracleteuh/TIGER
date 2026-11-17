#include <Arduino_FreeRTOS.h>
#include "config.h"
#include "tasks/tourelle/tourelle.h"

#if HW_HAS_TOURELLE

#include "software_handlers/tankos_math/tankos_math.h"
#include <Servo.h>

// -- Servos --
Servo servoAzimut;
Servo servoTranslation;
Servo servoElevation;
Servo esc1;
Servo esc2;

// -- Limite azimut --
static const int AZIMUT_MIN_ANGLE = 0;
static const int AZIMUT_MAX_ANGLE = 180;

// -- Limites translation (S2) --
static const int TRANS_CENTER_ANGLE = 90;
static const int TRANS_MAX_DELTA = 35;
static const int TRANS_MIN_ANGLE = TRANS_CENTER_ANGLE - TRANS_MAX_DELTA;
static const int TRANS_MAX_ANGLE = TRANS_CENTER_ANGLE + TRANS_MAX_DELTA;

// -- Limites elevation (S3) --
static const int ELEV_CENTER_ANGLE = 90;
static const int ELEV_MAX_DELTA = 40;
static const int ELEV_MIN_ANGLE = ELEV_CENTER_ANGLE - ELEV_MAX_DELTA;
static const int ELEV_MAX_ANGLE = ELEV_CENTER_ANGLE + ELEV_MAX_DELTA;

// -- Découpage de la course du joystick Y de tourelle --
static const int TRANS_ZONE_WIDTH = 350;

static int currentTransAngle = TRANS_CENTER_ANGLE;
static int currentElevAngle = ELEV_CENTER_ANGLE;

// -- Tir (bouton de la HT06) --
static const int FIRE_LOW_US = 1400;
static const int FIRE_HIGH_US = 1600;
static Schmitt fireSchmitt = {false};

static bool fireArmed = false;

void setupTourelle() {
  servoAzimut.attach(servoAzimutTourelle);
  servoTranslation.attach(servo1Tourelle);
  servoElevation.attach(servo2Tourelle);
  esc1.attach(esc1Tourelle);
  esc2.attach(esc2Tourelle);

  esc1.writeMicroseconds(1000);
  esc2.writeMicroseconds(1000);

  servoAzimut.write(90);
  servoTranslation.write(TRANS_CENTER_ANGLE);
  servoElevation.write(ELEV_CENTER_ANGLE);
  currentTransAngle = TRANS_CENTER_ANGLE;
  currentElevAngle = ELEV_CENTER_ANGLE;

  pinMode(TIR_PIN, OUTPUT);
  stopTir();
}

void updateTourelleAzimut(int turretX) {
  int angle = map(turretX, rxMin, rxMax, AZIMUT_MIN_ANGLE, AZIMUT_MAX_ANGLE);
  angle = constrain(angle, AZIMUT_MIN_ANGLE, AZIMUT_MAX_ANGLE);
  servoAzimut.write(angle);
}

void updateTourelleTranslationElevation(int turretY) {
  int offset = turretY - rxCenter;
  if (abs(offset) < deadzone) {
    servoTranslation.write(currentTransAngle);
    servoElevation.write(currentElevAngle);
    return;
  }

  int absOffset = abs(offset) - deadzone;
  bool forward = (offset > 0);

  if (absOffset <= TRANS_ZONE_WIDTH) {
    int delta = map(absOffset, 0, TRANS_ZONE_WIDTH, 0, TRANS_MAX_DELTA);
    currentTransAngle = forward ? (TRANS_CENTER_ANGLE + delta) : (TRANS_CENTER_ANGLE - delta);
    currentElevAngle = ELEV_CENTER_ANGLE;
  } else {
    currentTransAngle = forward ? TRANS_MAX_ANGLE : TRANS_MIN_ANGLE;
    int maxTravel = forward ? (rxMax - rxCenter) : (rxCenter - rxMin);
    int elevZoneWidth = maxTravel - deadzone - TRANS_ZONE_WIDTH;
    if (elevZoneWidth < 1) elevZoneWidth = 1;
    int elevOffset = absOffset - TRANS_ZONE_WIDTH;
    int delta = map(elevOffset, 0, elevZoneWidth, 0, ELEV_MAX_DELTA);
    delta = constrain(delta, 0, ELEV_MAX_DELTA);
    currentElevAngle = forward ? (ELEV_CENTER_ANGLE + delta) : (ELEV_CENTER_ANGLE - delta);
  }

  currentTransAngle = constrain(currentTransAngle, TRANS_MIN_ANGLE, TRANS_MAX_ANGLE);
  currentElevAngle = constrain(currentElevAngle, ELEV_MIN_ANGLE, ELEV_MAX_ANGLE);
  servoTranslation.write(currentTransAngle);
  servoElevation.write(currentElevAngle);
}

void updateEscPower(int vraRaw) {
  if (vraRaw > 900) {
    int escPwm = constrain(vraRaw, 1000, 2000);
    esc1.writeMicroseconds(escPwm);
    esc2.writeMicroseconds(escPwm);
  } else {
    esc1.writeMicroseconds(1000);
    esc2.writeMicroseconds(1000);
  }
}

void updateTir(int fireRaw) {
  if (!fireArmed) {
    if (fireRaw > 500 && fireRaw < FIRE_LOW_US) {
      fireArmed = true;
      fireSchmitt.state = false;
    }
    relayWrite(TIR_PIN, false);
    return;
  }
  bool fire = schmittUpdate(fireSchmitt, (int16_t)fireRaw, FIRE_LOW_US, FIRE_HIGH_US);
  relayWrite(TIR_PIN, fire);
}

void stopTir() {
  fireSchmitt.state = false;
  fireArmed = false;
  relayWrite(TIR_PIN, false);
}

void stopTourelle() {
  esc1.writeMicroseconds(1000);
  esc2.writeMicroseconds(1000);
}

#endif // HW_HAS_TOURELLE