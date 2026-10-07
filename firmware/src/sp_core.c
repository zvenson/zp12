/* SPDX-License-Identifier: GPL-3.0-only */
/* zp12: the sound of an 80s 12-bit sampler. Self-contained (integer only, no tables to generate), so
 * the host tests and the web editor's preview can run the same code.
 *
 *   waves   12-bit linear samples, packed (2 in 3 bytes), recorded at 26.04 kHz or 27.5 kHz
 *   pitch   the playback rate, no interpolation: tuned up it skips samples, down it repeats them (the
 *           zero-order hold of a DAC clocked at the pitched rate); the aliasing is the sound
 *   sounds  32: a wave with TUNE / FINE, DECAY, LEVEL, PAN, START / END, REVERSE, 45->33, CHANNEL
 *   channels 8 outputs, one sound at a time each (a new hit on a channel cuts the last):
 *           1-2 a 4-pole resonant low-pass on the decay envelope (SSM2044-style, its cutoff follows
 *               the level as the original's dynamic filters do), 3-6 a fixed 2-pole low-pass each,
 *           7-8 no filter
 *   out     a stereo mix in Q15 (the caller adds it to the master) */
#include <stdint.h>

#define SP_NCH 8
#define SP_NSOUND 32
#define SP_FS 44100
#define SP_BLK 32                       /* the envelope and the filter cutoff move once a block */

typedef struct {                        /* a wave: where its packed samples are */
    const uint8_t *d;                   /* (flash, XIP) */
    uint32_t n;                         /* samples */
    uint16_t rate;                      /* Hz it was recorded at: 26040 or 27500 */
} sp_wave_t;

enum { SPF_REVERSE = 1, SPF_33 = 2 };   /* SPF_33: stored at 45 rpm, played at 33 1/3 (rate x 33/45) */

typedef struct {                        /* a sound (a pad) */
    uint8_t wave;                       /* index into sp_wave[] (0xFF: none) */
    int8_t tune;                        /* semitones -24..+12 */
    int8_t fine;                        /* cents -50..+50 */
    uint8_t decay;                      /* 0 short .. 127 the whole sound */
    uint8_t level;                      /* 0..127 */
    int8_t pan;                         /* -64..63 */
    uint8_t chan;                       /* 0..7 */
    uint8_t flags;                      /* SPF_* */
    uint16_t start, end;                /* truncate, 0..1000 of the wave (end 1000 = its end) */
    uint8_t cut, reso;                  /* channels 1-2: cutoff 0..127, resonance 0..127 */
} sp_sound_t;

typedef struct {
    const sp_wave_t *w;
    uint32_t pos, end;                  /* the sample now (integer) and where it stops */
    uint32_t frac, step;                /* Q16 */
    int32_t dir;                        /* +1 / -1 */
    int32_t env, emul;                  /* Q24 level, Q16 its decay a block */
    uint8_t tail;                       /* blocks the filter still rings after the sample ended */
    int32_t gl, gr;                     /* Q12 gains: level x pan */
    int32_t cut, res;                   /* channel 1-2 filter settings */
    int32_t z[4];                       /* filter state */
    uint8_t on;
} sp_ch_t;

static sp_wave_t sp_wave[64];
static sp_sound_t sp_sound[SP_NSOUND];
static sp_ch_t sp_ch[SP_NCH];
static uint8_t sp_mix[SP_NCH] = {100, 100, 100, 100, 100, 100, 100, 100};   /* the channel faders, 0..127 */

/* sample i of a packed wave, -2048..2047 */
static inline int32_t sp_sample(const uint8_t *d, uint32_t i)
{
    const uint8_t *p = d + (i >> 1) * 3u;
    int32_t v = (i & 1u) ? (p[1] >> 4) | (p[2] << 4) : p[0] | ((p[1] & 15) << 8);
    return (v ^ 0x800) - 0x800;
}

/* pack n 16-bit samples (already 12-bit: the low 4 bits are dropped) into 12-bit pairs; d holds
 * (n + 1) / 2 * 3 bytes */
