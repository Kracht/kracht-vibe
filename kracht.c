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
 *   PERFORM SHIFT + a pad: 16 FX on the drums while it is held (repeat, tape stop, pitch, reverse, filters, delay and
 *           hall throws, crush, no kick) · LEFT: note repeat on the keys · REC: overdub the keys and MIDI into the
 *           steps · SHIFT + a key: the kick tuned to that note
 *   MIDI    notes in (the keys, USB/DIN MIDI, the DAW) by a map – KRACHT, GM, MPC or learned; the sequencer's notes
 *           out to a DAW track or an MPC (sample-exact), and the clock with START / STOP when KRACHT leads
 *
 * Pages (K keys): K1 KICK · K2 PUNCH · K3 DRUMS · K4 SIDE · K5 RUMBLE · K6 SEQ (again: MIDI). Pads = the 16 steps of
 * the lane chosen with F1-F4 (KICK HAT OPEN CLAP), F5 = next pattern from the book, F6 / OCT+ = next kick preset,
 * OCT- = the previous one. PLAY / STOP.
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
       P_TEMPO, P_SWING, P_LV0, P_LV1, P_LV2, P_LV3,
       P_MMAP, P_MINCH, P_MOUTCH, P_MOUT, P_LN0, P_LN1, P_LN2, P_LN3, P_NRATE, P_COUNT };
static const char *const PRESET_NAMES[NKP] = { "EXHALE", "KRACHT", "PSY", "HOUSE", "RUMBLE", "ZAP", "INDUSTRIAL" };
static vibe_param PARAMS[P_COUNT];
static const char *const MAP_NAMES[4] = { "KRACHT", "GM", "MPC", "LEARN" };
static const char *const IN_CH[17] = { "OMNI", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16" };
static const char *const NR_NAMES[6] = { "1/4", "1/8", "1/16", "1/32", "1/8T", "1/16T" };
static const float NR_BEATS[6] = { 1.0f, 0.5f, 0.25f, 0.125f, 1.0f / 3.0f, 1.0f / 6.0f };
static const char *const MOUT_NAMES[4] = { "OFF", "NOTES", "CLOCK", "BOTH" };   /* bit 0 notes, bit 1 clock */
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
  { "midi_map", "Map",        0, 3, 0,     "", 4, MAP_NAMES },
  { "midi_in",  "In ch",      0, 16, 0,    "", 17, IN_CH },
  { "midi_och", "Out ch",     0, 15, 9,    "", 16, IN_CH + 1 },
  { "midi_out", "Out",        0, 3, 0,     "", 4, MOUT_NAMES },
  { "learn_kick", "Learn kick", 0, 127, 36, "", 128, 0 },
  { "learn_hat",  "Learn hat",  0, 127, 42, "", 128, 0 },
  { "learn_open", "Learn open", 0, 127, 46, "", 128, 0 },
  { "learn_clap", "Learn clap", 0, 127, 39, "", 128, 0 },
  { "nr_rate",  "Repeat",     0, 5, 2,     "", 6, NR_NAMES },
};
static const char *GROUPS[P_COUNT];
static float kp[K_COUNT];                     /* the kick's values as the control flow renders them */
static float P(int i) { return vibe_param_get(i); }
static int PI_(int i) { return (int)(vibe_param_get(i) + 0.5f); }

/* ---- the pattern book ----------------------------------------------------------------------------------------- */
#define LANES 4
enum { L_KICK, L_HAT, L_OPEN, L_CLAP };
static const char *const LANE_NAME[LANES] = { "KICK", "HAT", "OPEN", "CLAP" };

/* ---- the MIDI maps: which note plays a lane, which note a lane sends ------------------------------------------
   KRACHT  in: by pitch class, any octave (C kick, D/E clap, F#/G# hat, A# open, the rest kick) · out: C1 D1 F#1 A#1
   GM      in: 35/36 kick, 37-40 clap, 42/44 hat, 46 open · out: 36 39 42 46
   MPC     pads A01-A04 in the chromatic layout: 36 kick, 37 clap, 38 hat, 39 open – in and out
   LEARN   one note a lane (learn_*), in and out. The VIBE's own keys always play by pitch class. */
enum { MAP_KRACHT, MAP_GM, MAP_MPC, MAP_LEARN };
static const uint8_t MAP_OUT[3][LANES] = { { 36, 42, 46, 38 }, { 36, 42, 46, 39 }, { 36, 38, 39, 37 } };
static int key_lane(int note)
{
  int pc = note % 12;
  return pc == 2 || pc == 3 || pc == 4 ? L_CLAP : pc == 6 || pc == 8 ? L_HAT : pc == 10 ? L_OPEN : L_KICK;
}
static int out_note(int map, int l, const float *learned) { return map == MAP_LEARN ? (int)(learned[l] + 0.5f) : MAP_OUT[map][l]; }
static int in_lane(int map, int note, const float *learned)
{
  switch (map) {
  case MAP_KRACHT: return key_lane(note);
  case MAP_GM: return note == 35 || note == 36 ? L_KICK : note >= 37 && note <= 40 ? L_CLAP : note == 42 || note == 44 ? L_HAT : note == 46 ? L_OPEN : -1;
  case MAP_MPC: { static const int8_t L[4] = { L_KICK, L_CLAP, L_HAT, L_OPEN }; return note >= 36 && note <= 39 ? L[note - 36] : -1; }
  default: for (int l = 0; l < LANES; l++) if ((int)(learned[l] + 0.5f) == note) return l; return -1;
  }
}
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

/* ---- performance (control writes, audio reads) ---------------------------------------------------------------
   FX: SHIFT + a pad, as long as the pad is held (tapped twice quickly: latched until tapped again). On the drums –
   kick, rumble, hats, clap – never on IN 1+2, so whatever runs through the sidechain keeps pumping.
     row 1  REPEAT 1 · 1/2 · 1/4 · 1/8 beat           row 2  TAPE STOP · PITCH DOWN · PITCH UP · REVERSE
     row 3  LP SWEEP · HP SWEEP · TELEPHONE · WASH     row 4  DELAY · HALL · CRUSH · NO KICK
   Rows 1-2 play from a ring of the drums, one at a time (the newest held), and start and end on a 16th. The pad's
   velocity is the amount. */
