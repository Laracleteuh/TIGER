#pragma once

#define ISR_HANDLER_VERSION "ISR_HANDLER_2026.3-RTOS-DUAL_RX"

// -----------------------------------------------------------------------------
// HT10 : radio de conduite (10 voies)
//   joystick gauche Y = vitesse, joystick droit X = direction
//   SWC, SWD, VRA, VRB conserves
// -----------------------------------------------------------------------------
struct Ht10Data {
  int speedRaw;   // joystick gauche, axe Y
  int steerRaw;   // joystick droit, axe X
  int swcRaw;
  int swdRaw;
  int vraRaw;
  int vrbRaw;
};

// -----------------------------------------------------------------------------
// HT06 : radio de tourelle (6 voies)
//   joystick de controle tourelle + bouton de tir
// -----------------------------------------------------------------------------
struct Ht06Data {
  int turretX;
  int turretY;
  int fireRaw;
};

void setupRecepteurs();

// true tant qu'aucune impulsion n'a ete recue, ou si le signal est perdu (> 200 ms)
bool checkFailsafeHt10();
bool checkFailsafeHt06();

Ht10Data getHt10Data();
Ht06Data getHt06Data();