static void sp_pack(uint8_t *d, const int16_t *s, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i += 2) {
        uint32_t a = (uint32_t)(s[i] >> 4) & 0xFFFu, b = i + 1u < n ? (uint32_t)(s[i + 1] >> 4) & 0xFFFu : 0u;
        d[0] = (uint8_t)a;
        d[1] = (uint8_t)((a >> 8) | (b << 4));
        d[2] = (uint8_t)(b >> 4);
        d += 3;
    }
}

static inline int32_t sp_clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

/* 2^(c / 1200) in Q16 for c in cents, -2400..+1200 (no float: the semitones from a table, the cents
 * between them linear, < 0.1 cent off) */
static uint32_t sp_pow2_cents(int32_t c)
{
    static const uint32_t SEMI[13] = {65536, 69433, 73562, 77936, 82570, 87480, 92682, 98193, 104032,
                                      110218, 116772, 123715, 131072};
    int32_t oct = 0, s, r;
    uint32_t a, b, v;
    while (c < 0) { c += 1200; oct--; }
    while (c >= 1200) { c -= 1200; oct++; }
    s = c / 100, r = c % 100;
    a = SEMI[s], b = SEMI[s + 1];
    v = a + (uint32_t)(((uint64_t)(b - a) * (uint32_t)r) / 100u);
    return oct >= 0 ? v << oct : v >> -oct;
}

/* the decay a block for DECAY d: 127 = none; else a time constant from ~15 ms to ~2.5 s */
static int32_t sp_decay_mul(uint32_t d)
{
    uint32_t ms, blocks;
    if (d >= 127u)
        return 65536;
    ms = 15u + (d * d * 155u) / 1000u;               /* 15 ms .. ~2.5 s, finer at the short end */
    blocks = ms * (SP_FS / 1000u) / SP_BLK;
    return 65536 - (int32_t)(65536u / (blocks + 1u));  /* e^(-1) after ~blocks blocks */
}

/* the one-pole coefficient (Q15) for a cutoff in Hz: 1 - e^(-2 pi f / fs), e^-x as (1 - x/256)^256
 * (eight squarings in Q30: within 0.3 % up to 20 kHz) */
static int32_t sp_onepole(uint32_t hz)
{
    int64_t x = (int64_t)hz * 6746518852LL / SP_FS;   /* 2 pi f / fs in Q30 */
    int64_t y = (1LL << 30) - x / 256;
    uint32_t i;
    for (i = 0; i < 8u; i++)
        y = (y * y) >> 30;
    return (int32_t)(((1LL << 30) - y) >> 15);
}

/* channels 1-2: CUT 0..127 -> 60 Hz .. 16 kHz, exponential */
static uint32_t sp_cut_hz(int32_t c)
{
    return (uint32_t)(60u * sp_pow2_cents(c * 9600 / 127) >> 16);
}

/* hit sound k at velocity 1..127, `semis` semitones from its TUNE (MULTI PITCH) */
static void sp_trigger_at(uint32_t k, uint32_t vel, int32_t semis)
{
    const sp_sound_t *s = &sp_sound[k % SP_NSOUND];
    sp_ch_t *c;
    const sp_wave_t *w;
    uint32_t a, b, rate;
    int32_t cents, lv;
    if (s->wave >= 64u || !sp_wave[s->wave].n)
        return;
    w = &sp_wave[s->wave];
    c = &sp_ch[s->chan % SP_NCH];
    a = (uint32_t)((uint64_t)w->n * s->start / 1000u);
    b = (uint32_t)((uint64_t)w->n * (s->end ? s->end : 1000u) / 1000u);
    if (b > w->n) b = w->n;
    if (b <= a + 1u)
        return;
    cents = sp_clamp(s->tune + semis, -36, 24) * 100 + s->fine;
    rate = w->rate;
    if (s->flags & SPF_33)
        rate = rate * 33u / 45u;
    c->w = w;
    c->dir = (s->flags & SPF_REVERSE) ? -1 : 1;
    c->pos = c->dir > 0 ? a : b - 1u;
    c->end = c->dir > 0 ? b : a;
    c->frac = 0;
    c->step = (uint32_t)(((uint64_t)rate * 65536u / SP_FS) * sp_pow2_cents(cents) >> 16);
    c->env = 1 << 24;
    c->emul = sp_decay_mul(s->decay);
    lv = (int32_t)s->level * (int32_t)vel / 127;      /* 0..127 */
    c->gl = lv * (64 - (s->pan > 0 ? s->pan : 0)) / 3;   /* Q12: 127 * 64 / 3 = 2709, -3.6 dB a channel */
    c->gr = lv * (64 + (s->pan < 0 ? s->pan : 0)) / 3;   /* (eight at once still fit the mix's headroom) */
    c->cut = s->cut;
    c->res = s->reso;
    c->tail = 8;
    c->on = 1;
}