enum { FX_R1, FX_R2, FX_R4, FX_R8, FX_TAPE, FX_PDOWN, FX_PUP, FX_REV, FX_LP, FX_HP, FX_BP, FX_WASH,
       FX_DLY, FX_HALL, FX_CRUSH, FX_NOKICK, FX_N };
static const char *const FX_NAME[FX_N] = { "REPEAT 1", "REPEAT 1/2", "REPEAT 1/4", "REPEAT 1/8", "TAPE STOP", "PITCH DOWN",
  "PITCH UP", "REVERSE", "LP SWEEP", "HP SWEEP", "TELEPHONE", "WASH", "DELAY", "HALL", "CRUSH", "NO KICK" };
static volatile int32_t fx_on[FX_N];
static volatile uint32_t fx_seq[FX_N];        /* press order: the newest ring FX / filter wins */
static volatile float fx_vel[FX_N];
/* note repeat: the VIBE's keys held per lane, while LEFT is on; REC: hits from the keys and MIDI land in the steps */
static volatile int32_t nr_on, nr_held[LANES], rec_on;
static volatile float nr_vel[LANES];

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
#define QH_OUT 1                                        /* send it as MIDI too (the VIBE's own keys, not MIDI in) */
#define QH_REC 2                                        /* played by hand: REC writes it into the steps */
static struct { int lane, flags; float vel; } q[QN];
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

/* ---- the performance FX, in the audio flow ----------------------------------------------------------------------- */
#define RING_N (1 << 18)                       /* 2.7 s at 96 kHz: a beat at 60 bpm fits twice */
#define RING_M (RING_N - 1)
static float ringL[RING_N], ringR[RING_N];
static uint32_t rw, rstart;                    /* write head (counts up), where the slice begins */
static int rmode = -1, rfrozen, rstop, rph, rlen, rt;   /* the ring FX playing, recording held, fading out, slice */
static double rpos;                            /* TAPE: the read head behind the write head */
static float xf, rvel;                         /* live → ring crossfade, the press's velocity */
static inline float ring_rd(const float *b, uint32_t base, double off)
{
  uint32_t i = (uint32_t)off; float f = (float)(off - (double)i);
  float a = b[(base + i) & RING_M], c = b[(base + i + 1) & RING_M];
  return a + (c - a) * f;
}
static void ring_begin(int m, float bf)
{
  rmode = m; rph = 0; rt = 0; rstop = 0; rvel = fx_vel[m]; rfrozen = 0; rstart = rw;
  if (m <= FX_R8) rlen = (int)(bf / (float)(1 << m));               /* 1, 1/2, 1/4, 1/8 beat: the next one, repeated */
  else if (m == FX_PDOWN || m == FX_PUP) rlen = (int)(bf * 0.25f);
  else if (m == FX_REV) { rlen = (int)bf; rstart = rw - (uint32_t)rlen; rfrozen = 1; }   /* the last beat, backwards */
  else rpos = 0.0;                                                  /* TAPE */
  if (rlen < 64) rlen = 64;
  if (rlen > RING_N / 2) rlen = RING_N / 2;
}
static void ring_frame(float *L, float *R, float bf)
{
  float sL = *L, sR = *R, oL, oR;
  if (!rfrozen) { ringL[rw & RING_M] = sL; ringR[rw & RING_M] = sR; rw++; }
  if (rmode < 0) return;
  if (rmode == FX_TAPE) {                                           /* the platter slows to a halt in 2 .. 0.5 beats */
    float r = 1.0f - (float)rt / (bf * (2.0f - 1.5f * rvel)); if (r < 0) r = 0;
    float g = vd_minf(1.0f, r * 4.0f);
    oL = ring_rd(ringL, rstart, rpos) * g; oR = ring_rd(ringR, rstart, rpos) * g;
    rpos += r;
  } else {
    if (rph >= rlen) { rph = 0; rfrozen = 1; }                      /* the first round played live, recorded: now repeat it */
    double off;
    if (rmode == FX_REV) off = (double)(rlen - 1 - rph);
    else {
      float rate = 1.0f, t = vd_minf((float)rt / (bf * 2.0f), 1.0f) * (0.5f + 0.5f * rvel);   /* an octave in 2 beats */
      if (rmode == FX_PDOWN) rate = vd_exp2(-t); else if (rmode == FX_PUP) rate = vd_exp2(t);
      off = (double)rph * rate; while (off >= rlen) off -= rlen;
    }
    if (rfrozen) { oL = ring_rd(ringL, rstart, off); oR = ring_rd(ringR, rstart, off); } else { oL = sL; oR = sR; }
    float env = vd_minf(1.0f, (float)rph / 16.0f) * vd_minf(1.0f, (float)(rlen - rph) / 64.0f);   /* no click at the seams */
    oL *= env; oR *= env; rph++;
  }
  rt++;
  xf += rstop ? -1.0f / 128.0f : 1.0f / 128.0f;
  if (xf > 1) xf = 1;
  if (xf <= 0) { xf = 0; if (rstop) { rmode = -1; rfrozen = 0; rstop = 0; return; } }
  *L = sL + (oL - sL) * xf; *R = sR + (oR - sR) * xf;
}

