#include "tankos_math.h"
#include <math.h>

// ---------------------------------------------------------------------------------------------
// 1. Bornage et conversion d'echelle
// ---------------------------------------------------------------------------------------------

int32_t mapClamped(int32_t x, int32_t inMin, int32_t inMax, int32_t outMin, int32_t outMax) {
  int32_t den = inMax - inMin;
  if (den == 0) return outMin;

  x = clampI(x, inMin, inMax);
  int32_t num = (x - inMin) * (outMax - outMin);
  int32_t half = (den > 0) ? (den / 2) : -(den / 2);
  num += (num >= 0) ? half : -half;
  return outMin + num / den;
}

int16_t rcNormalize(int16_t us, int16_t minUs, int16_t centerUs, int16_t maxUs, int16_t dead) {
  int16_t d = us - centerUs;
  if (d <= dead && d >= -dead) return 0;

  int16_t span;
  if (d > 0) {
    d -= dead;
    span = (maxUs - centerUs) - dead;
  } else {
    d += dead;
    span = (centerUs - minUs) - dead;
  }
  if (span <= 0) return 0;

  int32_t out = ((int32_t)d * 100L) / span;
  return (int16_t)clampI(out, -100, 100);
}

// ---------------------------------------------------------------------------------------------
// 2. Filtres
// ---------------------------------------------------------------------------------------------

void emaInit(Ema16& f, uint8_t shift) {
  if (shift < 1) shift = 1;
  if (shift > 8) shift = 8;
  f.acc = 0;
  f.shift = shift;
  f.primed = false;
}

int16_t emaUpdate(Ema16& f, int16_t x) {
  if (!f.primed) {
    f.acc = (int32_t)x << f.shift;
    f.primed = true;
  } else {
    f.acc += (int32_t)x - (f.acc >> f.shift);
  }
  return (int16_t)(f.acc >> f.shift);
}

int16_t median3(int16_t a, int16_t b, int16_t c) {
  if (a > b) { int16_t t = a; a = b; b = t; }
  if (b > c) { b = c; }
  return (a > b) ? a : b;
}

int16_t slewLimit(int16_t current, int16_t target, int16_t maxStep) {
  if (maxStep < 0) maxStep = 0;
  int32_t diff = (int32_t)target - current;
  if (diff > maxStep) return current + maxStep;
  if (diff < -maxStep) return current - maxStep;
  return target;
}

bool schmittUpdate(Schmitt& s, int16_t value, int16_t low, int16_t high) {
  if (s.state) {
    if (value < low) s.state = false;
  } else {
    if (value > high) s.state = true;
  }
  return s.state;
}

// ---------------------------------------------------------------------------------------------
// 3. Table d'interpolation
// ---------------------------------------------------------------------------------------------

int16_t lutInterp(const LutPoint* table, uint8_t n, int16_t x) {
  if (n == 0) return 0;

  int16_t x0 = (int16_t)pgm_read_word(&table[0].x);
  int16_t y0 = (int16_t)pgm_read_word(&table[0].y);
  if (n == 1 || x <= x0) return y0;

  int16_t xl = (int16_t)pgm_read_word(&table[n - 1].x);
  int16_t yl = (int16_t)pgm_read_word(&table[n - 1].y);
  if (x >= xl) return yl;

  for (uint8_t i = 1; i < n; i++) {
    int16_t x1 = (int16_t)pgm_read_word(&table[i].x);
    int16_t y1 = (int16_t)pgm_read_word(&table[i].y);
    if (x <= x1) {
      int32_t dx = (int32_t)x1 - x0;
      if (dx <= 0) return y1;
      int32_t num = ((int32_t)(y1 - y0)) * ((int32_t)x - x0);
      int32_t half = (num >= 0) ? (dx / 2) : -(dx / 2);
      return (int16_t)(y0 + (num + half) / dx);
    }
    x0 = x1;
    y0 = y1;
  }
  return yl;
}

// ---------------------------------------------------------------------------------------------
// 4. Trigonometrie
// ---------------------------------------------------------------------------------------------

