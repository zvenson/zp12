/* SPDX-License-Identifier: GPL-3.0-only */
/* zp12: the sound of an 80s 12-bit sampler. Self-contained (integer only, no tables to generate), so
 * the host tests and the web editor's preview can run the same code.
 *
 *   waves   12-bit linear samples, packed (2 in 3 bytes), recorded at 26.04 kHz or 27.5 kHz
 *   pitch   the playback rate, no interpolation: tuned up it skips samples, down it repeats them (the
 *           zero-order hold of a DAC clocked at the pitched rate); the aliasing is the sound
 *   sounds  32: a wave with TUNE / FINE, DECAY, LEVEL, PAN, START / END, REVERSE, 45->33, CHANNEL
 *   channels 8 outputs, one sound at a time each (a new hit on a channel cuts the last); a pad's channel is its
 *           position, 1-8 in every bank (2.1, as the SP-1200: bank A's pad 3 and bank D's pad 3 share channel 3):
 *           1-2 a 4-pole resonant low-pass (SSM2044-style): open as CUT at the hit, it follows the decay
 *               envelope down two octaves, as the original's dynamic filters do; 3-6 a fixed 2-pole
 *               low-pass each, 7-8 no filter. The order is the hardware's: sample, VCA (the decay),
 *               DRIVE, the filter, then the channel's fader (the mixer's slider: it never changes the tone)
 *   out     a stereo mix in Q15; sp_out makes the master's 16 bits of it (-6 dB, a soft knee) */
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
    uint8_t drive;                      /* 0..127: the channel's input driven into a soft clip */
    uint8_t send[3];                    /* chorus, delay, reverb sends 0..127 (sp_fx.c) */
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
    int32_t drv, snd[3];                /* drive (Q4 gain), sends Q15 */
    int32_t z[4];                       /* filter state */
    uint8_t on;
    uint8_t jump;                       /* the signal jumps next sample (a hit cut, a sample's end): SMOOTH bridges it */
    int32_t dc, last;                   /* SMOOTH: what bridges the jump, dying away; the last output */
    int32_t quiet;                      /* MUTE / SOLO: how far down, Q12 (0 heard, 4096 silent), ramped */
} sp_ch_t;

static sp_wave_t sp_wave[64];
static sp_sound_t sp_sound[SP_NSOUND];
static sp_ch_t sp_ch[SP_NCH];
#define SP_CH_OF(k) ((k) % SP_NCH)       /* the pad's channel: its position (2.1; sp_sound_t.chan only kept for the saves) */
static uint8_t sp_mix[SP_NCH] = {100, 100, 100, 100, 100, 100, 100, 100};   /* the channel faders, 0..127 */
static uint8_t sp_mute, sp_solo;        /* GLO held + white key: channels muted, soloed (a solo silences the others) */
static uint8_t sp_smooth = 1;            /* GLO > OUTPUT > SMOOTH: a jump in a channel's signal bridged in ~1 ms (no click) */
static int32_t sp_bus[3][SP_BLK];       /* the send buses of the block: chorus, delay, reverb (mono, Q15) */

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
    v = a + (b - a) * (uint32_t)r / 100u;          /* (< 2^17 * 100: 32 bits) */
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
    int64_t x = (int64_t)hz * 152982;              /* 2 pi f / fs in Q30 (2^30 * 2 pi / 44100; no 64-bit division) */
    int64_t y = (1LL << 30) - (x >> 8);
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

/* a soft clip, x in +-49152 (1.5 in Q15): 1.5 (q - q^3 / 3) for q = x / 1.5, 32-bit */
static int32_t sp_soft(int32_t x)
{
    int32_t q = x * 2 / 3, q3 = ((q * q) >> 15) * q >> 15;
    return (q - q3 / 3) * 3 / 2;
}

/* a hit's own TUNE + FINE, DECAY and CUT (recorded with it while they were turned, sp_seq.c), one word:
 * bit 31 set, cents + 2560 in 0-11, decay in 12-18, cut in 19-25; 0 = the sound's own */
#define SP_LOCK 0x80000000u
static uint32_t sp_lock_of(const sp_sound_t *s)
{
    return SP_LOCK | (uint32_t)(s->tune * 100 + s->fine + 2560) | (uint32_t)(s->decay & 127u) << 12 |
           (uint32_t)(s->cut & 127u) << 19;
}