/* the filter: a state-variable filter (trapezoidal) per side; one mode at a time, the newest held */
typedef struct { float c1, c2; } svf_t;
static svf_t fsL, fsR;
static int fmode = -1, fcnt;
static float famt, ft, fvel, fg = 0.5f, fk = 1.4f;
static inline void svf(svf_t *s, float x, float g, float k, float *lo, float *bp, float *hi)
{
  float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
  float v3 = x - s->c2, v1 = a1 * s->c1 + a2 * v3, v2 = s->c2 + a2 * s->c1 + a3 * v3;
  s->c1 = 2.0f * v1 - s->c1; s->c2 = 2.0f * v2 - s->c2;
  *lo = v2; *bp = v1; *hi = x - k * v1 - v2;
}
static float tan_pi(float fc) { float t = vd_clamp(fc, 10.0f, 0.45f * SR) / SR * 0.5f; return vd_sin(t) / vd_sin(t + 0.25f); }   /* tan(pi fc / SR) */
static void filter_frame(float *L, float *R, int want, float bf)
{
  if (want >= 0 && want != fmode) { fmode = want; ft = 0; fvel = fx_vel[want]; fcnt = 0; }
  if (fmode < 0) return;
  int held = want == fmode;
  if (held) {
    ft += 1.0f;
    famt = fmode <= FX_HP ? vd_minf(1.0f, ft / (2.0f * bf)) : vd_minf(1.0f, ft / (0.03f * SR));   /* sweeps: 2 beats */
  } else if ((famt -= 1.0f / (0.05f * SR)) <= 0) { famt = 0; fmode = -1; fsL.c1 = fsL.c2 = fsR.c1 = fsR.c2 = 0; return; }
  if ((fcnt++ & 7) == 0) {                                          /* the coefficients every 8 frames */
    float a = famt, fc;
    if (fmode == FX_LP) { float t = 150.0f + 450.0f * (1.0f - fvel); fc = 18000.0f * vd_exp2(a * vd_log2(t / 18000.0f)); fk = 1.3f - 0.8f * a; }
    else if (fmode == FX_HP) { float t = 1200.0f + 2800.0f * fvel; fc = 20.0f * vd_exp2(a * vd_log2(t / 20.0f)); fk = 1.3f - 0.8f * a; }
    else if (fmode == FX_BP) { fc = 1300.0f; fk = 1.4f; }
    else { fc = 2400.0f * vd_exp2(vd_minf(1.0f, ft / bf) * vd_log2(350.0f / 2400.0f)); fk = 0.18f; }
    fg = tan_pi(fc);
  }
  float lo, bp, hi, yL, yR;
  svf(&fsL, *L, fg, fk, &lo, &bp, &hi); yL = fmode == FX_LP ? lo : fmode == FX_HP ? hi : fmode == FX_BP ? bp * 2.2f : vd_tanh(lo * 1.5f);
  svf(&fsR, *R, fg, fk, &lo, &bp, &hi); yR = fmode == FX_LP ? lo : fmode == FX_HP ? hi : fmode == FX_BP ? bp * 2.2f : vd_tanh(lo * 1.5f);
  if (fmode <= FX_HP) { *L = yL; *R = yR; }                         /* the sweeps open fully: they are the signal */
  else { *L += (yL - *L) * famt; *R += (yR - *R) * famt; }
}

/* the throws: a ping-pong delay (dotted 8th, dub-filtered feedback) and a dark hall (4 lines, Hadamard); the send
   follows the pad, the tail rings on after it */
#define DLY_N (1 << 17)
#define DLY_M (DLY_N - 1)
#define FDN_N (1 << 14)
#define FDN_M (FDN_N - 1)
static float dlyL[DLY_N], dlyR[DLY_N], fdn[4][FDN_N];
static uint32_t dw, hw;
static float dsend, dvel, dlpL, dlpR, dhpL, dhpR, hsend, hlp[4], hgain[4], hdamp;
static int hlen[4];
static float crL, crR, crph = 1.0f, nkg = 1.0f;
static void fx_init(void)
{
  static const int BASE[4] = { 1931, 2311, 2753, 3163 };
  for (int i = 0; i < 4; i++) {
    hlen[i] = (int)((float)BASE[i] * SR / 48000.0f); if (hlen[i] > FDN_N - 1) hlen[i] = FDN_N - 1;
    hgain[i] = vd_exp2(-3.0f * 3.321928f * (float)hlen[i] / (SR * 2.8f));    /* RT60 2.8 s */
  }
  hdamp = 1.0f - vd_exp2(-1.442695f * 6.2831853f * 3000.0f / SR);
}
static void throws_frame(float *L, float *R, float bf)
{
  float m = (*L + *R) * 0.5f;
  /* delay */
  dsend += ((fx_on[FX_DLY] ? 1.0f : 0.0f) - dsend) * 0.005f;
  int dt = (int)(0.75f * bf); if (dt > DLY_N - 1) dt = DLY_N - 1; if (dt < 1) dt = 1;
  float rl = dlyL[(dw - (uint32_t)dt) & DLY_M], rr = dlyR[(dw - (uint32_t)dt) & DLY_M];
  float fb = 0.45f + 0.3f * dvel;
  dlpL += (rr - dlpL) * 0.3f; dhpL += (dlpL - dhpL) * 0.02f;          /* into the left: the right's echo, dark and thin */
  dlpR += (rl - dlpR) * 0.3f; dhpR += (dlpR - dhpR) * 0.02f;
  dlyL[dw & DLY_M] = m * dsend + vd_tanh((dlpL - dhpL) * fb);
  dlyR[dw & DLY_M] = vd_tanh((dlpR - dhpR) * fb);
  dw++;
  /* hall */
  hsend += ((fx_on[FX_HALL] ? 1.0f : 0.0f) - hsend) * 0.005f;
  float o[4];
  for (int i = 0; i < 4; i++) { float x = fdn[i][(hw - (uint32_t)hlen[i]) & FDN_M]; hlp[i] += (x - hlp[i]) * hdamp; o[i] = hlp[i]; }
  float h0 = o[0] + o[1] + o[2] + o[3], h1 = o[0] - o[1] + o[2] - o[3], h2 = o[0] + o[1] - o[2] - o[3], h3 = o[0] - o[1] - o[2] + o[3];
  float in = m * hsend * 0.6f;
  fdn[0][hw & FDN_M] = in + 0.5f * h0 * hgain[0]; fdn[1][hw & FDN_M] = in + 0.5f * h1 * hgain[1];
  fdn[2][hw & FDN_M] = in + 0.5f * h2 * hgain[2]; fdn[3][hw & FDN_M] = in + 0.5f * h3 * hgain[3];
  hw++;
  *L += rl * 0.6f + (o[0] + o[2]) * 0.35f;
  *R += rr * 0.6f + (o[1] + o[3]) * 0.35f;
}
/* crush: fewer bits and a lower rate – then a low-pass at the new rate's Nyquist, so the mirror images the rate
   makes (the high whine) stay out and what is left is grit */
