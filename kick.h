/* SPDX-License-Identifier: GPL-3.0-or-later
 * The kick designer's voice, rendered offline into a one-shot.
 *
 *   BODY  a sine (WAVE squares it) swept from FROM down to TUNE in SWEEP, CURVE between a sweep even in octaves and
 *         one even in Hz, ZAP a second, far faster chirp on top (the hard-techno "tok")
 *   AMP   attack, hold, then exp(-(t/DECAY)^(1+3 WALL)); GATE cuts the whole kick at a length, over FADE
 *   CLICK band-passed noise or a tone burst at CTONE, CDEC long
 *   DRIVE body and click through a peak at FOCUS (+EMPH), a shaper (SOFT, TUBE, FOLD, HARD), the peak taken back out,
 *         a low-pass at POST for the fizz
 *   TAIL  the body's own phase as a clean sine past the drive: the sub that stays round under a broken top
 *   OUT   LOWCUT, TONE, normalised to LEVEL dBFS peak
 *
 * Body and click run at four times the rate through the drive and come down through two Kaiser half-bands, so a
 * hard clip or a fold does not alias. Noise is seeded: one set of values is one sound, always.
 */
#include "dmath.h"

enum { K_TUNE, K_FROM, K_SWEEP, K_CURVE, K_ZAP, K_ZAPT, K_WAVE, K_PHASE,
       K_ATK, K_HOLD, K_DECAY, K_WALL, K_GATE, K_FADE,
       K_CLICK, K_CTONE, K_CDEC, K_CTYPE,
       K_TAIL, K_TDEC, K_TIN, K_TOCT,
       K_DRIVE, K_MODE, K_FOCUS, K_EMPH, K_POST,
       K_LOWCUT, K_TONE, K_LEVEL, K_COUNT };

static const char *const SHAPES[4] = { "Soft", "Tube", "Fold", "Hard" };
static const char *const OCTS[2] = { "0", "-1" };
/* the kick's values in a fixed order, ids prefixed k_ */
static const vibe_param KICK_SPECS[K_COUNT] = {
  { "k_tune",  "Tune",    30, 90,   49,   "Hz", 0, 0 },
  { "k_from",  "From",    60, 2000, 220,  "Hz", 0, 0 },
  { "k_sweep", "Sweep",   3,  400,  60,   "ms", 0, 0 },
  { "k_curve", "Curve",   0,  1,    0.3f, "",   0, 0 },
  { "k_zap",   "Zap",     0,  1,    0,    "",   0, 0 },
  { "k_zapt",  "Zap time",0.3f, 20, 3,    "ms", 0, 0 },
  { "k_wave",  "Wave",    0,  1,    0,    "",   0, 0 },
  { "k_phase", "Phase",   0,  1,    0.5f, "",   0, 0 },
  { "k_atk",   "Attack",  0,  10,   0.4f, "ms", 0, 0 },
  { "k_hold",  "Hold",    0,  400,  20,   "ms", 0, 0 },
  { "k_decay", "Decay",   20, 2000, 250,  "ms", 0, 0 },
  { "k_wall",  "Wall",    0,  1,    0.55f,"",   0, 0 },
  { "k_gate",  "Gate",    0,  2000, 0,    "ms", 0, 0 },
  { "k_fade",  "Fade",    1,  60,   8,    "ms", 0, 0 },
  { "k_click", "Click",   0,  1,    0.3f, "",   0, 0 },
  { "k_ctone", "Click tone", 300, 12000, 3000, "Hz", 0, 0 },
  { "k_cdec",  "Click decay", 0.3f, 60, 4, "ms", 0, 0 },
  { "k_ctype", "Noise/tone", 0, 1,  0.2f, "",   0, 0 },
  { "k_tail",  "Tail",    0,  1,    0,    "",   0, 0 },
  { "k_tdec",  "Tail decay", 50, 3000, 500, "ms", 0, 0 },
  { "k_tin",   "Tail in", 0,  300,  40,   "ms", 0, 0 },
  { "k_toct",  "Tail oct",0,  1,    0,    "",   2, OCTS },
  { "k_drive", "Drive",   0,  36,   6,    "dB", 0, 0 },
  { "k_mode",  "Shape",   0,  3,    0,    "",   4, SHAPES },
  { "k_focus", "Focus",   60, 4000, 300,  "Hz", 0, 0 },
  { "k_emph",  "Emph",    0,  18,   3,    "dB", 0, 0 },
  { "k_post",  "Post",    1000, 20000, 12000, "Hz", 0, 0 },
  { "k_lowcut","Lowcut",  10, 80,   25,   "Hz", 0, 0 },
  { "k_tone",  "Tone",    2000, 20000, 18000, "Hz", 0, 0 },
  { "k_level", "Level",   -12, 0,   -1,   "dB", 0, 0 },
};