/* hit sound k at velocity 1..127, `semis` semitones from its TUNE (MULTI PITCH), with a hit's lock (0: none) */
static void sp_trigger_lk(uint32_t k, uint32_t vel, int32_t semis, uint32_t lk)
{
    const sp_sound_t *s = &sp_sound[k % SP_NSOUND];
    sp_ch_t *c;
    const sp_wave_t *w;
    uint32_t a, b, rate;
    int32_t cents, lv;
    if (s->wave >= 64u || !sp_wave[s->wave].n)
        return;
    w = &sp_wave[s->wave];
    c = &sp_ch[SP_CH_OF(k)];
    a = w->n / 1000u * s->start + w->n % 1000u * s->start / 1000u;   /* (32 bits, no 64-bit division) */
    b = s->end ? w->n / 1000u * s->end + w->n % 1000u * s->end / 1000u : w->n;
    if (b > w->n) b = w->n;
    if (b <= a + 1u)
        return;
    cents = (lk ? (int32_t)(lk & 0xFFFu) - 2560 : s->tune * 100 + s->fine) + semis * 100;
    cents = sp_clamp(cents, -3650, 2450);
    rate = w->rate;
    if (s->flags & SPF_33)
        rate = rate * 33u / 45u;
    c->w = w;
    c->dir = (s->flags & SPF_REVERSE) ? -1 : 1;
    c->pos = c->dir > 0 ? a : b - 1u;
    c->end = c->dir > 0 ? b : a;
    c->frac = 0;
    c->step = (uint32_t)(((uint64_t)(rate * 65536u / SP_FS) * sp_pow2_cents(cents)) >> 16);
    c->env = 1 << 24;
    c->emul = sp_decay_mul(lk ? (lk >> 12) & 127u : s->decay);
    lv = (int32_t)s->level * s->level / 127 * (int32_t)vel / 127;   /* 0..127: LEVEL in an audio taper (squared) */
    c->gl = lv * (64 - (s->pan > 0 ? s->pan : 0)) / 3;   /* Q12: 127 * 64 / 3 = 2709, -3.6 dB a channel */
    c->gr = lv * (64 + (s->pan < 0 ? s->pan : 0)) / 3;   /* (eight at once still fit the mix's headroom) */
    c->cut = lk ? (int32_t)((lk >> 19) & 127u) : s->cut;
    c->res = s->reso;
    c->drv = s->drive ? 16 + s->drive / 2 : 0;          /* 1x .. ~5x into the clip */
    c->snd[0] = s->send[0] * 258;
    c->snd[1] = s->send[1] * 258;
    c->snd[2] = s->send[2] * 258;
    c->tail = 8;
    /* SMOOTH only where it clicks: a sound still sounding cut, or a start inside the wave (TRUNC); a sample from
     * its own beginning keeps its attack */
    c->jump = (uint8_t)(c->on || ((s->flags & SPF_REVERSE) ? s->end < 1000u && s->end : s->start > 0u));
    if (!c->jump) c->dc = 0;
    c->on = 1;
}

static void sp_trigger_at(uint32_t k, uint32_t vel, int32_t semis) { sp_trigger_lk(k, vel, semis, 0); }
static void sp_trigger(uint32_t k, uint32_t vel) { sp_trigger_at(k, vel, 0); }