// Table sin sur 360 degres, 256 entrees, Q15 (512 octets de flash).
static const int16_t SIN_LUT[256] PROGMEM = {
  0, 804, 1608, 2410, 3212, 4011, 4808, 5602,
  6393, 7179, 7962, 8739, 9512, 10278, 11039, 11793,
  12539, 13279, 14010, 14732, 15446, 16151, 16846, 17530,
  18204, 18868, 19519, 20159, 20787, 21403, 22005, 22594,
  23170, 23731, 24279, 24811, 25329, 25832, 26319, 26790,
  27245, 27683, 28105, 28510, 28898, 29268, 29621, 29956,
  30273, 30571, 30852, 31113, 31356, 31580, 31785, 31971,
  32137, 32285, 32412, 32521, 32609, 32678, 32728, 32757,
  32767, 32757, 32728, 32678, 32609, 32521, 32412, 32285,
  32137, 31971, 31785, 31580, 31356, 31113, 30852, 30571,
  30273, 29956, 29621, 29268, 28898, 28510, 28105, 27683,
  27245, 26790, 26319, 25832, 25329, 24811, 24279, 23731,
  23170, 22594, 22005, 21403, 20787, 20159, 19519, 18868,
  18204, 17530, 16846, 16151, 15446, 14732, 14010, 13279,
  12539, 11793, 11039, 10278, 9512, 8739, 7962, 7179,
  6393, 5602, 4808, 4011, 3212, 2410, 1608, 804,
  0, -804, -1608, -2410, -3212, -4011, -4808, -5602,
  -6393, -7179, -7962, -8739, -9512, -10278, -11039, -11793,
  -12539, -13279, -14010, -14732, -15446, -16151, -16846, -17530,
  -18204, -18868, -19519, -20159, -20787, -21403, -22005, -22594,
  -23170, -23731, -24279, -24811, -25329, -25832, -26319, -26790,
  -27245, -27683, -28105, -28510, -28898, -29268, -29621, -29956,
  -30273, -30571, -30852, -31113, -31356, -31580, -31785, -31971,
  -32137, -32285, -32412, -32521, -32609, -32678, -32728, -32757,
  -32767, -32757, -32728, -32678, -32609, -32521, -32412, -32285,
  -32137, -31971, -31785, -31580, -31356, -31113, -30852, -30571,
  -30273, -29956, -29621, -29268, -28898, -28510, -28105, -27683,
  -27245, -26790, -26319, -25832, -25329, -24811, -24279, -23731,
  -23170, -22594, -22005, -21403, -20787, -20159, -19519, -18868,
  -18204, -17530, -16846, -16151, -15446, -14732, -14010, -13279,
  -12539, -11793, -11039, -10278, -9512, -8739, -7962, -7179,
  -6393, -5602, -4808, -4011, -3212, -2410, -1608, -804,
};

angle16_t angleFromDeg(int16_t deg) {
  return (angle16_t)(((int32_t)deg * 65536L) / 360L);
}

int16_t angleToDeg(angle16_t a) {
  return (int16_t)(((uint32_t)a * 360UL) >> 16);
}

int16_t sinQ15(angle16_t a) {
  uint8_t i = (uint8_t)(a >> 8);
  uint8_t f = (uint8_t)(a & 0xFF);
  int16_t s0 = (int16_t)pgm_read_word(&SIN_LUT[i]);
  int16_t s1 = (int16_t)pgm_read_word(&SIN_LUT[(uint8_t)(i + 1)]);
  return s0 + (int16_t)(((int32_t)(s1 - s0) * f) >> 8);
}

angle16_t atan2BAM(int16_t y, int16_t x) {
  if (x == 0 && y == 0) return 0;

  uint16_t ax = (x < 0) ? (uint16_t)(-(int32_t)x) : (uint16_t)x;
  uint16_t ay = (y < 0) ? (uint16_t)(-(int32_t)y) : (uint16_t)y;
  uint16_t mn = (ax < ay) ? ax : ay;
  uint16_t mx = (ax < ay) ? ay : ax;

  // z = mn / mx en Q15 (0..32768)
  uint32_t z = (((uint32_t)mn) << 15) / mx;

  // atan(z) ~ z * (pi/4 + 0.273 * (1 - z)), exprime en BAM (45 degres = 8192, 0.273 rad = 2847)
  uint32_t zz = (z * (32768UL - z)) >> 15;
  uint32_t a = ((8192UL * z) >> 15) + ((2847UL * zz) >> 15);

  if (ay > ax) a = 16384UL - a;
  if (x < 0) a = 32768UL - a;
  if (y < 0) a = 65536UL - a;
  return (angle16_t)a;
}