/* presets, EXHALE first – long, broken, swinging */
typedef struct { const char *name; signed char k[12]; float v[12]; } kick_preset;
#define KP_END -1
static const kick_preset KICK_PRESETS[] = {
  { "EXHALE", { K_TUNE, K_FROM, K_SWEEP, K_CURVE, K_ZAP, K_WAVE, K_DECAY, K_WALL, K_TAIL, K_DRIVE, K_MODE, K_EMPH },
              { 47, 340, 58, 0.35f, 0.3f, 0.35f, 430, 0.35f, 0.75f, 26, 1, 9 } },
  { "KRACHT", { KP_END }, { 0 } },
  { "PSY",    { K_TUNE, K_FROM, K_SWEEP, K_CURVE, K_WAVE, K_HOLD, K_DECAY, K_WALL, K_GATE, K_CLICK, K_CTONE, KP_END },
              { 47, 330, 110, 0.6f, 0.05f, 120, 160, 0.9f, 190, 0.25f, 4500 } },
  { "HOUSE",  { K_TUNE, K_FROM, K_SWEEP, K_HOLD, K_DECAY, K_WALL, K_GATE, K_FADE, K_CLICK, K_DRIVE, K_EMPH, K_TONE },
              { 50, 240, 50, 60, 160, 0.6f, 220, 20, 0.15f, 2, 0, 12000 } },
  { "RUMBLE", { K_TUNE, K_FROM, K_SWEEP, K_WAVE, K_DECAY, K_TAIL, K_TDEC, K_DRIVE, K_MODE, K_FOCUS, K_EMPH, K_POST },
              { 52, 160, 25, 0.2f, 180, 0.8f, 700, 14, 1, 180, 6, 6000 } },
  { "ZAP",    { K_TUNE, K_FROM, K_SWEEP, K_ZAP, K_ZAPT, K_WAVE, K_DECAY, K_CLICK, K_TAIL, K_DRIVE, K_MODE, K_FOCUS },
              { 55, 260, 35, 0.7f, 4, 0.3f, 220, 0.4f, 0.4f, 24, 1, 900 } },
  { "INDUSTRIAL", { K_TUNE, K_FROM, K_SWEEP, K_DECAY, K_WALL, K_TAIL, K_TDEC, K_DRIVE, K_MODE, K_FOCUS, K_EMPH, K_POST },
              { 45, 200, 40, 400, 0.3f, 0.5f, 500, 30, 2, 400, 9, 5000 } },
};
#define NKP ((int)(sizeof KICK_PRESETS / sizeof KICK_PRESETS[0]))
/* EXHALE's other values (the twelve slots above are not enough) */
static void exhale_extra(float *p)
{
  p[K_ZAPT] = 3; p[K_PHASE] = 0.7f; p[K_ATK] = 0.3f; p[K_HOLD] = 35; p[K_CLICK] = 0.35f; p[K_CTONE] = 4200;
  p[K_CDEC] = 3; p[K_CTYPE] = 0.3f; p[K_TDEC] = 650; p[K_TIN] = 25; p[K_FOCUS] = 230; p[K_POST] = 7500; p[K_LOWCUT] = 28;
  p[K_TONE] = 16000;
}
static void kick_preset_apply(int i, float *p)
{
  for (int k = 0; k < K_COUNT; k++) p[k] = KICK_SPECS[k].def;
  if (i == 0) exhale_extra(p);
  for (int j = 0; j < 12 && KICK_PRESETS[i].k[j] != KP_END; j++) p[KICK_PRESETS[i].k[j]] = KICK_PRESETS[i].v[j];
  if (i == 2) { p[K_PHASE] = 0.8f; p[K_FADE] = 6; p[K_CDEC] = 2; p[K_CTYPE] = 0.6f; p[K_DRIVE] = 3; p[K_EMPH] = 0; p[K_LOWCUT] = 30; }
  if (i == 3) { p[K_CTONE] = 2500; p[K_CDEC] = 3; p[K_CTYPE] = 0.2f; }
  if (i == 4) { p[K_CURVE] = 0.2f; p[K_HOLD] = 10; p[K_WALL] = 0.4f; p[K_TIN] = 40; p[K_LOWCUT] = 28; p[K_CLICK] = 0.2f; }
  if (i == 5) { p[K_CURVE] = 0.3f; p[K_WALL] = 0.5f; p[K_CTONE] = 6000; p[K_CDEC] = 2; p[K_TDEC] = 300; p[K_EMPH] = 12; p[K_POST] = 9000; }
  if (i == 6) { p[K_CLICK] = 0.2f; }
}

