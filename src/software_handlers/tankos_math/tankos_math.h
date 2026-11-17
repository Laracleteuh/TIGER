#pragma once
#include <Arduino.h>
#include <avr/pgmspace.h>


#define TANKOSMATH_VERSION "tankos_math_lib_FreeRTOS2026.1"

/* Bornage et conversion d'échelle */

inline int32_t clampI(int32_t v, int32_t lo, int32_t hi) {
  return (v < lo) ? lo : ((v > hi) ? hi : v);
}

// Equivalent de map() avec bornage de l'entree et arrondi au plus proche.
// inMin doit etre different de inMax. Attention a l'overflow int32 :
// (inMax - inMin) * (outMax - outMin) doit rester sous 2 milliards.
int32_t mapClamped(int32_t x, int32_t inMin, int32_t inMax, int32_t outMin, int32_t outMax);

// Conversion d'une largeur d'impulsion RC (us) en -100..100 avec :
//  - echelle separee de chaque cote du centre (plage RC asymetrique)
//  - zone morte recentree (la sortie part de 0 juste apres la zone morte)
int16_t rcNormalize(int16_t us, int16_t minUs, int16_t centerUs, int16_t maxUs, int16_t dead);

// ---------------------------------------------------------------------------------------------
// 2. Filtres
// ---------------------------------------------------------------------------------------------

// Filtre passe-bas exponentiel a decalage de bits. Constante de temps ~ 2^shift periodes.
// shift valide : 1 a 8.
struct Ema16 {
  int32_t acc;
  uint8_t shift;
  bool primed;
};

void emaInit(Ema16& f, uint8_t shift);
int16_t emaUpdate(Ema16& f, int16_t x);

// Mediane de trois valeurs (rejet des impulsions parasites).
int16_t median3(int16_t a, int16_t b, int16_t c);

// Limiteur de pente : rapproche current de target d'au plus maxStep.
int16_t slewLimit(int16_t current, int16_t target, int16_t maxStep);

// Comparateur a hysteresis : passe a true au-dessus de high, a false en dessous de low.
struct Schmitt {
  bool state;
};
bool schmittUpdate(Schmitt& s, int16_t value, int16_t low, int16_t high);

// ---------------------------------------------------------------------------------------------
// 3. Table d'interpolation (en PROGMEM)
// ---------------------------------------------------------------------------------------------

struct LutPoint {
  int16_t x;
  int16_t y;
};

// table : tableau de LutPoint declare avec PROGMEM, x strictement croissant.
// n : nombre de points. x hors plage : renvoie le premier ou le dernier y.
int16_t lutInterp(const LutPoint* table, uint8_t n, int16_t x);

// ---------------------------------------------------------------------------------------------
// 4. Trigonometrie en angle binaire (BAM) : 0..65535 represente 0..360 degres.
//    Le depassement du uint16_t fait la normalisation d'angle gratuitement.
// ---------------------------------------------------------------------------------------------

typedef uint16_t angle16_t;

angle16_t angleFromDeg(int16_t deg);
int16_t angleToDeg(angle16_t a);          // renvoie 0..359

int16_t sinQ15(angle16_t a);              // -32767..32767 (1.0 = 32767)
inline int16_t cosQ15(angle16_t a) { return sinQ15((angle16_t)(a + 16384)); }

angle16_t atan2BAM(int16_t y, int16_t x); // erreur max environ 0.3 degre

// ---------------------------------------------------------------------------------------------
// 5. Racine carree et normes
// ---------------------------------------------------------------------------------------------

uint16_t isqrt32(uint32_t x);             // racine entiere, 16 iterations
uint16_t hypotI(int16_t x, int16_t y);    // sqrt(x^2 + y^2)

// ---------------------------------------------------------------------------------------------
// 6. Regulateur PID entier
//
// Gains en Q8 (256 = 1.0). Le PID doit etre appele a periode fixe : le pas de temps
// est inclus dans ki et kd. Anti-windup par integration conditionnelle.
// ---------------------------------------------------------------------------------------------

struct Pid {
  int16_t kp;
  int16_t ki;
  int16_t kd;
  int16_t outMin;
  int16_t outMax;
  int32_t integral;
  int16_t prevErr;
  bool primed;
};

void pidInit(Pid& p, int16_t kp, int16_t ki, int16_t kd, int16_t outMin, int16_t outMax);
void pidReset(Pid& p);
int16_t pidStep(Pid& p, int16_t setpoint, int16_t measure);

// ---------------------------------------------------------------------------------------------
// 7. Mixage differentiel avec normalisation proportionnelle
//    speed et turn en -100..100. Si un cote depasse 100, les deux cotes sont reduits
//    dans le meme rapport, ce qui conserve le rayon de virage.
// ---------------------------------------------------------------------------------------------

void arcadeMix(int16_t speed, int16_t turn, int16_t& left, int16_t& right);

// ---------------------------------------------------------------------------------------------
// 8. Balistique (float, calcul ponctuel, pas a appeler en boucle serree)
//
// Trajectoire parabolique sans traînee, branche basse.
//   x : distance horizontale a la cible (m)
//   y : denivele de la cible par rapport a la bouche du canon (m, positif si plus haut)
//   v : vitesse initiale du projectile (m/s)
// Renvoie false si la cible est hors de portee ou si les parametres sont invalides.
// thetaDeg : angle d'elevation du canon en degres.
// ---------------------------------------------------------------------------------------------

bool ballisticElevation(float x, float y, float v, float& thetaDeg);