static svf_t crfL, crfR;
static void crush_frame(float *L, float *R)
{
  if (!fx_on[FX_CRUSH]) { crph = 1.0f; crfL.c1 = crfL.c2 = crfR.c1 = crfR.c2 = 0; return; }
  float a = fx_vel[FX_CRUSH], lv = vd_exp2(10.0f - 5.0f * a), d = 1.0f + a * 9.0f;   /* 10 .. 5 bits, rate / 1 .. 10 */
  crph += 1.0f / d;
  if (crph >= 1.0f) {
    crph -= 1.0f;
    crL = (float)(int)(*L * lv + (*L >= 0 ? 0.5f : -0.5f)) / lv;
    crR = (float)(int)(*R * lv + (*R >= 0 ? 0.5f : -0.5f)) / lv;
  }
  float g = tan_pi(vd_clamp(0.45f * SR / d, 1500.0f, 7000.0f)), lo, bp, hi;
  svf(&crfL, crL, g, 1.2f, &lo, &bp, &hi); *L = lo;
  svf(&crfR, crR, g, 1.2f, &lo, &bp, &hi); *R = lo;
}

/* note repeat and the overdub */
static double pbeat;                           /* a beat that always runs (the sequencer's while it plays) */
static double nr_next[LANES];
static int nr_was[LANES];
static int32_t skip_s[LANES] = { -1, -1, -1, -1 };   /* a step the overdub just played by hand: not twice */
static void rec_hit(int l, float vel, double b)
{
  if (!rec_on || b < 0.0) return;
  int32_t s = (int32_t)(b * 4.0 + 0.5);                             /* the nearest 16th */
  float v = vel >= 0.9f ? 1.0f : vel >= 0.55f ? 0.8f : 0.42f;
  int k = s & 15;
  if (pat[l][k] < v) pat[l][k] = v;
  if (s > a_step) skip_s[l] = s;
}

/* ---- MIDI out: the audio flow is the queue's only writer (notes, START / STOP; the SDK adds the clock) ---------- */
static int mo_note[LANES] = { -1, -1, -1, -1 }, mo_ch[LANES], mo_left[LANES], mo_run;
static volatile uint64_t mo_t[LANES];                  /* when a lane last sent (the MIDI page's lights) */
static int mo_mode, mo_map, mo_och;
static float mo_learn[LANES];
static void midi_off(int l, int frame)
{
  if (mo_note[l] < 0) return;
  vibe_midi_send_at((uint8_t)(0x80 | mo_ch[l]), (uint8_t)mo_note[l], 0, frame);
  mo_note[l] = -1;
}
static void midi_hit(int l, float vel, int frame)
{
  if (!(mo_mode & 1)) return;
  int v = (int)(vel * 127.0f + 0.5f); if (v < 1) v = 1; if (v > 127) v = 127;
  midi_off(l, frame);
  mo_note[l] = out_note(mo_map, l, mo_learn) & 127; mo_ch[l] = mo_och;
  vibe_midi_send_at((uint8_t)(0x90 | mo_ch[l]), (uint8_t)mo_note[l], (uint8_t)v, frame);
  mo_left[l] = (int)(0.03f * SR);                      /* a drum: 30 ms of gate is plenty */
  mo_t[l] = vibe_frames();
}

