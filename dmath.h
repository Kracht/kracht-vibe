/* SPDX-License-Identifier: GPL-3.0-or-later
 * Double-precision maths for offline rendering (the SDK's vd_* are fast float approximations – fine for audio
 * oscillators, too coarse for filter coefficients at 25 Hz or a kick's pitch curve). No libc: WebAssembly has
 * f64.floor and f64.sqrt as instructions. */
#ifndef DMATH_H
#define DMATH_H

#define D_PI  3.14159265358979323846
#define D_TAU 6.28318530717958647692
#define D_LN2 0.69314718055994530942

static inline double d_floor(double x) { return __builtin_floor(x); }
static inline double d_sqrt(double x) { return x <= 0 ? 0 : __builtin_sqrt(x); }
static inline double d_abs(double x) { return x < 0 ? -x : x; }
static inline double d_min(double a, double b) { return a < b ? a : b; }
static inline double d_max(double a, double b) { return a > b ? a : b; }

static double d_sin(double x)                       /* radians */
{
  x -= D_TAU * d_floor(x / D_TAU + 0.5);              /* -pi .. pi */
  if (x > D_PI / 2) x = D_PI - x; else if (x < -D_PI / 2) x = -D_PI - x;
  double x2 = x * x;
  return x * (1 + x2 * (-1.0 / 6 + x2 * (1.0 / 120 + x2 * (-1.0 / 5040 + x2 * (1.0 / 362880 + x2 * (-1.0 / 39916800 + x2 / 6227020800.0))))));
}
static inline double d_cos(double x) { return d_sin(x + D_PI / 2); }

static double d_exp(double x)
{
  if (x < -700) return 0;
  if (x > 700) x = 700;
  double k = d_floor(x / D_LN2 + 0.5), r = x - k * D_LN2;
  double s = 1 + r * (1 + r * (1.0 / 2 + r * (1.0 / 6 + r * (1.0 / 24 + r * (1.0 / 120 + r * (1.0 / 720 + r * (1.0 / 5040 + r * (1.0 / 40320 + r / 362880.0))))))));
  union { double d; unsigned long long u; } b;
  b.u = (unsigned long long)((long long)k + 1023) << 52;
  return s * b.d;
}

static double d_log(double x)
{
  if (x <= 0) return -1e300;
  union { double d; unsigned long long u; } b;
  b.d = x;
  long long e = (long long)((b.u >> 52) & 2047) - 1023;
  b.u = (b.u & 0xFFFFFFFFFFFFFull) | 0x3FF0000000000000ull;
  double m = b.d;
  if (m > 1.41421356237) { m *= 0.5; e++; }
  double t = (m - 1) / (m + 1), t2 = t * t;
  double s = 2 * t * (1 + t2 * (1.0 / 3 + t2 * (1.0 / 5 + t2 * (1.0 / 7 + t2 * (1.0 / 9 + t2 * (1.0 / 11 + t2 / 13))))));
  return s + (double)e * D_LN2;
}
static inline double d_pow(double a, double b) { return a <= 0 ? 0 : d_exp(b * d_log(a)); }
static inline double d_log10(double x) { return d_log(x) / 2.30258509299404568402; }
static double d_tanh(double x)
{
  if (x > 20) return 1;
  if (x < -20) return -1;
  double e = d_exp(2 * x);
  return (e - 1) / (e + 1);
}

/* an RBJ biquad, transposed direct form II, in doubles */
typedef struct { double b0, b1, b2, a1, a2, z1, z2; } dbq;
enum { BQ_LP, BQ_HP, BQ_BP, BQ_PEAK };
static void dbq_set(dbq *f, int type, double fr, double sr, double q, double db)
{
  double w = D_TAU * d_min(fr, sr * 0.49) / sr, c = d_cos(w), al = d_sin(w) / (2 * q);
  double b0 = 0, b1 = 0, b2 = 0, a0 = 1, a1 = -2 * c, a2 = 1 - al;
  if (type == BQ_LP) { b0 = (1 - c) / 2; b1 = 1 - c; b2 = b0; a0 = 1 + al; }
  else if (type == BQ_HP) { b0 = (1 + c) / 2; b1 = -(1 + c); b2 = b0; a0 = 1 + al; }
  else if (type == BQ_BP) { b0 = al; b1 = 0; b2 = -al; a0 = 1 + al; }
  else {
    double A = d_pow(10, db / 40);
    b0 = 1 + al * A; b1 = -2 * c; b2 = 1 - al * A; a0 = 1 + al / A; a2 = 1 - al / A;
  }
  f->b0 = b0 / a0; f->b1 = b1 / a0; f->b2 = b2 / a0; f->a1 = a1 / a0; f->a2 = a2 / a0; f->z1 = f->z2 = 0;
}
static inline double dbq_run(dbq *f, double x)
{
  double y = f->b0 * x + f->z1;
  f->z1 = f->b1 * x - f->a1 * y + f->z2;
  f->z2 = f->b2 * x - f->a2 * y;
  return y;
}

#endif
