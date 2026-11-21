/* SPDX-License-Identifier: GPL-3.0-or-later
 * KRACHT – hard techno drums for VIBE FM.
 *
 *   KICK    a kick designer, rendered offline at x4 (no aliasing however hard it is driven); presets EXHALE,
 *           KRACHT, PSY, HOUSE, RUMBLE, ZAP, INDUSTRIAL
 *   RUMBLE  the kick into a long dark hall, cut to the bottom octave, saturated, and ducked by the kick itself –
 *           sample-exact. The hall is a convolution baked per kick (FFT), so a hit plays a ready rumble one-shot and
 *           only the saturation, the low-pass and the duck run live
 *   DRUMS   closed and open hat, clap: synthesised voices
 *   SEQ     16 steps per lane, a pattern book, swing; the DAW's song position when it plays, else its own tempo
 *   SIDE    the VIBE touch: IN 1 + 2 run through and are ducked by the kick – put it on a bass or a pad track as a
 *           sidechain; the kick also fires GATE OUT
 *
 * Pages (K keys): K1 KICK · K2 PUNCH · K3 DRUMS · K4 SIDE · K5 RUMBLE · K6 SEQ. Pads = the 16 steps of the lane chosen
 * with F1-F4 (KICK HAT OPEN CLAP), F5 = next pattern from the book, F6 = next kick preset. PLAY / STOP.
 */
#include "vibe.h"
#include "vibe_dsp.h"
#include "vibe_gfx.h"
#include "kick.h"

void *memset(void *d, int c, size_t n);

VIBE_PARALLEL_AUDIO

/* ---- parameters: the kick's thirty, then the instrument's -------------------------------------------------------- */
enum { P_RLEVEL = K_COUNT, P_RTONE, P_RDRIVE, P_DUCK, P_RELEASE,
       P_HDEC, P_HTONE, P_ODEC, P_CDEC, P_INGAIN, P_SCDEPTH, P_MDRIVE, P_VOLUME,
       P_TEMPO, P_SWING, P_LV0, P_LV1, P_LV2, P_LV3, P_COUNT };
static const char *const PRESET_NAMES[NKP] = { "EXHALE", "KRACHT", "PSY", "HOUSE", "RUMBLE", "ZAP", "INDUSTRIAL" };
static vibe_param PARAMS[P_COUNT];
static const vibe_param MORE[P_COUNT - K_COUNT] = {
  { "rumble",   "Rumble",     0, 1, 0.6f,  "", 0, 0 },
  { "r_tone",   "R tone",     0, 1, 0.3f,  "", 0, 0 },
  { "r_drive",  "R drive",    0, 1, 0.55f, "", 0, 0 },
  { "duck",     "Duck",       0, 1, 0.85f, "", 0, 0 },
  { "release",  "Release",    0.1f, 1, 0.5f, "beats", 0, 0 },
  { "h_decay",  "Hat decay",  12, 700, 42, "ms", 0, 0 },
  { "h_tone",   "Hat tone",   0, 1, 0.5f,  "", 0, 0 },
  { "o_decay",  "Open decay", 40, 900, 230, "ms", 0, 0 },
  { "c_decay",  "Clap decay", 40, 700, 190, "ms", 0, 0 },
  { "in_gain",  "Input",      0, 1, 0.0f,  "", 0, 0 },
  { "sc_depth", "Sidechain",  0, 1, 0.8f,  "", 0, 0 },
  { "m_drive",  "Glue",       0, 1, 0.25f, "", 0, 0 },
  { "volume",   "Volume",     0, 1, 0.85f, "", 0, 0 },
  { "tempo",    "Tempo",      120, 170, 150, "bpm", 0, 0 },
  { "swing",    "Swing",      0, 0.5f, 0,  "", 0, 0 },
  { "lv_kick",  "Kick",       0, 1, 0.85f, "", 0, 0 },
  { "lv_hat",   "Hat",        0, 1, 0.6f,  "", 0, 0 },
  { "lv_open",  "Open",       0, 1, 0.5f,  "", 0, 0 },
  { "lv_clap",  "Clap",       0, 1, 0.6f,  "", 0, 0 },
};
static const char *GROUPS[P_COUNT];
static float kp[K_COUNT];                     /* the kick's values as the control flow renders them */
static float P(int i) { return vibe_param_get(i); }

/* ---- the pattern book ----------------------------------------------------------------------------------------- */
#define LANES 4
enum { L_KICK, L_HAT, L_OPEN, L_CLAP };
static const char *const LANE_NAME[LANES] = { "KICK", "HAT", "OPEN", "CLAP" };
typedef struct { const char *name, *s; } book_t;
static const book_t BOOK_KICK[] = { { "four", "X...X...X...X..." }, { "four, pickup", "X...X...X...X.o." },
  { "rolling", "X..oX...X..oX..." }, { "gallop", "X..xX...X..xX..x" }, { "double end", "X...X...X...X.x." },
  { "broken", "X...X...X..xX..." }, { "half", "X.......X......." }, { "out", "................" } };
static const book_t BOOK_HAT[] = { { "offbeat", "..x...x...x...x." }, { "sixteenths", "oxXxoxXxoxXxoxXx" },
  { "eighths", "x.x.x.x.x.x.x.x." }, { "offbeat + ghost", "..x.o.x...x.o.x." }, { "gallop", "..xo..xo..xo..xo" },
  { "rolling", "o.x.oox.o.x.oox." }, { "drive", "xxXxxxXxxxXxxxXx" }, { "out", "................" } };
static const book_t BOOK_OPEN[] = { { "offbeat", "..x...x...x...x." }, { "offbeat accent", "..X...X...X...Xo" },
  { "every other", "..x.......x....." }, { "bar end", "..............x." }, { "offbeat + 16th", "..x..ox...x..ox." },
  { "ghost run", "..x.o.x.o.x.o.x." }, { "out", "................" } };
static const book_t BOOK_CLAP[] = { { "two & four", "....X.......X..." }, { "two & four, ghost", "....X.......X..o" },
  { "push", "....X..o....X..." }, { "double end", "....X.......X.X." }, { "industrial", "..x...x...x...x." },
  { "four only", "............X..." }, { "every beat", "X...X...X...X..." }, { "out", "................" } };
static const book_t *const BOOK[LANES] = { BOOK_KICK, BOOK_HAT, BOOK_OPEN, BOOK_CLAP };
static const int BOOK_N[LANES] = { 8, 8, 7, 8 };
static volatile float pat[LANES][16];         /* control writes (pads), audio reads – and writes a queued change */
static int book_at[LANES];
static float book_val[LANES][8][16];          /* the book as step values, for the audio flow */
static float step_val(char c) { return c == 'X' ? 1.0f : c == 'x' ? 0.8f : c == 'o' ? 0.42f : c == '?' ? -0.7f : 0.0f; }   /* <0: 50 % */
static void load_pattern(int lane, int i)
{
  for (int k = 0; k < 16; k++) pat[lane][k] = step_val(BOOK[lane][i].s[k]);
  book_at[lane] = i;
}
/* live: a pattern change (F5) and a mute (SHIFT + F1-F4) wait for the next bar's first step; the audio flow applies
   them there, to the sample. Stopped, they apply at once. */