void vibe_on_audio(const float *in, float *out_l, float *out_r, int frames)
{
  const float *i1 = vibe_input(0), *i2 = vibe_input(1);
  if (!i1) i1 = in;
  /* the clock */
  uint32_t st = start_seq;
  const vibe_transport_info *tp = vibe_transport();
  int host = tp->has && tp->playing && tp->bpm > 0.0f;
  mo_mode = PI_(P_MOUT); mo_map = PI_(P_MMAP); mo_och = PI_(P_MOUTCH) & 15;
  for (int l = 0; l < LANES; l++) mo_learn[l] = P(P_LN0 + l);
  vibe_clock_out(mo_mode & 2); vibe_tempo_set(P(P_TEMPO));    /* (silent by itself while a host leads) */
  if (st != start_done) { own_beat = 0; a_step = -1; start_done = st; if ((mo_mode & 2) && !host) vibe_transport_out(1); mo_run = 1; }
  if (mo_run && !run_req) { if ((mo_mode & 2) && !host) vibe_transport_out(0); mo_run = 0; }
  int running = host || run_req;
  float bpm = host ? tp->bpm : P(P_TEMPO);
  double beat = host ? tp->beat : own_beat, dbeat = (double)bpm / 60.0 / (double)SR;
  float swing = P(P_SWING);
  /* live hits */
  uint32_t w = qw;
  VIBE_ACQUIRE();
  for (uint32_t k = qr; k != w; k++) {
    int l = q[k % QN].lane, fl = q[k % QN].flags; float v = q[k % QN].vel;
    hit(l, v);
    if (fl & QH_OUT) midi_hit(l, v, 0);
    if ((fl & QH_REC) && running) rec_hit(l, v, beat);
  }
  VIBE_RELEASE(); qr = w;
  /* the performance: which ring FX and filter are wanted (the newest held), note repeat */
  float bf = 60.0f / bpm * SR;
  int rwant = -1, fwant = -1; uint32_t rbest = 0, fbest = 0;
  for (int i = FX_R1; i <= FX_REV; i++) if (fx_on[i] && fx_seq[i] >= rbest) { rbest = fx_seq[i]; rwant = i; }
  for (int i = FX_LP; i <= FX_WASH; i++) if (fx_on[i] && fx_seq[i] >= fbest) { fbest = fx_seq[i]; fwant = i; }
  if (fx_on[FX_DLY]) dvel = fx_vel[FX_DLY];
  float nkt = fx_on[FX_NOKICK] ? 0.0f : 1.0f;
  double nr_rate = NR_BEATS[PI_(P_NRATE) % 6];
  int nr_h[LANES], any_was = 0; float nr_v[LANES];
  for (int l = 0; l < LANES; l++) { nr_h[l] = nr_on && nr_held[l] > 0; nr_v[l] = nr_vel[l]; any_was |= nr_was[l]; }
  if (running) pbeat = beat;
  for (int l = 0; l < LANES; l++) {
    if (nr_h[l] && !nr_was[l] && !running && !any_was) pbeat = 0.0;              /* stopped: the grid starts at the press */
    if (nr_h[l] && (!nr_was[l] || nr_next[l] - pbeat > nr_rate * 1.01 || pbeat - nr_next[l] > nr_rate)) {
      nr_next[l] = ((double)(int64_t)(pbeat / nr_rate) + 1.0) * nr_rate;
      if (!nr_was[l] && nr_next[l] - pbeat < nr_rate * 0.3) nr_next[l] += nr_rate;   /* not a flam right after the press */
    }
    nr_was[l] = nr_h[l];
  }

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
    int edge = !running;                                         /* a 16th begins here (stopped: every frame) */
    if (running && beat >= 0.0) {                                /* the step, with swing on the odd sixteenths */
      double t16 = beat * 4.0;
      int32_t s = (int32_t)t16;
      if ((s & 1) && (float)(t16 - (double)s) < swing * 0.66f) s--;
      if (s != a_step) {
        a_step = s; edge = 1;
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
          if (skip_s[l] == s) { skip_s[l] = -1; continue; }      /* played by hand a moment ago (REC) */
          float v = pat[l][k];
          if (v < 0) v = (vd_rand() & 1) ? -v : 0.0f;
          if (v > 0) { hit(l, v); midi_hit(l, v, n); }
        }
        step_pub = s;
      }
    }
    /* note repeat: the held keys' lanes on the grid */
    for (int l = 0; l < LANES; l++) if (nr_h[l] && pbeat >= nr_next[l]) {
      hit(l, nr_v[l]); midi_hit(l, nr_v[l], n); if (running) rec_hit(l, nr_v[l], pbeat);
      nr_next[l] += nr_rate;
    }
    /* the ring FX switch on a 16th */
    if (edge && rwant != (rstop ? -1 : rmode)) { if (rwant >= 0) ring_begin(rwant, bf); else if (rmode >= 0) rstop = 1; }
    /* the duck: 2 ms up, then down over RELEASE beats */
    if (duck_atk > 0) { duck += (1.0f - duck) * 0.08f; duck_atk--; } else duck *= dcoef;
    if (gate_left > 0 && --gate_left == 0) vibe_gate_out(0);
    for (int l = 0; l < LANES; l++) if (mo_note[l] >= 0 && --mo_left[l] <= 0) midi_off(l, n);

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
    nkg += (nkt - nkg) * 0.004f;                                 /* NO KICK: the kick and its rumble out */
    float k = kick * lv[L_KICK] * nkg;
    float bL = k + dr + rL * nkg, bR = k + dr + rR * nkg;          /* the drums, through the performance FX */
    ring_frame(&bL, &bR, bf);
    filter_frame(&bL, &bR, fwant, bf);
    crush_frame(&bL, &bR);
    throws_frame(&bL, &bR, bf);
    float L = (bL + xl) * mpre, R = (bR + xr) * mpre;
    L = vd_tanh(L) * mpost * vol; R = vd_tanh(R) * mpost * vol;
    out_l[n] = L; out_r[n] = R;
    float a = vd_maxf(vd_absf(L), vd_absf(R)); if (a > opk) opk = a;
    beat += dbeat; pbeat += dbeat;
  }
  if (!host && run_req) own_beat = beat;
  kick_busy = kb; rmb_busy = rb;
  duck_pub = duck; rlev_pub = rpk; out_pub = opk; in_pub = ipk;
}

/* ---- control flow -------------------------------------------------------------------------------------------- */
enum { PG_KICK, PG_PUNCH, PG_DRUMS, PG_SIDE, PG_RUMBLE, PG_SEQ, PG_MIDI, PG_COUNT };   /* MIDI: K6 again, from SEQ */
static const char *const PAGE_NAME[PG_COUNT] = { "KICK", "PUNCH", "DRUMS", "SIDE", "RUMBLE", "SEQ", "MIDI" };
static const int MAP[PG_COUNT][4] = {
  { K_TUNE, K_FROM, K_SWEEP, K_DECAY },
  { K_CLICK, K_DRIVE, K_MODE, K_TAIL },
  { P_HDEC, P_HTONE, P_ODEC, P_CDEC },
  { P_INGAIN, P_SCDEPTH, P_MDRIVE, P_VOLUME },
  { P_RLEVEL, P_RTONE, P_RDRIVE, P_DUCK },
  { P_TEMPO, P_SWING, P_NRATE, -1 },       /* SEQ: 4 = the lane's level */
  { P_MMAP, P_MINCH, P_MOUTCH, P_MOUT } };
static int page, lane, preset, help, running_c;
static int learn = -1;                                /* the MIDI page: the lane waiting for its note (ENTER), -1 none */
static uint64_t mi_t[LANES];                          /* when a lane was last played from outside (the MIDI page's lights) */
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

static void queue_hit(int l, float vel, int flags)
{
  uint32_t w = qw;
  if (w - qr >= QN) return;
  q[w % QN].lane = l; q[w % QN].vel = vel; q[w % QN].flags = flags;
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
  for (int i = 0; i < 6; i++) { vibe_label(VB_K1 + i, i == 5 ? (page == PG_MIDI ? "MIDI" : "SEQ") : PAGE_NAME[i]);
    vibe_led_button(VB_K1 + i, 0xFF3D14, i == page || (i == 5 && page == PG_MIDI) ? 1.0f : 0.0f); }
  vibe_label(VB_OCT_DOWN, "< PRST"); vibe_label(VB_OCT_UP, "PRST >");
  vibe_label(VB_ENTER, page == PG_MIDI ? "LEARN" : "");
  vibe_label(VB_LEFT, "REPEAT"); vibe_label(VB_REC, "REC");
}