/* channel ch's next SP_BLK samples into out (stereo, added) */
static void sp_channel(uint32_t ch, int32_t *out)
{
    sp_ch_t *c = &sp_ch[ch];
    uint32_t i;
    int32_t a1 = 0, k = 0, x, y, e0, e1, gl, gr, q0 = c->quiet, q1;
    int32_t qt = (sp_solo ? !((sp_solo >> ch) & 1u) : (sp_mute >> ch) & 1u) ? 4096 : 0;
    q1 = q0 < qt ? (q0 + 1024 < qt ? q0 + 1024 : qt) : (q0 - 1024 > qt ? q0 - 1024 : qt);   /* (~3 ms: no click) */
    c->quiet = q1;
    if (!c->on && !c->tail) {
        c->quiet = qt;                                /* (silent anyway: there at once) */
        return;
    }
    e0 = c->env >> 9;                                 /* Q15: the envelope now, and at the block's end: the level */
    e1 = (int32_t)(((int64_t)c->env * c->emul) >> 16) >> 9;   /* ramps between them (no steps at the block rate) */
    gl = c->gl * sp_mix[ch] * sp_mix[ch] / 10000;     /* the fader, an audio taper as the original's sliders: */
    gr = c->gr * sp_mix[ch] * sp_mix[ch] / 10000;     /* 100 as set, 50 -12 dB, 25 -24 dB, 127 +4 dB */
    if (ch < 2u) {                                    /* the dynamic filter: CUT at the hit, two octaves down as it decays */
        uint32_t hz = sp_cut_hz(c->cut) * sp_pow2_cents(-2400 + ((e0 * 2400) >> 15)) >> 16;
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
                    c->jump = 1;                      /* (the sample ends: from where it was to silence) */
                    break;
                }
            }
        } else {
            x = 0;
        }
        x = (x * (e0 + (((e1 - e0) * (int32_t)i) >> 5))) >> 15;   /* (|x| < 2^15, e < 2^15: 32 bits) */
        if (c->drv) {                                 /* DRIVE: louder into a soft clip, the level kept */
            x = sp_clamp((x * c->drv) >> 4, -49152, 49152);
            x = sp_soft(x);
            x = x * 3 / 4;
        }
        if (ch < 2u) {                                /* 4 one-pole stages, the 4th fed back, a soft clip in */
            int32_t in = x - ((c->z[3] * k) >> 12);
            in = sp_clamp(in, -49152, 49152);
            in = sp_soft(in);                         /* x - x^3/3 shape */
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
        if (c->jump) {                                /* SMOOTH: go on from where it was, into the new signal */
            c->jump = 0;
            if (sp_smooth) c->dc = c->last - y;
        }
        y += c->dc;
        c->dc -= c->dc / 24;                          /* (gone in ~1 ms: e^-1 every 24 samples) */
        c->last = y;
        if (q0 | q1)                                  /* muted: the channel plays on, unheard (sends too) */
            y = (y * (4096 - (q0 + (((q1 - q0) * (int32_t)i) >> 5)))) >> 12;
        out[2u * i] += (y * gl) >> 12;
        out[2u * i + 1u] += (y * gr) >> 12;
        if (c->snd[0] | c->snd[1] | c->snd[2]) {      /* the sends: post fader, before pan */
            int32_t m = (y * (gl + gr)) >> 13;
            sp_bus[0][i] += (m * c->snd[0]) >> 15;
            sp_bus[1][i] += (m * c->snd[1]) >> 15;
            sp_bus[2][i] += (m * c->snd[2]) >> 15;
        }
    }
    c->env = (int32_t)(((int64_t)c->env * c->emul) >> 16);
    if (c->env < (1 << 12))                            /* below ~-72 dB: done */
        c->on = 0;
    if (!c->on && c->tail && !--c->tail)
        c->z[0] = c->z[1] = c->z[2] = c->z[3] = 0;
}

/* the master: the Q15 mix to 16 bits at -6 dB (the full scale was too loud), with a soft knee above 3/4 so a
 * pile of hits rounds off like a mixer's bus instead of clipping hard (tanh as u (27 + u^2) / (27 + 9 u^2)) */
static inline int32_t sp_out(int32_t v)
{
    int32_t a, u, f;
    v >>= 1;
    a = v < 0 ? -v : v;
    if (a <= 24576)
        return v;
    u = (a - 24576) >> 5;                              /* Q8 above the knee, 3.0 the most */
    if (u > 768) u = 768;
    f = u * (27 * 65536 + u * u) / (27 * 65536 + 9 * u * u);
    a = 24576 + (f << 5);
    if (a > 32767) a = 32767;
    return v < 0 ? -a : a;
}

/* the mix of all channels for one block (stereo Q15, out zeroed by the caller or added to) */
/* the metronome: a short square blip, the bar's first beat higher; not on a channel, not on the faders */
static struct { uint32_t ph, inc; int32_t env; } sp_clk;
static void sp_click(int accent)
{
    sp_clk.inc = (accent ? 1760u : 1175u) * 97391u;   /* 2^32 / 44100 = 97391.5 */
    sp_clk.ph = 0;
    sp_clk.env = 9000;
}
static void sp_click_block(int32_t *out)
{
    uint32_t i;
    if (sp_clk.env < 16)
        return;
    for (i = 0; i < SP_BLK; i++) {
        int32_t v = (sp_clk.ph & 0x80000000u) ? sp_clk.env : -sp_clk.env;
        sp_clk.ph += sp_clk.inc;
        out[2u * i] += v;
        out[2u * i + 1u] += v;
    }
    sp_clk.env = sp_clk.env * 27 / 32;          /* ~-1.5 dB a block: gone in ~25 ms */
}

#ifdef SP_WITH_FX
static void sp_fx_block(int32_t *out);         /* sp_fx.c */
#endif
static void sp_render(int32_t *out)
{
    uint32_t ch, i;
    for (i = 0; i < SP_BLK; i++)
        sp_bus[0][i] = sp_bus[1][i] = sp_bus[2][i] = 0;
    for (ch = 0; ch < SP_NCH; ch++)
        sp_channel(ch, out);
#ifdef SP_WITH_FX
    sp_fx_block(out);
#endif
    sp_click_block(out);
}