static void sp_trigger(uint32_t k, uint32_t vel) { sp_trigger_at(k, vel, 0); }

/* channel ch's next SP_BLK samples into out (stereo, added) */
static void sp_channel(uint32_t ch, int32_t *out)
{
    sp_ch_t *c = &sp_ch[ch];
    uint32_t i;
    int32_t a1 = 0, k = 0, x, y, e;
    if (!c->on && !c->tail)
        return;
    e = (int32_t)(((int64_t)(c->env >> 9) * sp_mix[ch]) / 100);   /* (100 = the level as set) */                                  /* Q15 */
    if (ch < 2u) {                                    /* the dynamic filter: the cutoff follows the level */
        uint32_t hz = sp_cut_hz(c->cut) * (uint32_t)(8192 + (e >> 2)) >> 15;   /* x 0.25 .. 0.5+ with it */
        a1 = sp_onepole(hz < 30u ? 30u : hz > 18000u ? 18000u : hz);
        k = c->res * 125;                             /* Q12 feedback, 0 .. ~3.9 (self-oscillation near 4) */
    } else if (ch < 6u) {
        a1 = sp_onepole(ch < 4u ? 9000u : 12000u);    /* the fixed output filters */
    }
    for (i = 0; i < SP_BLK; i++) {
        if (c->on) {
            x = sp_sample(c->w->d, c->pos) << 4;      /* Q15 */
            c->frac += c->step;                       /* zero-order hold: the sample now, no interpolation */
            while (c->frac >= 65536u) {
                c->frac -= 65536u;
                c->pos += (uint32_t)c->dir;
                if (c->pos == c->end || (c->dir < 0 && c->pos == 0xFFFFFFFFu)) {
                    c->on = 0;
                    break;
                }
            }
        } else {
            x = 0;
        }
        x = (int32_t)(((int64_t)x * e) >> 15);
        if (ch < 2u) {                                /* 4 one-pole stages, the 4th fed back, a soft clip in */
            int32_t in = x - ((c->z[3] * k) >> 12);
            in = sp_clamp(in, -49152, 49152);
            in = in - (int32_t)(((int64_t)in * in / 49152 * in) / 49152 / 3);   /* x - x^3/3 shape */
            c->z[0] += ((in - c->z[0]) * a1) >> 15;
            c->z[1] += ((c->z[0] - c->z[1]) * a1) >> 15;
            c->z[2] += ((c->z[1] - c->z[2]) * a1) >> 15;
            c->z[3] += ((c->z[2] - c->z[3]) * a1) >> 15;
            y = c->z[3];
        } else if (ch < 6u) {
            c->z[0] += ((x - c->z[0]) * a1) >> 15;
            c->z[1] += ((c->z[0] - c->z[1]) * a1) >> 15;
            y = c->z[1];
        } else {
            y = x;
        }
        out[2u * i] += (y * c->gl) >> 12;
        out[2u * i + 1u] += (y * c->gr) >> 12;
    }
    c->env = (int32_t)(((int64_t)c->env * c->emul) >> 16);
    if (c->env < (1 << 12))                            /* below ~-72 dB: done */
        c->on = 0;
    if (!c->on && c->tail && !--c->tail)
        c->z[0] = c->z[1] = c->z[2] = c->z[3] = 0;
}

/* the mix of all channels for one block (stereo Q15, out zeroed by the caller or added to) */
static void sp_render(int32_t *out)
{
    uint32_t ch;
    for (ch = 0; ch < SP_NCH; ch++)
        sp_channel(ch, out);
}