/* ---- the curves ---------------------------------------------------------------------------------------------- */
typedef struct { double tune, ratio, span, curve, tau, zap, zt, a, h, dec, pw, tin, tdec, gate, fade; } kcurves;
static void kc_init(kcurves *c, const float *p)
{
  c->tune = p[K_TUNE]; c->ratio = (double)p[K_FROM] / p[K_TUNE]; c->span = (double)p[K_FROM] - p[K_TUNE]; c->curve = p[K_CURVE];
  c->tau = p[K_SWEEP] / 1000.0 / 3; c->zap = p[K_ZAP] * 12.0; c->zt = p[K_ZAPT] / 1000.0;
  c->a = p[K_ATK] / 1000.0; c->h = p[K_HOLD] / 1000.0; c->dec = p[K_DECAY] / 1000.0; c->pw = 1 + 3.0 * p[K_WALL];
  c->tin = p[K_TIN] / 1000.0; c->tdec = p[K_TDEC] / 1000.0; c->gate = p[K_GATE] / 1000.0; c->fade = p[K_FADE] / 1000.0;
}
static double kc_pitch(const kcurves *c, double t)
{
  double e = d_exp(-t / c->tau);
  double fLog = c->tune * d_pow(c->ratio, e), fLin = c->tune + c->span * e;
  double f = fLog + (fLin - fLog) * c->curve;
  return c->zap > 0 ? f * (1 + c->zap * d_exp(-t / c->zt)) : f;
}
static double kc_amp(const kcurves *c, double t)
{
  if (t < c->a) return t / c->a;
  if (t < c->a + c->h) return 1;
  return d_exp(-d_pow((t - c->a - c->h) / c->dec, c->pw));
}
static double kc_tail(const kcurves *c, double t)
{
  double up = c->tin > 0 ? d_min(1, t / c->tin) : 1;
  return up * up * (3 - 2 * up) * d_exp(-(t / c->tdec) * (t / c->tdec));
}
static double kc_gate(const kcurves *c, double t)
{
  if (c->gate <= 0 || t < c->gate - c->fade) return 1;
  if (t >= c->gate) return 0;
  return 0.5 + 0.5 * d_cos(D_PI * (t - c->gate + c->fade) / c->fade);
}
static double kick_length(const float *p)
{
  if (p[K_GATE] > 0) return p[K_GATE] / 1000.0;
  double body = (p[K_ATK] + p[K_HOLD]) / 1000.0 + (p[K_DECAY] / 1000.0) * d_pow(9.2, 1 / (1 + 3.0 * p[K_WALL]));
  double tail = p[K_TAIL] > 0 ? (p[K_TDEC] / 1000.0) * d_sqrt(9.2) : 0;
  return d_min(4, d_max(0.05, d_max(body, tail)));
}