static volatile int32_t q_seq[LANES], q_idx[LANES], q_done[LANES];   /* control: idx then seq; audio: done = seq */
static volatile int32_t mute_req[LANES], mute_act[LANES];

/* ---- the rumble's hall: an FFT convolution per kick ------------------------------------------------------------ */
#define FFT_MAX_LOG 19
#define FFT_MAX (1 << FFT_MAX_LOG)
#define SLOTS 3
static float re[FFT_MAX], im[FFT_MAX], Hre[FFT_MAX], Him[FFT_MAX];
static float tw_c[FFT_MAX / 2], tw_s[FFT_MAX / 2];
static float kick_buf[SLOTS][KICK_MAX];
static float rmb_L[SLOTS][FFT_MAX], rmb_R[SLOTS][FFT_MAX];
static int kick_len_c[SLOTS], rmb_len_c[SLOTS];   /* control's copy */
static volatile int32_t kick_len_pub[SLOTS], rmb_len_pub[SLOTS];
static int H_len, ir_len;
static float SR;

/* The FFT, the hall and the rumble as one job in portions (rj_step does about `budget` butterflies' worth of work,
   returns 1 when the rumble is in its slot) – for the same reason as the kick's: one thread for audio and control. */
typedef struct { uint32_t M, i, j, len, s, k; int inv, bitrev, done; } fft_job;
static void fft_begin(fft_job *f, uint32_t M, int inverse) { f->M = M; f->inv = inverse; f->i = 1; f->j = 0; f->bitrev = 1; f->len = 2; f->s = 0; f->k = 0; f->done = 0; }
static int fft_step(fft_job *f, long *budget)
{
  uint32_t M = f->M;
  if (f->bitrev) {
    for (; f->i < M && *budget > 0; f->i++, (*budget)--) {
      uint32_t i = f->i, j = f->j, bit = M >> 1;
      for (; j & bit; bit >>= 1) j ^= bit;
      j ^= bit; f->j = j;
      if (i < j) { float t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
    }
    if (f->i < M) return 0;
    f->bitrev = 0;
  }
  float sg = f->inv ? 1.0f : -1.0f;
  for (; f->len <= M; f->len <<= 1, f->s = 0, f->k = 0) {
    uint32_t len = f->len, half_ = len >> 1, step = FFT_MAX / len;
    for (; f->s < M; f->s += len, f->k = 0) {
      for (; f->k < half_; f->k++) {
        if (*budget <= 0) return 0;
        (*budget)--;
        uint32_t k = f->k, a = f->s + k, b = a + half_;
        float c = tw_c[k * step], sn = sg * tw_s[k * step];
        float xr = re[b] * c - im[b] * sn, xi = re[b] * sn + im[b] * c;
        re[b] = re[a] - xr; im[b] = im[a] - xi;
        re[a] += xr;        im[a] += xi;
      }
    }
  }
  f->done = 1;
  return 1;
}

/* the hall's impulse (2.6 s, noise darkened at 900 Hz, (1 - t)^1.6), with the rumble's fixed filters baked in
   (low-pass 320 Hz, high-pass 32 Hz: linear, so they belong to the impulse), left in re, right in im, then H = FFT */
typedef struct {
  int phase, slot, n, need, L, len, c, i;
  uint32_t seed, ver;
  double k, y; dbq lp, hp;
  float peak;
  const float *kick;
  fft_job f;
} rumble_job;
static rumble_job RJ;

static void rj_begin(int slot, const float *kick, int n, uint32_t ver)
{
  rumble_job *r = &RJ;
  r->slot = slot; r->kick = kick; r->ver = ver;
  r->need = n + (int)(2.6f * SR); r->L = 1 << 12;
  while (r->L < r->need && r->L < FFT_MAX) r->L <<= 1;
  if (n > r->L - (int)(2.6f * SR)) n = r->L - (int)(2.6f * SR);
  r->n = n;
  r->i = 0; r->c = 0;
  if (r->L != H_len) {                                           /* a new length: the hall first */
    ir_len = (int)(2.6f * SR);
    r->k = d_exp(-D_TAU * 900.0 / SR); r->seed = 0x68616c6c; r->y = 0;
    dbq_set(&r->lp, BQ_LP, 320, SR, 0.7, 0); dbq_set(&r->hp, BQ_HP, 32, SR, 0.7, 0);
    r->phase = 1;
  } else r->phase = 4;
}

static int rj_step(long budget)
{
  rumble_job *r = &RJ;
  int L = r->L;
  while (budget > 0) {
    switch (r->phase) {
    case 1:                                                      /* the hall's impulse, left then right */
      for (; r->c < 2 && budget > 0; ) {
        float *d = r->c ? im : re;
        for (; r->i < L && budget > 0; r->i++) {
          if (r->i < ir_len) {
            uint32_t sd = r->seed; sd ^= sd << 13; sd ^= sd >> 17; sd ^= sd << 5; r->seed = sd;
            double x = (double)sd / 2147483648.0 - 1;
            r->y = r->y * r->k + x * (1 - r->k);
            d[r->i] = (float)dbq_run(&r->hp, dbq_run(&r->lp, r->y * d_pow(1 - (double)r->i / ir_len, 1.6)));
            budget -= 16;
          } else { d[r->i] = (float)dbq_run(&r->hp, dbq_run(&r->lp, 0)); budget -= 2; }
        }
        if (r->i >= L) {
          r->c++; r->i = 0; r->y = 0;
          dbq_set(&r->lp, BQ_LP, 320, SR, 0.7, 0); dbq_set(&r->hp, BQ_HP, 32, SR, 0.7, 0);
        }
      }
      if (r->c >= 2) { fft_begin(&r->f, (uint32_t)L, 0); r->phase = 2; }
      break;
    case 2: if (fft_step(&r->f, &budget)) { r->phase = 3; r->i = 0; } break;
    case 3:
      for (; r->i < L && budget > 0; r->i++, budget--) { Hre[r->i] = re[r->i]; Him[r->i] = im[r->i]; }
      if (r->i >= L) { H_len = L; r->phase = 4; r->i = 0; }
      break;
    case 4:                                                      /* the kick into re */
      for (; r->i < L && budget > 0; r->i++, budget--) { re[r->i] = r->i < r->n ? r->kick[r->i] : 0.0f; im[r->i] = 0.0f; }
      if (r->i >= L) { fft_begin(&r->f, (uint32_t)L, 0); r->phase = 5; }
      break;
    case 5: if (fft_step(&r->f, &budget)) { r->phase = 6; r->i = 0; } break;
    case 6:                                                      /* Y = K * H: real part = kick*irL, imag = kick*irR */
      for (; r->i < L && budget > 0; r->i++, budget--) {
        float a = re[r->i], b = im[r->i], c = Hre[r->i], d = Him[r->i];
        re[r->i] = a * c - b * d; im[r->i] = a * d + b * c;
      }
      if (r->i >= L) { fft_begin(&r->f, (uint32_t)L, 1); r->phase = 7; }
      break;
    case 7: if (fft_step(&r->f, &budget)) { r->phase = 8; r->i = 0; r->len = r->need < L ? r->need : L; r->peak = 1e-9f; } break;
    case 8:
      for (; r->i < r->len && budget > 0; r->i++, budget--) {
        float a = vd_absf(re[r->i]), b = vd_absf(im[r->i]);
        if (a > r->peak) r->peak = a;
        if (b > r->peak) r->peak = b;
      }
      if (r->i >= r->len) { r->phase = 9; r->i = 0; }
      break;
    case 9: {
      float g = 1.0f / r->peak;
      for (; r->i < r->len && budget > 0; r->i++, budget--) { rmb_L[r->slot][r->i] = re[r->i] * g; rmb_R[r->slot][r->i] = im[r->i] * g; }
      if (r->i >= r->len) { rmb_len_c[r->slot] = r->len; r->phase = 0; return 1; }
      break; }
    default: return 1;
    }
  }
  return 0;
}

/* the whole rumble at once: for init */
static void make_rumble(int slot, const float *kick, int n)
{
  rj_begin(slot, kick, n, 0);
  while (!rj_step(1L << 30)) {}
}

/* ---- hand-over between the flows ------------------------------------------------------------------------------- */
static volatile int32_t cur_kick = -1, cur_rmb = -1;   /* control: the slots new hits play */
static volatile uint32_t kick_busy, rmb_busy;          /* audio: bit per slot a voice still plays */
static volatile int32_t run_req;                       /* control: own clock runs */
static volatile uint32_t start_seq;                    /* control counts up: own clock back to 0 */
#define QN 32
static volatile uint32_t qw, qr;                       /* live hits: control writes q[] then qw */
static struct { int lane; float vel; } q[QN];
static volatile int32_t step_pub = -1;
static volatile float duck_pub, rlev_pub, out_pub, in_pub;

/* ---- the voices (audio flow) ---------------------------------------------------------------------------------- */
typedef struct { float b0, b1, b2, a1, a2, x1, x2, y1, y2; } bq;
static void bq_set(bq *f, int type, float fr, float qq)             /* biquad: 0 low-pass, 1 high-pass, 2 band-pass */
{
  double w = D_TAU * d_min(fr, SR * 0.45) / SR, c = d_cos(w), al = d_sin(w) / (2 * qq);
  double b0, b1, b2;
  if (type == 0) { b0 = (1 - c) / 2; b1 = 1 - c; b2 = b0; }
  else if (type == 1) { b0 = (1 + c) / 2; b1 = -(1 + c); b2 = b0; }
  else { b0 = al; b1 = 0; b2 = -al; }
  double a0 = 1 + al;
  f->b0 = (float)(b0 / a0); f->b1 = (float)(b1 / a0); f->b2 = (float)(b2 / a0);
  f->a1 = (float)(-2 * c / a0); f->a2 = (float)((1 - al) / a0);
  f->x1 = f->x2 = f->y1 = f->y2 = 0;
}
static inline float bq_run(bq *f, float x)
{
  float y = f->b0 * x + f->b1 * f->x1 + f->b2 * f->x2 - f->a1 * f->y1 - f->a2 * f->y2;
  f->x2 = f->x1; f->x1 = x; f->y2 = f->y1;
  f->y1 = (y > -1e-20f && y < 1e-20f) ? 0.0f : y;
  return f->y1;
}
static uint32_t ns = 0x9e3779b9;
static inline float noise(void) { ns ^= ns << 13; ns ^= ns >> 17; ns ^= ns << 5; return (float)ns / 2147483648.0f - 1.0f; }
static const float HAT_RATIOS[6] = { 1, 1.4828f, 1.8003f, 2.5461f, 2.6303f, 3.8968f };

#define KV 4
#define RV 8
#define DV 8
typedef struct { int on, slot, len, pos, rel; float vel; } play_v;  /* kick and rumble one-shots; rel: choke fade left */
typedef struct { int on, lane, t, len, nb, bn, sp; float vel, e1, e2, k1, k2, g, phs[6], inc[6]; bq f1, f2; } drum_v;
static play_v KVo[KV], RVo[RV];
static drum_v DVo[DV];
static float duck;
static int duck_atk, gate_left, choke_len = 192;
static bq rlp1, rlp2;
static float rlp_f = -1;
static uint32_t start_done;
static int32_t a_step = -1;
static double own_beat;

static float dk(float sec) { return vd_exp2(-1.442695f / (sec * SR > 1 ? sec * SR : 1)); }

static void hit(int lane, float vel)
{
  if (lane == L_KICK) {
    int s = cur_kick, r = cur_rmb;
    VIBE_ACQUIRE();
    if (s >= 0) {                                              /* one kick at a time: the last one is choked over 4 ms */
      int i = 0;
      for (int j = 0; j < KV; j++) if (KVo[j].on && !KVo[j].rel) KVo[j].rel = choke_len;
      while (i < KV && KVo[i].on) i++; if (i == KV) i = 0;
      KVo[i].on = 1; KVo[i].slot = s; KVo[i].len = kick_len_pub[s]; KVo[i].pos = 0; KVo[i].vel = vel; KVo[i].rel = 0; }
    if (r >= 0) { int i = 0, old = 0; for (; i < RV && RVo[i].on; i++) if (RVo[i].pos > RVo[old].pos) old = i; if (i == RV) i = old;
      RVo[i].on = 1; RVo[i].slot = r; RVo[i].len = rmb_len_pub[r]; RVo[i].pos = 0; RVo[i].vel = vel; }
    duck_atk = (int)(0.002f * SR); gate_left = (int)(0.01f * SR);
    vibe_gate_out(1);
    return;
  }
  if (lane == L_HAT) for (int i = 0; i < DV; i++) if (DVo[i].on && DVo[i].lane == L_OPEN) DVo[i].on = 0;   /* choke */
  int i = 0, old = 0;
  for (; i < DV && DVo[i].on; i++) if (DVo[i].t > DVo[old].t) old = i;
  if (i == DV) i = old;
  drum_v *v = &DVo[i];
  memset(v, 0, sizeof *v);
  v->on = 1; v->lane = lane; v->vel = vel; v->e1 = v->e2 = 1;
  if (lane == L_HAT || lane == L_OPEN) {
    float tone = lane == L_HAT ? P(P_HTONE) : 0.6f, dec = (lane == L_HAT ? P(P_HDEC) : P(P_ODEC)) / 1000.0f;
    float nz = lane == L_HAT ? 0.35f : 0.45f, base = 320 + 260 * tone;
    for (int k = 0; k < 6; k++) { v->phs[k] = (float)(vd_rand() >> 8) / 16777216.0f; v->inc[k] = base * HAT_RATIOS[k] / SR; }
    bq_set(&v->f1, 2, 7000 + 5000 * tone, 1.1f); bq_set(&v->f2, 1, 7000, 0.7f);
    v->len = (int)(dec * 3 * SR); v->k1 = dk(dec / 2.2f); v->g = (1 - nz) / 6; v->e2 = nz;
  } else {
    float dec = P(P_CDEC);
    bq_set(&v->f1, 2, 1150, 1.6f); bq_set(&v->f2, 1, 500, 0.7f);
    v->len = (int)((dec + 60) / 1000.0f * 3 * SR); v->sp = (int)(0.009f * SR); v->nb = 0; v->bn = 0; v->e1 = 0;
    v->k1 = dk(0.0045f); v->k2 = dk(dec / 1000.0f / 2.2f);
  }
}

static float drum(drum_v *v)
{
  float y;
  if (v->lane == L_CLAP) {
    if (v->t == v->nb && v->bn < 4) { v->e1 = v->bn == 3 ? 1.0f : 0.85f; v->bn++; v->nb += v->sp; }
    v->e1 *= v->k1;
    if (v->bn == 4) v->e2 *= v->k2;
    float env = v->bn == 4 ? vd_maxf(v->e1, v->e2 * 0.55f) : v->e1;
    y = bq_run(&v->f2, bq_run(&v->f1, noise())) * env * 3.2f;
  } else {
    float s = 0;
    for (int k = 0; k < 6; k++) { v->phs[k] += v->inc[k]; if (v->phs[k] > 1) v->phs[k] -= 1; s += v->phs[k] < 0.5f ? 1.0f : -1.0f; }
    v->e1 *= v->k1;
    float env = v->t < 24 ? v->e1 * (float)v->t / 24.0f : v->e1;
    y = bq_run(&v->f2, bq_run(&v->f1, s * v->g + noise() * v->e2)) * env * 3.6f;
  }
  if (++v->t >= v->len) v->on = 0;
  return y * v->vel;
}

/* the soft clipper: a wire below the knee, bends above, never past 1 */
static inline float clip_soft(float x)
{
  float a = vd_absf(x);
  if (a <= 0.5f) return x;
  float y = 0.5f + 0.5f * vd_tanh((a - 0.5f) / 0.5f);
  return x < 0 ? -y : y;
}

void vibe_on_audio(const float *in, float *out_l, float *out_r, int frames)
{
  const float *i1 = vibe_input(0), *i2 = vibe_input(1);
  if (!i1) i1 = in;
  /* the clock */
  uint32_t st = start_seq;
  if (st != start_done) { own_beat = 0; a_step = -1; start_done = st; }
  const vibe_transport_info *tp = vibe_transport();
  int host = tp->has && tp->playing && tp->bpm > 0.0f;
  int running = host || run_req;
  float bpm = host ? tp->bpm : P(P_TEMPO);
  double beat = host ? tp->beat : own_beat, dbeat = (double)bpm / 60.0 / (double)SR;
  float swing = P(P_SWING);
  /* live hits */
  uint32_t w = qw;
  VIBE_ACQUIRE();
  for (uint32_t k = qr; k != w; k++) hit(q[k % QN].lane, q[k % QN].vel);
  VIBE_RELEASE(); qr = w;

  float lv[LANES] = { P(P_LV0), P(P_LV1), P(P_LV2), P(P_LV3) };
  for (int i = 0; i < LANES; i++) lv[i] *= lv[i];
  float rlev = P(P_RLEVEL); rlev = rlev * rlev * 1.6f;
  float tf = 70.0f + P(P_RTONE) * 220.0f;
  if (tf != rlp_f) {                                             /* coefficients only, the state stays */
    bq a = rlp1, b = rlp2; bq_set(&rlp1, 0, tf, 0.6f); bq_set(&rlp2, 0, tf, 0.6f);
    rlp1.x1 = a.x1; rlp1.x2 = a.x2; rlp1.y1 = a.y1; rlp1.y2 = a.y2; rlp2.x1 = b.x1; rlp2.x2 = b.x2; rlp2.y1 = b.y1; rlp2.y2 = b.y2;
    rlp_f = tf;
  }
  /* rumble drive: up to +12 dB into the clipper, half given back */
  float push = vd_exp2(P(P_RDRIVE) * 12.0f / 6.0206f), gin = 0.3f * push, gout = 1.0f / vd_sqrt(push);
  float depth = P(P_DUCK), beat_frames = 60.0f / bpm * SR;
  float rel = P(P_RELEASE) * beat_frames; if (rel < 64) rel = 64;
  float dcoef = vd_exp2(-4.6f * 1.442695f / rel);
  float ing = P(P_INGAIN), scd = P(P_SCDEPTH);
  float md = P(P_MDRIVE), mpre = 1.0f + md * 3.0f, mpost = 1.0f / (1.0f + md * 1.2f);
  float vol = P(P_VOLUME); vol *= vol;
  float rpk = 0, opk = 0, ipk = 0;
  uint32_t kb = 0, rb = 0;

  for (int n = 0; n < frames; n++) {
    if (running && beat >= 0.0) {                                /* the step, with swing on the odd sixteenths */
      double t16 = beat * 4.0;
      int32_t s = (int32_t)t16;
      if ((s & 1) && (float)(t16 - (double)s) < swing * 0.66f) s--;
      if (s != a_step) {
        a_step = s;
        int k = s & 15;
        if (k == 0) {                                            /* the bar's first step: queued changes land here */
          VIBE_ACQUIRE();
          for (int l = 0; l < LANES; l++) {
            int32_t sq = q_seq[l];
            if (sq != q_done[l]) { const float *b = book_val[l][q_idx[l]]; for (int j = 0; j < 16; j++) pat[l][j] = b[j]; q_done[l] = sq; }
            mute_act[l] = mute_req[l];
          }
        }
        for (int l = 0; l < LANES; l++) {
          if (mute_act[l]) continue;
          float v = pat[l][k];
          if (v < 0) v = (vd_rand() & 1) ? -v : 0.0f;
          if (v > 0) hit(l, v);
        }
        step_pub = s;
      }
    }
    /* the duck: 2 ms up, then down over RELEASE beats */
    if (duck_atk > 0) { duck += (1.0f - duck) * 0.08f; duck_atk--; } else duck *= dcoef;
    if (gate_left > 0 && --gate_left == 0) vibe_gate_out(0);

    float kick = 0, rl = 0, rr = 0, dr = 0;
    for (int i = 0; i < KV; i++) if (KVo[i].on) {
      play_v *v = &KVo[i];
      float kg = v->vel;
      if (v->rel) { kg *= (float)v->rel / (float)choke_len; if (--v->rel == 0) v->on = 0; }
      kick += kick_buf[v->slot][v->pos] * kg;
      kb |= 1u << v->slot;
      if (++v->pos >= v->len) v->on = 0;
    }
    for (int i = 0; i < RV; i++) if (RVo[i].on) {
      play_v *v = &RVo[i];
      rl += rmb_L[v->slot][v->pos] * v->vel; rr += rmb_R[v->slot][v->pos] * v->vel;
      rb |= 1u << v->slot;
      if (++v->pos >= v->len) v->on = 0;
    }
    for (int i = 0; i < DV; i++) if (DVo[i].on) dr += drum(&DVo[i]) * lv[DVo[i].lane];
    /* the rumble: saturate, low-pass twice (the harmonics back out), duck */
    float rm = (rl + rr) * 0.5f, side = (rl - rr) * 0.5f;
    float sat = clip_soft(rm * gin) * gout;
    float low = bq_run(&rlp2, bq_run(&rlp1, sat));
    float g = (1.0f - depth * duck) * rlev;
    float rL = (low + side * 0.15f * gout) * g, rR = (low - side * 0.15f * gout) * g;
    if (vd_absf(low * g) > rpk) rpk = vd_absf(low * g);
    /* the input through the sidechain */
    float xl = 0, xr = 0;
    if (ing > 0.0f) {
      float a = i1 ? i1[n] : 0.0f, b = i2 ? i2[n] : a;
      float sg = ing * ing * 2.0f * (1.0f - scd * duck);
      xl = a * sg; xr = b * sg;
      if (vd_absf(a) > ipk) ipk = vd_absf(a);
    } else if (i1 && vd_absf(i1[n]) > ipk) ipk = vd_absf(i1[n]);
    float k = kick * lv[L_KICK];
    float L = (k + dr + rL + xl) * mpre, R = (k + dr + rR + xr) * mpre;
    L = vd_tanh(L) * mpost * vol; R = vd_tanh(R) * mpost * vol;
    out_l[n] = L; out_r[n] = R;
    float a = vd_maxf(vd_absf(L), vd_absf(R)); if (a > opk) opk = a;
    beat += dbeat;
  }
  if (!host && run_req) own_beat = beat;
  kick_busy = kb; rmb_busy = rb;
  duck_pub = duck; rlev_pub = rpk; out_pub = opk; in_pub = ipk;
}

/* ---- control flow -------------------------------------------------------------------------------------------- */
enum { PG_KICK, PG_PUNCH, PG_DRUMS, PG_SIDE, PG_RUMBLE, PG_SEQ, PG_COUNT };
static const char *const PAGE_NAME[PG_COUNT] = { "KICK", "PUNCH", "DRUMS", "SIDE", "RUMBLE", "SEQ" };
static const int MAP[PG_COUNT][4] = {
  { K_TUNE, K_FROM, K_SWEEP, K_DECAY },
  { K_CLICK, K_DRIVE, K_MODE, K_TAIL },
  { P_HDEC, P_HTONE, P_ODEC, P_CDEC },
  { P_INGAIN, P_SCDEPTH, P_MDRIVE, P_VOLUME },
  { P_RLEVEL, P_RTONE, P_RDRIVE, P_DUCK },
  { P_TEMPO, P_SWING, -1, -1 } };          /* SEQ: 3 = the lane's level, (4 free) */
static int page, lane, preset, help, running_c;
static uint32_t kick_ver = 1, kick_done, rmb_ver;     /* kick values changed / rendered / rumble made for */
static uint64_t last_kick_change, last_render;
static int calls;
static float wave[200];                               /* the kick's picture */
static uint32_t pad_shown[16];

static int free_slot(int cur, uint32_t busy)
{
  for (int i = 0; i < SLOTS; i++) if (i != cur && !(busy & (1u << i))) return i;
  return -1;
}

static void picture(int s)
{
  int n = kick_len_c[s], span = (int)(0.6f * SR);
  for (int x = 0; x < 200; x++) {
    int a = x * span / 200, b = (x + 1) * span / 200;
    float m = 0;
    for (int i = a; i < b && i < n; i++) if (vd_absf(kick_buf[s][i]) > m) m = vd_absf(kick_buf[s][i]);
    wave[x] = m;
  }
}

/* The kick and the rumble are made in portions, one per audio block (the host runs audio, events and drawing on one
   thread): while a knob turns a quick preview (no oversampling), 250 ms after it rests the real kick at 4×, 300 ms
   after that the rumble. */
#define WORK_BUDGET 40000L                                   /* ~0.5–0.9 ms native per portion */
static int kr_slot = -1, kr_os, kick_final = 1, rj_on;
static uint32_t kr_ver;
static uint64_t last_work;

static void service(void)
{
  uint64_t now = vibe_frames();
  calls++;
  if (now == last_work && calls < 8) return;                  /* one portion per audio block (or every 8 calls without audio) */
  last_work = now; calls = 0;

  if (kr_slot >= 0) {                                          /* a kick in the making */
    if (kr_step(WORK_BUDGET)) {
      int s = kr_slot;
      kick_len_c[s] = KR.n;
      kick_len_pub[s] = kick_len_c[s];
      VIBE_RELEASE();
      cur_kick = s;
      kick_done = kr_ver; kick_final = kr_os == 4; last_render = now;
      kr_slot = -1;
      picture(s);
    }
    return;
  }
  if (rj_on) {                                                 /* a rumble in the making: dropped if the kick moved on */
    if (kick_ver != RJ.ver || !kick_final) { rj_on = 0; return; }
    if (rj_step(WORK_BUDGET)) {
      rmb_len_pub[RJ.slot] = rmb_len_c[RJ.slot];
      VIBE_RELEASE();
      cur_rmb = RJ.slot;
      rmb_ver = RJ.ver;
      rj_on = 0;
    }
    return;
  }
  int turning = now - last_kick_change < (uint64_t)(SR * 0.25f);
  if (kick_ver != kick_done || (!kick_final && !turning)) {
    int s = free_slot(cur_kick, kick_busy);
    if (s >= 0) {
      for (int i = 0; i < K_COUNT; i++) kp[i] = P(i);
      kr_os = turning ? 1 : 4;
      kr_ver = kick_ver;
      kr_begin(kp, SR, kick_buf[s], kr_os);
      kr_slot = s;
    }
    return;
  }
  if (kick_final && rmb_ver != kick_done && now - last_kick_change > (uint64_t)(SR * 0.55f)) {
    int s = free_slot(cur_rmb, rmb_busy);
    if (s >= 0 && cur_kick >= 0) { rj_begin(s, kick_buf[cur_kick], kick_len_c[cur_kick], kick_done); rj_on = 1; }
  }
}

static void kick_changed(void) { kick_ver++; last_kick_change = vibe_frames(); }

static void queue_hit(int l, float vel)
{
  uint32_t w = qw;
  if (w - qr >= QN) return;
  q[w % QN].lane = l; q[w % QN].vel = vel;
  VIBE_RELEASE();
  qw = w + 1;
}

static void set_preset(int i)
{
  float p[K_COUNT];
  preset = (i % NKP + NKP) % NKP;
  kick_preset_apply(preset, p);
  for (int k = 0; k < K_COUNT; k++) vibe_param_set(k, p[k]);
  kick_changed();
}

static void labels(void)
{
  static const char *const F[6] = { "KICK", "HAT", "OPEN", "CLAP", "PATTRN", "PRESET" };
  for (int i = 0; i < 6; i++) vibe_label(VB_F1 + i, F[i]);
  for (int i = 0; i < 6; i++) { vibe_label(VB_K1 + i, PAGE_NAME[i]); vibe_led_button(VB_K1 + i, 0xFF3D14, i == page ? 1.0f : 0.0f); }
}

void vibe_on_init(float sample_rate)
{
  SR = sample_rate > SR_MAX ? SR_MAX : sample_rate;
  choke_len = (int)(0.004f * SR);
  for (int k = 0; k < FFT_MAX / 2; k++) { double a = D_TAU * k / FFT_MAX; tw_c[k] = (float)d_cos(a); tw_s[k] = (float)d_sin(a); }
  kick_init();
  for (int i = 0; i < K_COUNT; i++) { PARAMS[i] = KICK_SPECS[i]; GROUPS[i] = i < K_ATK ? "A · BODY" : i < K_CLICK ? "A · AMP" :
    i < K_TAIL ? "A · CLICK" : i < K_DRIVE ? "A · TAIL" : i < K_LOWCUT ? "A · DRIVE" : "A · OUT"; }
  for (int i = K_COUNT; i < P_COUNT; i++) { PARAMS[i] = MORE[i - K_COUNT]; GROUPS[i] = i <= P_RELEASE ? "B · RUMBLE" :
    i <= P_CDEC ? "B · DRUMS" : i <= P_VOLUME ? "B · SIDE" : "B · SEQ"; }
  vibe_notes(VIBE_NOTES);
  vibe_params(PARAMS, P_COUNT);
  vibe_param_groups(GROUPS);
  vibe_request(VIBE_REQ_AUDIO_INPUT);
  float p[K_COUNT];
  kick_preset_apply(0, p);                                       /* EXHALE */
  for (int k = 0; k < K_COUNT; k++) vibe_param_set(k, p[k]);
  for (int l = 0; l < LANES; l++) for (int i = 0; i < BOOK_N[l]; i++) for (int k = 0; k < 16; k++) book_val[l][i][k] = step_val(BOOK[l][i].s[k]);
  for (int l = 0; l < LANES; l++) load_pattern(l, 0);
  /* ready before the first hit: kick and rumble */
  for (int i = 0; i < K_COUNT; i++) kp[i] = P(i);
  kick_len_c[0] = render_kick(kp, SR, kick_buf[0]); kick_len_pub[0] = kick_len_c[0];
  make_rumble(0, kick_buf[0], kick_len_c[0]); rmb_len_pub[0] = rmb_len_c[0];
  cur_kick = 0; cur_rmb = 0; kick_done = kick_ver; rmb_ver = kick_ver;
  picture(0);
  labels();
}

static const char *const HELP[] = {          /* short: the overview cuts each value at ~17 characters */
  "PADS", "16 steps x X o off",
  "F1-F4", "lane",
  "SHIFT + F1-F4", "mute, next bar",
  "F5 / SHIFT+F5", "pattern, next bar",
  "F6", "kick preset",
  "KEYS / MIDI", "C kick D/E clap",
  "K1-K6", "the six pages",
  "ENCODERS 1-4", "the page's values",
  "ENCODER A / B", "volume / release",
  "PLAY / STOP", "the sequencer",
  "IN 1+2", "ducked by the kick" };

static int sel_param(int slot) { int p = MAP[page][slot]; if (page == PG_SEQ && slot == 3) p = P_LV0 + lane; return p; }

static void encoder(int id, float v)
{
  int p = id == VE_A ? P_VOLUME : id == VE_B ? P_RELEASE : (id <= VE_4 ? sel_param(id) : -1);
  if (p < 0) return;
  const vibe_param *d = &PARAMS[p];
  if (d->steps) {
    static float acc; acc += v;
    if (acc >= 1 || acc <= -1) { int nv = (int)(P(p) + 0.5f) + (acc > 0 ? 1 : -1); acc = 0;
      if (nv < 0) nv = 0; if (nv >= d->steps) nv = d->steps - 1; vibe_param_set(p, (float)nv); }
  } else {
    float span = d->max - d->min;
    int logish = p == K_FROM || p == K_SWEEP || p == K_DECAY || p == K_CTONE || p == K_TDEC || p == K_FOCUS || p == K_POST;
    if (logish) { float x = P(p) * vd_exp2(v * 0.04f); vibe_param_set(p, vd_clamp(x, d->min, d->max)); }
    else vibe_param_set(p, vd_clamp(P(p) + v * span / (p == K_TUNE ? 120.0f : 60.0f), d->min, d->max));
  }
  if (p < K_COUNT) kick_changed();
}

static int shift, q_book[LANES] = { -1, -1, -1, -1 };   /* q_book: the entry waiting for the bar, -1 none */
static int seq_on(void) { const vibe_transport_info *t = vibe_transport(); return running_c || (t->has && t->playing && t->bpm > 0.0f); }
static int pending(int l) { return q_book[l] >= 0 && q_seq[l] != q_done[l]; }
static void live_sync(void)                     /* a queued pattern the audio flow has applied becomes the current one */
{
  int on = seq_on();
  for (int l = 0; l < LANES; l++) {
    if (q_book[l] >= 0 && (q_seq[l] == q_done[l] || !on)) { if (!on) load_pattern(l, q_book[l]); book_at[l] = q_book[l]; q_book[l] = -1; q_done[l] = q_seq[l]; }
    if (!on) mute_act[l] = mute_req[l];                          /* stopped: nothing waits for a bar */
  }
}
static void queue_pattern(int l, int i)
{
  if (!seq_on()) { load_pattern(l, i); q_book[l] = -1; q_done[l] = q_seq[l]; return; }
  q_book[l] = i; q_idx[l] = i;
  VIBE_RELEASE();
  q_seq[l] = q_seq[l] + 1;
}
static void toggle_mute(int l)
{
  mute_req[l] = !mute_req[l];
  if (!seq_on()) mute_act[l] = mute_req[l];
}
void vibe_on_event(const vibe_event *e)
{
  int fid, fr = vf_event(e, &fid);
  if (fr != VF_NO) { service(); return; }
  switch (e->type) {
  case VIBE_EV_BUTTON:
    if (e->id == VB_SHIFT) { shift = e->down; break; }
    if (!e->down) break;
    if (e->id == VB_HOME) { help = !help; return; }
    if (e->id == VB_RETURN) { if (help) help = 0; else { page = PG_KICK; labels(); } return; }
    if (e->id >= VB_K1 && e->id <= VB_K6) { page = e->id - VB_K1; labels(); }
    if (e->id >= VB_F1 && e->id <= VB_F4) { if (shift) toggle_mute(e->id - VB_F1); else lane = e->id - VB_F1; }
    if (e->id == VB_F5) {                                         /* the next (SHIFT: the previous) entry, on the next bar */
      int n = BOOK_N[lane], at = q_book[lane] >= 0 ? q_book[lane] : book_at[lane];
      queue_pattern(lane, (at + (shift ? n - 1 : 1)) % n);
    }
    if (e->id == VB_F6) { set_preset(preset + 1); if (!running_c) queue_hit(L_KICK, 1.0f); }
    if (e->id == VB_PLAY) { if (!running_c) { start_seq = start_seq + 1; VIBE_RELEASE(); run_req = 1; running_c = 1; } else { run_req = 0; running_c = 0; } }
    if (e->id == VB_STOP) { run_req = 0; running_c = 0; }
    break;
  case VIBE_EV_PAD:
    if (e->id < 16 && e->down) {
      float v = pat[lane][e->id];
      pat[lane][e->id] = v == 0 ? 0.8f : v == 0.8f ? 1.0f : v == 1.0f ? 0.42f : 0.0f;
      if (!running_c && pat[lane][e->id] > 0) queue_hit(lane, pat[lane][e->id]);
    }
    break;
  case VIBE_EV_NOTE:
    if (e->down) {
      int pc = e->id % 12, l = pc == 2 || pc == 3 || pc == 4 ? L_CLAP : pc == 6 || pc == 8 ? L_HAT : pc == 10 ? L_OPEN : L_KICK;
      queue_hit(l, e->value > 0 ? e->value : 0.8f);
    }
    break;
  case VIBE_EV_ENCODER: encoder(e->id, e->value); break;
  case VIBE_EV_PARAM: if (e->id < K_COUNT) kick_changed(); break;
  default: break;
  }
  live_sync();
  service();
}

/* ---- the display ------------------------------------------------------------------------------------------------ */
static void val_text(int p, char *buf)
{
  const vibe_param *d = &PARAMS[p];
  float v = P(p);
  if (d->choices) { const char *s = d->choices[(int)(v + 0.5f)]; char *q2 = buf; while (*s) *q2++ = *s++; *q2 = 0; return; }
  if (d->max <= 1.0f && d->min >= 0.0f) { vg_itoa((int)(v * 100 + 0.5f), buf); return; }
  if (d->max <= 20.0f) { vg_ftoa(v, 1, buf); return; }
  vg_itoa((int)(v + (v < 0 ? -0.5f : 0.5f)), buf);
}

/* KRACHT's palette, shared with the page */
#define C_BG     0x09090A
#define C_PANEL  0x111113
#define C_RAISED 0x17171A
#define C_EDGE   0x24242A
#define C_EDGEHI 0x34343C
#define C_TEXT   0xD9D5CF
#define C_DIM    0x8B8780
#define C_MUTE   0x55534F
#define C_SIGNAL 0xFF3D14
#define C_HOT    0xFFB39C
#define C_WELL   0x050506

static void chevrons(int x, int y, int h, int n, uint32_t c)   /* >>> */
{
  for (int i = 0; i < n; i++) {
    float x0 = (float)(x + i * h * 0.75f);
    vg_line(x0, (float)y, x0 + h * 0.5f, (float)y + h * 0.5f, h * 0.22f, c);
    vg_line(x0 + h * 0.5f, (float)y + h * 0.5f, x0, (float)(y + h), h * 0.22f, c);
  }
}
static void stripes(int x, int y, int w, int h, uint32_t c)     /* hazard stripes //// */
{
  for (int i = 0; i + h <= w; i += 9) vg_line((float)(x + i), (float)(y + h), (float)(x + i + h), (float)y, 3.5f, c);
}
static void cross(int x, int y, uint32_t c) { vg_rect(x - 4, y, 9, 1, c); vg_rect(x, y - 4, 1, 9, c); }

void vibe_on_draw(uint16_t *fb)
{
  (void)fb;
  service();
  live_sync();
  if (help) { vg_controls("KRACHT", HELP, sizeof HELP / sizeof HELP[0] / 2); return; }
  char buf[24];
  const vibe_transport_info *tp = vibe_transport();
  int host = tp->has && tp->playing;
  vg_clear(C_BG);

  /* header: the wordmark, the pages */
  vg_text("KRACHT", 14, 3, VG_BIG, C_TEXT, VG_LEFT);
  vg_text("HARD TECHNO", 108, 4, VG_BOLD, C_SIGNAL, VG_LEFT);
  vg_text("DRUM MACHINE", 108, 17, VG_SMALL, C_MUTE, VG_LEFT);
  { int w = 0; for (int i = 0; i < PG_COUNT; i++) w += vg_text_width(PAGE_NAME[i], VG_SMALL) + 9;
    int x = 470 - w;
    for (int i = 0; i < PG_COUNT; i++) {
      int tw = vg_text(PAGE_NAME[i], x, 9, VG_SMALL, i == page ? C_TEXT : C_MUTE, VG_LEFT);
      if (i == page) vg_rect(x, 26, tw, 2, C_SIGNAL);
      x += tw + 9;
    } }
  vg_rect(0, 31, 480, 1, C_EDGE);

  /* the plate: preset, tempo, the kick's first 600 ms – black on signal */
  int px = 14, py = 38, pw = 282, ph = 74;
  vg_round(px, py, pw, ph, 4, C_SIGNAL);
  vg_text(PRESET_NAMES[preset], px + 10, py + 5, VG_BIG, C_BG, VG_LEFT);
  { char bb[16], *q2 = bb; const char *u = vg_itoa((int)((host ? tp->bpm : P(P_TEMPO)) + 0.5f), buf);
    while (*u) *q2++ = *u++; u = host ? " BPM DAW" : " BPM"; while (*u) *q2++ = *u++; *q2 = 0;
    vg_text(bb, px + pw - 10, py + 7, VG_BOLD, C_BG, VG_RIGHT); }
  chevrons(px + pw - 100, py + 9, 12, 3, C_BG);
  for (int x = 0; x < 200; x++) {
    int h = (int)(wave[x] * 18.0f);
    vg_rect(px + 10 + x, py + 52 - h, 1, 2 * h + 1, C_BG);
  }
  stripes(px + pw - 62, py + ph - 22, 54, 14, 0x7A1A08);

  /* the big number: the page */
  buf[0] = '0'; buf[1] = (char)('1' + page); buf[2] = 0;
  vg_text(buf, 304, 36, VG_LOGO, C_TEXT, VG_LEFT);
  /* meters, numbered */
  const char *mn[4] = { "RUMBLE", "DUCK", "IN", "OUT" };
  float mv[4] = { rlev_pub * 2.0f, duck_pub, in_pub * 1.25f, out_pub };
  for (int i = 0; i < 4; i++) {
    int y = 40 + i * 18, x = 384;
    char nb[4] = { '0', (char)('1' + i), 0, 0 };
    vg_text(nb, x, y - 2, VG_SMALL, C_MUTE, VG_LEFT);
    vg_text(mn[i], x + 16, y, VG_SMALL, C_DIM, VG_LEFT);
    vg_rect(x + 70, y + 6, 18, 5, C_EDGE);
    vg_rect(x + 70, y + 6, (int)(18 * vd_clamp(mv[i], 0, 1)), 5, i == 3 && out_pub > 0.98f ? C_HOT : C_SIGNAL);
  }

  /* the steps: raised keys, the signal where they play */
  int step = running_c || host ? (step_pub & 15) : -1;
  int blink = (int)(vibe_frames() / (uint64_t)(SR * 0.18f)) & 1;
  for (int l = 0; l < LANES; l++) {
    int y = 120 + l * 25;
    int mu = mute_act[l], wait = mute_req[l] != mute_act[l];
    if (l == lane) vg_rect(14, y + 2, 3, 16, C_SIGNAL);
    vg_text(LANE_NAME[l], 22, y + 2, VG_BOLD, mu ? C_EDGEHI : l == lane ? C_TEXT : C_MUTE, VG_LEFT);
    if ((mu && !wait) || (wait && blink)) vg_rect(20, y + 9, 40, 2, C_SIGNAL);   /* struck through: muted (blinking: on the next bar) */
    for (int k = 0; k < 16; k++) {
      float v = pat[l][k]; int maybe = v < 0; if (maybe) v = -v;
      int x = 70 + k * 25 + (k / 4) * 4;
      vg_round(x, y, 21, 20, 3, k == step ? C_EDGEHI : C_RAISED);
      if (v > 0) {
        uint32_t c = mu ? (v >= 0.6f ? 0x4A1A0C : 0x2E120A) : v >= 0.95f ? C_SIGNAL : v >= 0.6f ? 0xC22E10 : 0x6E2010;
        if (maybe) { vg_frame(x + 3, y + 3, 15, 14, 2, C_SIGNAL); }
        else vg_round(x + 2, y + 2, 17, 16, 2, c);
      }
      if (k == step) vg_rect(x + 3, y - 3, 15, 2, C_TEXT);
    }
  }
  cross(6, 228, C_SIGNAL); cross(473, 228, C_SIGNAL);
  vg_rect(0, 228, 480, 1, C_EDGE);

  /* the page's encoders: tiny number, name, value, a signal bar */
  for (int i = 0; i < 4; i++) {
    int p = sel_param(i);
    int x = 14 + i * 118;
    char nb[4] = { '0', (char)('1' + i), 0, 0 };
    vg_text(nb, x, 233, VG_SMALL, C_MUTE, VG_LEFT);
    if (p < 0) continue;
    const vibe_param *d = &PARAMS[p];
    float t = (P(p) - d->min) / (d->max - d->min);
    vg_text(d->name, x + 16, 236, VG_SMALL, C_DIM, VG_LEFT);
    val_text(p, buf);
    vg_text(buf, x + 104, 236, VG_BOLD, C_TEXT, VG_RIGHT);
    vg_rect(x, 256, 104, 4, C_EDGE);
    vg_rect(x, 256, (int)(104 * vd_clamp(t, 0, 1)), 4, C_SIGNAL);
  }
  /* soft keys: black keys, a red light over the chosen one */
  static const char *const F[6] = { "KICK", "HAT", "OPEN", "CLAP", "PATTERN", "PRESET" };
  for (int i = 0; i < 6; i++) {
    int x = i * 80, on = i < 4 && i == lane;
    vg_rect(x + 24, 279, 32, 3, on ? C_SIGNAL : 0x3A1208);
    vg_round(x + 4, 286, 72, 30, 4, C_EDGE);
    vg_round(x + 5, 287, 70, 28, 3, on ? C_RAISED : C_PANEL);
    if (i == 4) {                                               /* PATTERN shows the book's name */
      int w = q_book[lane] >= 0;
      char nm[14]; const char *u = BOOK[lane][w ? q_book[lane] : book_at[lane]].name; int j = 0, o = 0;
      if (w) { nm[0] = '>'; nm[1] = ' '; o = 2; }
      while (u[j] && j + o < 11) { nm[j + o] = u[j]; j++; } nm[j + o] = 0;
      if (w) vg_rect(x + 24, 279, 32, 3, blink ? C_SIGNAL : 0x3A1208);   /* waiting for the bar */
      vg_text(nm, x + 40, 290, VG_SMALL, w ? C_SIGNAL : C_TEXT, VG_CENTER);
      vg_text("PATTERN", x + 40, 302, VG_SMALL, C_MUTE, VG_CENTER);
    } else vg_text(F[i], x + 40, 294, VG_BOLD, on ? C_TEXT : C_DIM, VG_CENTER);
  }
  /* the pads: the lane's steps in signal, the playhead white */
  for (int k = 0; k < 16; k++) {
    float v = pat[lane][k]; if (v < 0) v = -v * 0.5f;
    uint32_t col = k == step ? 0xFFFFFF : C_SIGNAL;
    uint32_t key = ((uint32_t)(v * 100) << 1) | (k == step);
    if (pad_shown[k] != key + 1) { vibe_led_pad(k, col, k == step ? 0.6f : v); pad_shown[k] = key + 1; }
  }
  vibe_link_page(PAGE_NAME[page]);
  vf_begin();
  for (int i = 0; i < 4; i++) vf_encoder(VE_1 + i, 14 + i * 118, 236, 104, 26);
  for (int i = 0; i < 6; i++) vf_key(VB_F1 + i, 4 + i * 80, 286, 72, 30);
  vf_end(C_SIGNAL);
}