// ---------------------------------------------------------------------------------------------
// 5. Racine carree et normes
// ---------------------------------------------------------------------------------------------

uint16_t isqrt32(uint32_t x) {
  uint32_t r = 0;
  uint32_t bit = 1UL << 30;
  while (bit > x) bit >>= 2;
  while (bit) {
    if (x >= r + bit) {
      x -= r + bit;
      r = (r >> 1) + bit;
    } else {
      r >>= 1;
    }
    bit >>= 2;
  }
  return (uint16_t)r;
}

uint16_t hypotI(int16_t x, int16_t y) {
  int32_t xx = (int32_t)x * x;
  int32_t yy = (int32_t)y * y;
  return isqrt32((uint32_t)(xx + yy));
}

// ---------------------------------------------------------------------------------------------
// 6. PID
// ---------------------------------------------------------------------------------------------

void pidInit(Pid& p, int16_t kp, int16_t ki, int16_t kd, int16_t outMin, int16_t outMax) {
  p.kp = kp;
  p.ki = ki;
  p.kd = kd;
  p.outMin = outMin;
  p.outMax = outMax;
  p.integral = 0;
  p.prevErr = 0;
  p.primed = false;
}

void pidReset(Pid& p) {
  p.integral = 0;
  p.prevErr = 0;
  p.primed = false;
}

int16_t pidStep(Pid& p, int16_t setpoint, int16_t measure) {
  int32_t err = (int32_t)setpoint - measure;
  if (!p.primed) {
    p.prevErr = (int16_t)err;
    p.primed = true;
  }

  int32_t pTerm = (int32_t)p.kp * err;
  int32_t dTerm = (int32_t)p.kd * (err - p.prevErr);
  int32_t iNew = p.integral + (int32_t)p.ki * err;

  int32_t out = (pTerm + iNew + dTerm) >> 8;

  // Integration conditionnelle : on n'integre pas quand la sortie sature
  // dans le sens ou l'erreur l'aggrave.
  if (out > p.outMax) {
    out = p.outMax;
    if (err < 0) p.integral = iNew;
  } else if (out < p.outMin) {
    out = p.outMin;
    if (err > 0) p.integral = iNew;
  } else {
    p.integral = iNew;
  }

  // Borne de securite sur l'integrale
  int32_t iMax = (int32_t)p.outMax << 8;
  int32_t iMin = (int32_t)p.outMin << 8;
  p.integral = clampI(p.integral, iMin, iMax);

  p.prevErr = (int16_t)err;
  return (int16_t)out;
}

// ---------------------------------------------------------------------------------------------
// 7. Mixage differentiel
// ---------------------------------------------------------------------------------------------

void arcadeMix(int16_t speed, int16_t turn, int16_t& left, int16_t& right) {
  int32_t l = (int32_t)speed + turn;
  int32_t r = (int32_t)speed - turn;

  int32_t al = (l < 0) ? -l : l;
  int32_t ar = (r < 0) ? -r : r;
  int32_t mx = (al > ar) ? al : ar;

  if (mx > 100) {
    l = (l * 100L) / mx;
    r = (r * 100L) / mx;
  }
  left = (int16_t)l;
  right = (int16_t)r;
}

// ---------------------------------------------------------------------------------------------
// 8. Balistique
// ---------------------------------------------------------------------------------------------

bool ballisticElevation(float x, float y, float v, float& thetaDeg) {
  const float g = 9.81f;
  if (x <= 0.0f || v <= 0.0f) return false;

  float v2 = v * v;
  float disc = v2 * v2 - g * (g * x * x + 2.0f * y * v2);
  if (disc < 0.0f) return false;

  float theta = atanf((v2 - sqrtf(disc)) / (g * x));
  thetaDeg = theta * 57.29578f;
  return true;
}