static double shape(int mode, double x)
{
  switch (mode) {
  case 1: return d_tanh(x + 0.35) - 0.3364;          /* TUBE: bent, even harmonics */
  case 2: return d_sin(x * D_PI / 2);                 /* FOLD */
  case 3: return x > 1 ? 1 : x < -1 ? -1 : x;         /* HARD */
  default: return d_tanh(x);                          /* SOFT */
  }
}

/* ---- down from x4: two Kaiser-windowed half-bands -------------------------------------------------------------- */
#define HA 11
#define HB 31
#define PAD (HA + 2 * HB)
static double FA[2 * HA + 1], FB[2 * HB + 1];
static double i0(double x) { double s = 1, t = 1; for (int k = 1; k < 30; k++) { t *= (x / (2 * k)) * (x / (2 * k)); s += t; } return s; }
static void halfband(double *h, int H, double beta)
{
  double sum = 0;
  for (int i = 0; i <= 2 * H; i++) {
    int m = i - H; double r = (double)m / H;
    double sinc = m == 0 ? 0.5 : (m % 2 == 0 ? 0 : d_sin(D_PI * m / 2) / (D_PI * m));
    h[i] = sinc * i0(beta * d_sqrt(1 - r * r)) / i0(beta);
    sum += h[i];
  }
  for (int i = 0; i <= 2 * H; i++) h[i] /= sum;
}
/* ---- the render ---------------------------------------------------------------------------------------------- */
#define SR_MAX 96000
#define KICK_MAX (4 * SR_MAX)                 /* 4 s */
#define BODY_MAX (3 * SR_MAX / 2)             /* the driven part, 1.5 s */
static float kx[PAD + 4 * BODY_MAX + 2 * PAD + 8];
static float kh1[2 * BODY_MAX + 2 * HB + 8], kh2[BODY_MAX + 8];
static double kphs[KICK_MAX];
static uint32_t knoise;
static double knoise_next(void) { uint32_t s = knoise; s ^= s << 13; s ^= s >> 17; s ^= s << 5; knoise = s; return (double)s / 2147483648.0 - 1; }

static void kick_init(void) { halfband(FA, HA, 7.9); halfband(FB, HB, 7.9); }

static double body_length(const float *p, double slope)
{
  double k = (80 + 20 * d_log10(d_max(1, slope))) / 8.686;
  double body = (p[K_ATK] + p[K_HOLD]) / 1000.0 + (p[K_DECAY] / 1000.0) * d_pow(k, 1 / (1 + 3.0 * p[K_WALL]));
  return d_max(body, p[K_CLICK] > 0 ? p[K_CDEC] / 1000.0 * k : 0);
}

/* The render, in portions: kr_begin sets it up, kr_step does about `budget` units of work and returns 1 when the
   kick is in out[]. The host calls audio, events and drawing from one thread, so a whole render (up to ~50 ms for a
   long, driven kick) in one go would cost the audio blocks around it. os 4 is the real thing; os 1 a quick preview
   while a knob turns (no oversampling, no half-bands). */
typedef struct {
  int phase, os, n, nb, N, i, mode, waved;
  double R, sr, drive, norm, ph0, wk, wn, cdk, clickK, ctype, ph, ce, ctPh, amp, pitch, ctInc, tailOct, tailK, peak, g;
  int fo;
  dbq pre, deEmph, post, cbp, lc0, lc1, tone;
  kcurves cv;
  float p[K_COUNT];
  float *out;
} kr_state;
static kr_state KR;