void vibe_on_init(float sample_rate)
{
  SR = sample_rate > SR_MAX ? SR_MAX : sample_rate;
  choke_len = (int)(0.004f * SR);
  for (int k = 0; k < FFT_MAX / 2; k++) { double a = D_TAU * k / FFT_MAX; tw_c[k] = (float)d_cos(a); tw_s[k] = (float)d_sin(a); }
  kick_init();
  fx_init();
  for (int i = 0; i < K_COUNT; i++) { PARAMS[i] = KICK_SPECS[i]; GROUPS[i] = i < K_ATK ? "A · BODY" : i < K_CLICK ? "A · AMP" :
    i < K_TAIL ? "A · CLICK" : i < K_DRIVE ? "A · TAIL" : i < K_LOWCUT ? "A · DRIVE" : "A · OUT"; }
  for (int i = K_COUNT; i < P_COUNT; i++) { PARAMS[i] = MORE[i - K_COUNT]; GROUPS[i] = i <= P_RELEASE ? "B · RUMBLE" :
    i <= P_CDEC ? "B · DRUMS" : i <= P_VOLUME ? "B · SIDE" : i <= P_LV3 ? "B · SEQ" : "C · MIDI"; }
  vibe_notes(VIBE_NOTES | VIBE_NOTES_OWN_OCTAVE);               /* OCT- / OCT+ are the presets' */
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
  "SHIFT + PADS", "16 FX, held (2x: latch)",
  "F1-F4", "lane",
  "SHIFT + F1-F4", "mute, next bar",
  "F5 / SHIFT+F5", "pattern, next bar",
  "F6 / OCT+", "next kick preset",
  "OCT-", "previous preset",
  "KEYS", "C kick D/E clap",
  "SHIFT + KEY", "kick tuned to it",
  "LEFT", "note repeat (tap/hold)",
  "REC / SHIFT+REC", "overdub / undo lane",
  "K1-K6", "the six pages",
  "K6 again", "MIDI: map, ch, out",
  "ENTER (MIDI)", "learn lane's note",
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
/* the performance, control side */
static int fx_pad[16], fx_latch[16];                 /* the pad holds its FX / it stays on after the release */
static uint64_t fx_tap[16];
static uint32_t fx_count;
static int8_t key_held[128];                          /* a key's lane + 1 while it is down (note repeat), 0 none */
static uint64_t nr_t;
static void fx_press(int i, float v)
{
  uint64_t now = vibe_frames();
  if (fx_latch[i]) { fx_latch[i] = 0; fx_on[i] = 0; fx_pad[i] = 0; return; }    /* a tap ends a latched FX */
  fx_vel[i] = v > 0 ? v : 0.8f; fx_seq[i] = ++fx_count;
  VIBE_RELEASE();
  fx_on[i] = 1; fx_pad[i] = 1;
  if (now - fx_tap[i] < (uint64_t)(SR * 0.3f)) fx_latch[i] = 1;                    /* the second tap: latched */
  fx_tap[i] = now;
}
static void fx_release(int i) { fx_pad[i] = 0; if (!fx_latch[i]) fx_on[i] = 0; }
static void tune_to(int note)                         /* the kick's body on this note, between 36 and 72 Hz */
{
  int n = 24 + note % 12;
  if (vd_mtof((float)n) < 36.0f) n += 12;
  vibe_param_set(K_TUNE, vd_clamp(vd_mtof((float)n), PARAMS[K_TUNE].min, PARAMS[K_TUNE].max));
  kick_changed();
}

void vibe_on_event(const vibe_event *e)
{
  int fid, fr = vf_event(e, &fid);
  if (fr != VF_NO) { service(); return; }
  switch (e->type) {
  case VIBE_EV_BUTTON:
    if (e->id == VB_SHIFT) { shift = e->down; break; }
    if (e->id == VB_LEFT) {                                       /* note repeat: a tap switches it, held it is momentary */
      if (e->down) { if (nr_on) { nr_on = 0; nr_t = 0; } else { nr_on = 1; nr_t = vibe_frames(); } }
      else if (nr_t && vibe_frames() - nr_t > (uint64_t)(SR * 0.4f)) { nr_on = 0; nr_t = 0; }
      break;
    }
    if (!e->down) break;
    if (e->id == VB_REC) { if (shift) load_pattern(lane, book_at[lane]); else rec_on = !rec_on; break; }   /* SHIFT: the lane back to its book entry */
    if (e->id == VB_HOME) { help = !help; return; }
    if (e->id == VB_RETURN) { if (help) help = 0; else if (learn >= 0) learn = -1; else { page = PG_KICK; labels(); } return; }
    if (e->id >= VB_K1 && e->id <= VB_K6) {
      int pg = e->id - VB_K1;
      if (pg == PG_SEQ && (page == PG_SEQ || shift)) pg = PG_MIDI;   /* K6 twice (or SHIFT + K6): MIDI */
      if (page != pg) learn = -1;
      page = pg; labels();
    }
    if (e->id == VB_OCT_DOWN || e->id == VB_OCT_UP) { set_preset(preset + (e->id == VB_OCT_UP ? 1 : -1)); if (!running_c) queue_hit(L_KICK, 1.0f, 0); }
    if (e->id == VB_ENTER && page == PG_MIDI) {                    /* learn: the next note plays this lane, in and out */
      if (learn >= 0) learn = -1;
      else {
        int m = PI_(P_MMAP);
        if (m != MAP_LEARN) { float ln[LANES]; for (int l = 0; l < LANES; l++) ln[l] = P(P_LN0 + l);
          for (int l = 0; l < LANES; l++) vibe_param_set(P_LN0 + l, (float)out_note(m, l, ln)); vibe_param_set(P_MMAP, MAP_LEARN); }
        learn = lane;
      }
    }
    if (e->id >= VB_F1 && e->id <= VB_F4) { if (shift) toggle_mute(e->id - VB_F1); else lane = e->id - VB_F1; }
    if (e->id == VB_F5) {                                         /* the next (SHIFT: the previous) entry, on the next bar */
      int n = BOOK_N[lane], at = q_book[lane] >= 0 ? q_book[lane] : book_at[lane];
      queue_pattern(lane, (at + (shift ? n - 1 : 1)) % n);
    }
    if (e->id == VB_F6) { set_preset(preset + 1); if (!running_c) queue_hit(L_KICK, 1.0f, 0); }
    if (e->id == VB_PLAY) { if (!running_c) { start_seq = start_seq + 1; VIBE_RELEASE(); run_req = 1; running_c = 1; } else { run_req = 0; running_c = 0; } }
    if (e->id == VB_STOP) { run_req = 0; running_c = 0; }
    break;
  case VIBE_EV_PAD:
    if (e->id < 16 && e->down && shift) { fx_press(e->id, e->value); break; }
    if (e->id < 16 && !e->down && fx_pad[e->id]) { fx_release(e->id); break; }
    if (e->id < 16 && e->down) {
      float v = pat[lane][e->id];
      pat[lane][e->id] = v == 0 ? 0.8f : v == 0.8f ? 1.0f : v == 1.0f ? 0.42f : 0.0f;
      if (!running_c && pat[lane][e->id] > 0) queue_hit(lane, pat[lane][e->id], 0);
    }
    break;
  case VIBE_EV_NOTE:
    if (e->midi[1] == VIBE_NOTE_KEYS && e->id < 128) {             /* the keys: SHIFT tunes the kick; held: note repeat */
      if (!e->down) { int l = key_held[e->id] - 1; if (l >= 0 && nr_held[l] > 0) nr_held[l] = nr_held[l] - 1; key_held[e->id] = 0; break; }
      if (shift) { tune_to(e->id); if (!running_c) queue_hit(L_KICK, 1.0f, 0); break; }
      if (learn < 0 && !key_held[e->id]) { int l = key_lane(e->id); nr_vel[l] = e->value > 0 ? e->value : 0.8f; nr_held[l] = nr_held[l] + 1; key_held[e->id] = (int8_t)(l + 1); }
    }
    if (e->down) {                                               /* from outside: by the map, on the In channel; the keys:
                                                                    by pitch class, and they go out as MIDI too */
      int ext = e->midi[1] == VIBE_NOTE_MIDI || e->midi[1] == VIBE_NOTE_HOST, ch = PI_(P_MINCH);
      if (ext && ch && e->midi[0] != ch - 1) break;
      float vel = e->value > 0 ? e->value : 0.8f;
      if (learn >= 0) { vibe_param_set(P_LN0 + learn, (float)e->id); queue_hit(learn, vel, 0); mi_t[learn] = vibe_frames(); learn = -1; break; }
      float ln[LANES]; for (int l = 0; l < LANES; l++) ln[l] = P(P_LN0 + l);
      int l = ext ? in_lane(PI_(P_MMAP), e->id, ln) : key_lane(e->id);
      if (l < 0) break;
      queue_hit(l, vel, (ext ? 0 : QH_OUT) | QH_REC);
      if (ext) mi_t[l] = vibe_frames();
    }
    break;
  case VIBE_EV_MIDI: {                                           /* Program Change = preset; CC 20-23 mute a lane (>= 64),
                                                                    CC 24-27 its pattern from the book (0..); next bar */
    int st = e->midi[0], ty = st & 0xF0, ch = PI_(P_MINCH);
    if (ch && (st & 15) != ch - 1) break;
    if (ty == 0xC0 && e->midi[1] < NKP) { set_preset(e->midi[1]); if (!running_c) queue_hit(L_KICK, 1.0f, 0); }
    if (ty == 0xB0 && e->midi[1] >= 20 && e->midi[1] <= 23) { int l = e->midi[1] - 20; if (mute_req[l] != (e->midi[2] >= 64)) toggle_mute(l); }
    if (ty == 0xB0 && e->midi[1] >= 24 && e->midi[1] <= 27) { int l = e->midi[1] - 24; if (e->midi[2] < BOOK_N[l]) queue_pattern(l, e->midi[2]); }
    break;
  }
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

static char *cat(char *d, const char *s) { while (*s) *d++ = *s++; *d = 0; return d; }
static char *note_name(char *d, int n)                          /* 36 = C1 (as MPCs and most DAWs count) */
{
  static const char *const NN[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
  char b[8];
  d = cat(d, NN[n % 12]);
  return cat(d, vg_itoa(n / 12 - 2, b));
}
/* the MIDI page: in the step grid's place – per lane what plays it, what it sends, two lights */
static void midi_panel(int blink)
{
  static const char *const KRACHT_IN[LANES] = { "C (rest)", "F# G#", "A#", "D D# E" };
  static const char *const GM_IN[LANES] = { "B0 C1", "F#1 G#1", "A#1", "C#1-E1" };
  int map = PI_(P_MMAP), mode = PI_(P_MOUT), ich = PI_(P_MINCH);
  float ln[LANES]; for (int l = 0; l < LANES; l++) ln[l] = P(P_LN0 + l);
  uint64_t now = vibe_frames(), lit = (uint64_t)(SR * 0.12f);
  char b[32], *e;
  vg_text("LANE", 22, 117, VG_SMALL, C_MUTE, VG_LEFT);
  e = cat(b, ich ? "IN  CH " : "IN  "); cat(e, IN_CH[ich]);
  vg_text(b, 96, 117, VG_SMALL, C_MUTE, VG_LEFT);
  e = cat(b, "OUT  CH "); cat(e, IN_CH[PI_(P_MOUTCH) + 1]);
  vg_text(b, 236, 117, VG_SMALL, mode & 1 ? C_MUTE : C_EDGEHI, VG_LEFT);
  vg_text(mode & 2 ? "CLOCK OUT" : "CLOCK -", 466, 117, VG_SMALL, mode & 2 ? C_SIGNAL : C_EDGEHI, VG_RIGHT);
  for (int l = 0; l < LANES; l++) {
    int y = 136 + l * 20;
    vg_round(14, y - 2, 452, 18, 3, l == lane ? C_RAISED : C_PANEL);
    if (l == lane) vg_rect(14, y, 3, 14, C_SIGNAL);
    vg_text(LANE_NAME[l], 22, y + 1, VG_BOLD, l == lane ? C_TEXT : C_DIM, VG_LEFT);
    if (learn == l) vg_text(blink ? "> PLAY A NOTE" : "", 96, y + 1, VG_BOLD, C_SIGNAL, VG_LEFT);
    else {
      const char *in = map == MAP_KRACHT ? KRACHT_IN[l] : map == MAP_GM ? GM_IN[l] : 0;
      if (!in) { note_name(b, out_note(map, l, ln)); in = b; }
      vg_text(in, 96, y + 1, VG_BOLD, C_TEXT, VG_LEFT);
    }
    char o[12]; note_name(o, out_note(map, l, ln));
    vg_text(o, 236, y + 1, VG_BOLD, mode & 1 ? C_TEXT : C_EDGEHI, VG_LEFT);
    { char nb[8]; vg_text(vg_itoa(out_note(map, l, ln), nb), 290, y + 2, VG_SMALL, C_MUTE, VG_LEFT); }
    int in_on = now - mi_t[l] < lit && mi_t[l], out_on = now - mo_t[l] < lit && mo_t[l];
    vg_text("IN", 404, y + 2, VG_SMALL, in_on ? C_HOT : C_EDGEHI, VG_LEFT);
    vg_text("OUT", 430, y + 2, VG_SMALL, out_on ? C_SIGNAL : C_EDGEHI, VG_LEFT);
  }
  vg_text(learn >= 0 ? "ENTER / RETURN: cancel" : "ENTER learn  ·  PC preset  ·  CC 20-23 mute  ·  CC 24-27 pattern",
          240, 214, VG_SMALL, C_MUTE, VG_CENTER);
}

/* SHIFT held: the 16 FX in the step grid's place, laid out as the pads */
static void fx_panel(void)
{
  static const char *const ROW[4] = { "REPEAT", "PITCH", "FILTER", "THROW" };
  for (int r = 0; r < 4; r++) {
    int y = 120 + r * 25;
    vg_text(ROW[r], 22, y + 4, VG_SMALL, C_MUTE, VG_LEFT);
    for (int c = 0; c < 4; c++) {
      int i = r * 4 + c, x = 70 + c * 101, on = fx_on[i];
      vg_round(x, y, 97, 20, 3, on ? C_SIGNAL : C_RAISED);
      vg_text(FX_NAME[i], x + 48, y + 4, VG_SMALL, on ? C_BG : C_TEXT, VG_CENTER);
      if (fx_latch[i]) vg_rect(x + 4, y + 8, 4, 4, on ? C_BG : C_SIGNAL);    /* latched */
    }
  }
}
/* what is on: REC, note repeat, the FX – one line under the grid */
static void perf_line(int blink)
{
  char b[96], *e = b; int any = 0; b[0] = 0;
  if (rec_on) { e = cat(e, "REC"); any = 1; }
  if (nr_on) { e = cat(e, any ? "  ·  NOTE REPEAT " : "NOTE REPEAT "); e = cat(e, NR_NAMES[PI_(P_NRATE) % 6]); any = 1; }
  for (int i = 0; i < FX_N; i++) if (fx_on[i] && e - b < 70) { e = cat(e, any ? "  ·  " : ""); e = cat(e, FX_NAME[i]); any = 1; }
  if (!any) return;
  if (rec_on) vg_round(14, 219, 6, 6, 3, blink ? C_SIGNAL : 0x3A1208);
  vg_text(b, 24, 217, VG_SMALL, C_SIGNAL, VG_LEFT);
}

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
  { const char *tab[6]; for (int i = 0; i < 6; i++) tab[i] = PAGE_NAME[i == 5 && page == PG_MIDI ? PG_MIDI : i];   /* K6: SEQ or MIDI */
    int w = 0; for (int i = 0; i < 6; i++) w += vg_text_width(tab[i], VG_SMALL) + 9;
    int x = 470 - w;
    for (int i = 0; i < 6; i++) {
      int on = i == page || (i == 5 && page == PG_MIDI);
      int tw = vg_text(tab[i], x, 9, VG_SMALL, on ? C_TEXT : C_MUTE, VG_LEFT);
      if (on) vg_rect(x, 26, tw, 2, C_SIGNAL);
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
  if (shift) fx_panel();
  else if (page == PG_MIDI) midi_panel(blink);
  else for (int l = 0; l < LANES; l++) {
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
  if (page != PG_MIDI || shift) perf_line(blink);
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
  for (int k = 0; k < 16; k++) {                               /* (SHIFT: the FX, a row a shade) */
    float v = pat[lane][k]; if (v < 0) v = -v * 0.5f;
    uint32_t col = k == step ? 0xFFFFFF : C_SIGNAL;
    float lvl = k == step ? 0.6f : v;
    if (shift) { col = fx_on[k] ? C_HOT : C_SIGNAL; lvl = fx_on[k] ? 1.0f : (k / 4) & 1 ? 0.22f : 0.1f; }
    uint32_t key = ((uint32_t)(lvl * 200.0f) << 24) ^ col;
    if (pad_shown[k] != key + 1) { vibe_led_pad(k, col, lvl); pad_shown[k] = key + 1; }
  }
  { static int led_rec = -1, led_nr = -1;
    int r = rec_on ? 1 + blink : 0, nr = nr_on != 0;
    if (r != led_rec) { vibe_led_button(VB_REC, 0xFF3D14, r == 2 ? 1.0f : r ? 0.35f : 0.0f); led_rec = r; }
    if (nr != led_nr) { vibe_led_button(VB_LEFT, 0xFF3D14, nr ? 1.0f : 0.0f); led_nr = nr; } }
  vibe_link_page(PAGE_NAME[page]);
  vf_begin();
  for (int i = 0; i < 4; i++) vf_encoder(VE_1 + i, 14 + i * 118, 236, 104, 26);
  for (int i = 0; i < 6; i++) vf_key(VB_F1 + i, 4 + i * 80, 286, 72, 30);
  vf_end(C_SIGNAL);
}