static void kr_begin(const float *pp, double sr, float *out, int os)
{
  kr_state *k = &KR;
  for (int i = 0; i < K_COUNT; i++) k->p[i] = pp[i];
  const float *p = k->p;
  k->os = os; k->sr = sr; k->out = out; k->R = sr * os;
  k->drive = 0.3 * d_pow(10, p[K_DRIVE] / 20.0);
  k->mode = (int)(p[K_MODE] + 0.5f);
  k->norm = k->mode == 2 ? 1 : 1 / shape(k->mode, k->drive);
  double slope = k->drive * k->norm * (k->mode == 2 ? D_PI / 2 : k->mode == 1 ? 0.886 : 1);
  k->n = (int)(kick_length(p) * sr + 0.999);
  if (k->n > KICK_MAX) k->n = KICK_MAX;
  k->nb = (int)((body_length(p, slope) + 0.005) * sr + 0.999);
  if (k->nb > k->n) k->nb = k->n;
  if (k->nb > BODY_MAX) k->nb = BODY_MAX;
  k->N = k->nb * os;
  for (int i = 0; i < PAD; i++) kx[i] = 0;                       /* the padding around the body; the body is written */
  for (int i = PAD + k->N; i < PAD + k->N + 2 * PAD + 8; i++) kx[i] = 0;
  knoise = 0x6b69636b;
  dbq_set(&k->pre, BQ_PEAK, p[K_FOCUS], k->R, 0.7, p[K_EMPH]);
  dbq_set(&k->deEmph, BQ_PEAK, p[K_FOCUS], k->R, 0.7, -p[K_EMPH]);
  dbq_set(&k->post, BQ_LP, p[K_POST], k->R, 0.707, 0);
  dbq_set(&k->cbp, BQ_BP, p[K_CTONE], k->R, 0.8, 0);
  k->ph0 = p[K_PHASE] * D_PI / 2;
  k->wk = 0.01 + p[K_WAVE] * 8; k->wn = 1 / d_tanh(k->wk);
  k->cdk = d_exp(-1 / (p[K_CDEC] / 1000.0 * k->R));
  kc_init(&k->cv, p);
  k->clickK = p[K_CLICK]; k->ctype = p[K_CTYPE]; k->waved = p[K_WAVE] > 0;
  k->ph = 0; k->ce = 1; k->ctPh = 0; k->amp = 0; k->pitch = 0; k->ctInc = p[K_CTONE] / k->R;
  dbq_set(&k->lc0, BQ_HP, p[K_LOWCUT], sr, 0.54, 0);
  dbq_set(&k->lc1, BQ_HP, p[K_LOWCUT], sr, 1.31, 0);
  dbq_set(&k->tone, BQ_LP, p[K_TONE], sr, 0.707, 0);
  k->tailOct = p[K_TOCT] >= 0.5f ? 0.5 : 1; k->tailK = p[K_TAIL] * 0.9;
  k->peak = 1e-9;
  k->phase = 1; k->i = 0;
}

static int kr_step(long budget)
{
  kr_state *k = &KR;
  const int OS = k->os;
  while (budget > 0) {
    switch (k->phase) {
    case 1:                                                      /* the driven body, at os × */
      for (; k->i < k->N && budget > 0; k->i++, budget -= 4) {
        int i = k->i;
        if ((i & 15) == 0) { double t = i / k->R; k->pitch = kc_pitch(&k->cv, t); k->amp = kc_amp(&k->cv, t); }
        if ((i & (OS - 1)) == 0) kphs[i / OS] = k->ph;
        k->ph += k->pitch / k->R;
        double sn = d_sin(D_TAU * k->ph + k->ph0);
        double body = (k->waved ? d_tanh(k->wk * sn) * k->wn : sn) * k->amp;
        double click = 0;
        if (k->clickK > 0 && k->ce > 1e-5) {
          k->ctPh += k->ctInc;
          double tn = d_sin(D_TAU * k->ctPh), ns = dbq_run(&k->cbp, knoise_next()) * 4;
          click = (ns + (tn - ns) * k->ctype) * k->ce * k->clickK;
          k->ce *= k->cdk;
        }
        double y = dbq_run(&k->deEmph, shape(k->mode, dbq_run(&k->pre, body + click) * k->drive) * k->norm);
        kx[i + PAD] = (float)dbq_run(&k->post, y);
      }
      if (k->i >= k->N) { k->phase = 2; k->i = k->nb; }
      break;
    case 2:                                                      /* the phase on for the tail */
      for (; k->i < k->n && budget > 0; k->i++, budget -= 2) { kphs[k->i] = k->ph; k->ph += kc_pitch(&k->cv, k->i / k->sr) / k->sr; }
      if (k->i >= k->n) { k->phase = OS == 4 ? 3 : 5; k->i = 0; }
      break;
    case 3: {                                                    /* half-band 1: 4× → 2× */
      int m = 2 * k->nb + 2 * HB + 1;
      for (; k->i < m && budget > 0; k->i++, budget -= 3) {
        int o = 2 * k->i; double acc = FA[HA] * kx[o + HA];
        for (int q = 1; q <= 2 * HA; q += 2) acc += FA[q] * kx[o + q];
        kh1[k->i] = (float)acc;
      }
      if (k->i >= m) { k->phase = 4; k->i = 0; }
      break; }
    case 4:                                                      /* half-band 2: 2× → 1× */
      for (; k->i < k->nb && budget > 0; k->i++, budget -= 8) {
        int o = 2 * k->i; double acc = FB[HB] * kh1[o + HB];
        for (int q = 1; q <= 2 * HB; q += 2) acc += FB[q] * kh1[o + q];
        kh2[k->i] = (float)acc;
      }
      if (k->i >= k->nb) { k->phase = 5; k->i = 0; }
      break;
    case 5:                                                      /* body + sub tail, gate, low cut, tone */
      for (; k->i < k->n && budget > 0; k->i++, budget -= 4) {
        int j = k->i; double t = j / k->sr;
        double y = j < k->nb ? (OS == 4 ? kh2[j] : kx[PAD + j]) : 0;
        if (k->tailK > 0) y += d_sin(D_TAU * kphs[j] * k->tailOct + k->ph0) * kc_tail(&k->cv, t) * k->tailK;
        y = dbq_run(&k->tone, dbq_run(&k->lc1, dbq_run(&k->lc0, y * kc_gate(&k->cv, t))));
        k->out[j] = (float)y;
        if (d_abs(y) > k->peak) k->peak = d_abs(y);
      }
      if (k->i >= k->n) {
        k->g = d_pow(10, k->p[K_LEVEL] / 20.0) / k->peak;
        k->fo = (int)(k->sr * 0.003); if (k->fo > k->n) k->fo = k->n;
        k->phase = 6; k->i = 0;
      }
      break;
    case 6: {                                                    /* level, and a 3 ms fade at the end */
      int n = k->n, fo = k->fo;
      for (; k->i < n && budget > 0; k->i++, budget -= 1) {
        int j = k->i;
        k->out[j] = (float)(k->out[j] * k->g * (j >= n - fo ? (double)(n - 1 - j) / fo : 1));
      }
      if (k->i >= n) { k->phase = 0; return 1; }
      break; }
    default: return 1;
    }
  }
  return 0;
}

/* the whole render at once: for init, where nothing plays yet */
static int render_kick(const float *p, double sr, float *out)
{
  kr_begin(p, sr, out, 4);
  while (!kr_step(1L << 30)) {}
  return KR.n;
}
