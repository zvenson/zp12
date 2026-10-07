/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* zp12's modern side: sloopDX's three send buses (fx.c fx_buses, unchanged but for where its settings live):
 * a stereo chorus, a tempo delay with a low-passed feedback, and a feedback-delay-network reverb with a
 * pre-delay. Every channel sends to them (its sound's CHO / DLY / REV); the wet comes back in stereo. */
#define DLY_LEN 65536u           /* 1.49 s: 1/4 at 40 BPM fits */
#define CHO_LEN 2048u
static int16_t dly_buf[DLY_LEN] __attribute__((section(".pool")));
static int16_t cho_buf[CHO_LEN] __attribute__((section(".pool")));
/* the reverb: two input diffusers, then four delay lines mixed by a Hadamard matrix (a feedback delay
 * network: every echo feeds all four, so it thickens instead of ringing like a comb), damped in the
 * loop, one line slowly modulated (no metallic tone on long tails); left and right take different lines */
#define REV_MOD 12               /* samples the modulated line moves (+-) */
static const uint16_t REV_LINE[4] = {1559, 1931, 2389, 2791};   /* 35..63 ms, coprime */
static const uint16_t REV_AP[2] = {556, 441};
static int16_t rev_line[1559 + 1931 + 2389 + 2791 + REV_MOD + 2] __attribute__((section(".pool")));
static int16_t rev_ap[556 + 441] __attribute__((section(".pool")));
#define PRE_LEN 4096u            /* the reverb's pre-delay (REV page PRE): 93 ms; 90 used */
static int16_t pre_buf[PRE_LEN] __attribute__((section(".pool")));
static struct {
    uint32_t dly_w, cho_w, cho_ph, rev_ph, pre_w;
    int32_t dly_lp;
    uint16_t line_i[4], ap_i[2];
    int32_t line_lp[4];
} fx;

static struct {                                  /* the FX pages (0..127 as in sloopDX unless said) */
    int16_t crate, cdepth, cmix;                 /* CHORUS */
    int16_t dtime, fdbk, colr, dmix;             /* DELAY: dtime 0..5 = 1/4 1/8 1/16 1/32 8T 16T */
    int16_t size, damp, pre;                     /* REVERB: pre in ms 0..90 */
} fxp = {40, 60, 127, 1, 60, 70, 90, 90, 60, 0};
static uint16_t fx_bpm10 = 900;                  /* the tempo the delay follows (the sequencer sets it) */

static inline int32_t fx_mulq15(int32_t a, int32_t b) { return (a * b) >> 15; }
/* sin(2 pi ph / 2^32) in Q15: a parabola corrected (< 0.1 %), enough for the LFOs */
static int32_t fx_sine(uint32_t ph)
{
    int32_t x = 32768 - (int32_t)(ph >> 16), y;    /* sin(t) = sin(pi - t): pi - t as -32768..32767 = -pi..pi */
    y = (int32_t)(((int64_t)x * (32768 - (x < 0 ? -x : x))) >> 13);   /* 4 x (1 - |x|), Q15 */
    return (int32_t)(((int64_t)y * 25395 + (((int64_t)y * (y < 0 ? -y : y)) >> 15) * 7373) >> 15);
}
/* the LFO step a block (32 samples) for RATE r 0..127: 0.05 Hz .. ~13 Hz, exponential */
static uint32_t fx_lfo_inc(int32_t r)
{
    uint32_t mhz = (uint32_t)(50u * sp_pow2_cents(r * 9600 / 127) >> 16);   /* 8 octaves */
    return mhz * 3117u;                            /* 2^32 * 32 / 44100 / 1000 = 3116.6 */
}
static uint32_t fx_delay_samples(void)
{
    static const uint16_t DIV_NUM[6] = {24, 12, 6, 3, 8, 4};   /* in 1/24 of a quarter: 1/4 1/8 1/16 1/32 8T 16T */
    uint32_t s = SP_FS * 600u / 24u * DIV_NUM[fxp.dtime % 6] / (fx_bpm10 ? fx_bpm10 : 900u);   /* (32 bits) */
    return s < 16u ? 16u : s >= DLY_LEN ? DLY_LEN - 1u : s;
}

/* process the three buses for one block; sends in, wet out (stereo). The LFOs (chorus, reverb line)
 * are computed per block and ramped: no sine per sample */
static void fx_buses(const int32_t *cho_in, const int32_t *dly_in, const int32_t *rev_in, int32_t *wet_l,
                     int32_t *wet_r, uint32_t n)
{
    uint32_t i, k, dl = fx_delay_samples();
    int32_t fb = fxp.fdbk * 230, col = 2000 + fxp.colr * 240;
    int32_t dmix = fxp.dmix * 258;
    int32_t g = 17000 + fxp.size * 104, lpk = 32767 - fxp.damp * 200;   /* loop gain (RT60 ~0.4..4 s), damping */
    int32_t cdepth = fxp.cdepth * 6, cmix = fxp.cmix * 258;
    uint32_t pre = (uint32_t)fxp.pre * (SP_FS / 1000u);
    int32_t ca0, ca1, cb0, cb1, ma, mb, dca, dcb;
    const uint32_t L0 = REV_LINE[0] + REV_MOD + 2u, B1 = L0, B2 = B1 + REV_LINE[1], B3 = B2 + REV_LINE[2];
    {   /* the chorus' two read points (Q8 samples back) and line 0's extra length, at both ends of the block */
        int32_t s0 = fx_sine(fx.cho_ph), s1, m0 = fx_sine(fx.rev_ph), m1;
        fx.cho_ph += fx_lfo_inc(fxp.crate);
        fx.rev_ph += fx_lfo_inc(30);
        s1 = fx_sine(fx.cho_ph);
        m1 = fx_sine(fx.rev_ph);
        ca0 = (400 << 8) + ((s0 + 32768) * cdepth >> 8), ca1 = (400 << 8) + ((s1 + 32768) * cdepth >> 8);
        cb0 = (400 << 8) + ((32767 - s0) * cdepth >> 8), cb1 = (400 << 8) + ((32767 - s1) * cdepth >> 8);
        ma = (REV_MOD << 8) + ((m0 * REV_MOD) >> 7), mb = (REV_MOD << 8) + ((m1 * REV_MOD) >> 7);
        dca = (ca1 - ca0) >> 5, dcb = (cb1 - cb0) >> 5;
    }
    for (i = 0; i < n; i++) {
        int32_t yl, yr, x, a, o0, o1, o2, o3;
        /* chorus: two modulated short delays, 5..15 ms, the LFO half a turn apart: left and right move apart */
        cho_buf[fx.cho_w & (CHO_LEN - 1u)] = (int16_t)sp_clamp(cho_in[i] >> 1, -32768, 32767);
        {
            int32_t r0 = ca0 + dca * (int32_t)i, r1 = cb0 + dcb * (int32_t)i;
            uint32_t i0 = (uint32_t)r0 >> 8, i1 = (uint32_t)r1 >> 8;
            int32_t c0 = cho_buf[(fx.cho_w - i0) & (CHO_LEN - 1u)], c1 = cho_buf[(fx.cho_w - i0 - 1u) & (CHO_LEN - 1u)];
            int32_t d0 = cho_buf[(fx.cho_w - i1) & (CHO_LEN - 1u)], d1 = cho_buf[(fx.cho_w - i1 - 1u) & (CHO_LEN - 1u)];
            yl = (c0 + (((c1 - c0) * (r0 & 255)) >> 8)) << 1;
            yr = (d0 + (((d1 - d0) * (r1 & 255)) >> 8)) << 1;
            if (cmix < 32766) {                     /* CHORUS MIX */
                yl = fx_mulq15(yl, cmix);
                yr = fx_mulq15(yr, cmix);
            }
        }
        fx.cho_w++;
        /* delay with a low-passed feedback (in the middle) */
        x = dly_buf[(fx.dly_w - dl) & (DLY_LEN - 1u)];
        fx.dly_lp += fx_mulq15(x - fx.dly_lp, col);
        dly_buf[fx.dly_w & (DLY_LEN - 1u)] =
            (int16_t)sp_clamp((dly_in[i] >> 1) + fx_mulq15(fx.dly_lp, fb), -32768, 32767);
        fx.dly_w++;
        x = fx_mulq15(x << 1, dmix);
        yl += x;
        yr += x;
        /* reverb: two diffusers, then the four lines */
        a = fx_mulq15(rev_in[i], 13000);
        if (pre) {                                  /* REVERB PRE */
            pre_buf[fx.pre_w & (PRE_LEN - 1u)] = (int16_t)sp_clamp(a, -32768, 32767);
            a = pre_buf[(fx.pre_w - pre) & (PRE_LEN - 1u)];
            fx.pre_w++;
        }
        {
            int16_t *c = rev_ap;
            for (k = 0; k < 2u; k++) {
                int32_t b = c[fx.ap_i[k]], v = a + (b >> 1);
                c[fx.ap_i[k]] = (int16_t)sp_clamp(v, -32768, 32767);
                a = b - (v >> 1);
                if (++fx.ap_i[k] >= REV_AP[k])
                    fx.ap_i[k] = 0;
                c += REV_AP[k];
            }
        }
        {
            int16_t *c = rev_line;
            int32_t s0, s1, d0, d1, r = ma + (((mb - ma) * (int32_t)i) >> 5);   /* (between the two: never below 0) */
            uint32_t ri = fx.line_i[0] + ((uint32_t)r >> 8), rj;
            if (ri >= L0)                                   /* (the oldest sample is at line_i: reading */
                ri -= L0;                                   /* past it shortens line 0 by 0..2 REV_MOD) */
            rj = ri + 1u >= L0 ? 0u : ri + 1u;
            o0 = c[ri] + (((c[rj] - c[ri]) * (r & 255)) >> 8);
            o1 = c[B1 + fx.line_i[1]];
            o2 = c[B2 + fx.line_i[2]];
            o3 = c[B3 + fx.line_i[3]];
            s0 = o0 + o1, d0 = o0 - o1, s1 = o2 + o3, d1 = o2 - o3;   /* Hadamard / 2: each feeds all four */
            fx.line_lp[0] += fx_mulq15(((s0 + s1) >> 1) - fx.line_lp[0], lpk);
            fx.line_lp[1] += fx_mulq15(((d0 + d1) >> 1) - fx.line_lp[1], lpk);
            fx.line_lp[2] += fx_mulq15(((s0 - s1) >> 1) - fx.line_lp[2], lpk);
            fx.line_lp[3] += fx_mulq15(((d0 - d1) >> 1) - fx.line_lp[3], lpk);
            c[fx.line_i[0]] = (int16_t)sp_clamp(fx_mulq15(fx.line_lp[0], g) + a, -32768, 32767);
            c[B1 + fx.line_i[1]] = (int16_t)sp_clamp(fx_mulq15(fx.line_lp[1], g) - a, -32768, 32767);
            c[B2 + fx.line_i[2]] = (int16_t)sp_clamp(fx_mulq15(fx.line_lp[2], g) + a, -32768, 32767);
            c[B3 + fx.line_i[3]] = (int16_t)sp_clamp(fx_mulq15(fx.line_lp[3], g) - a, -32768, 32767);
            if (++fx.line_i[0] >= L0) fx.line_i[0] = 0;
            if (++fx.line_i[1] >= REV_LINE[1]) fx.line_i[1] = 0;
            if (++fx.line_i[2] >= REV_LINE[2]) fx.line_i[2] = 0;
            if (++fx.line_i[3] >= REV_LINE[3]) fx.line_i[3] = 0;
        }
        wet_l[i] = yl + o0 + o2;
        wet_r[i] = yr + o1 - o3;
    }
}

/* the sends of the block (sp_channel adds into them), the wet added to out */
/* ---- the DJ filter on the whole mix (sloopDX's): FILTER < 0 a low-pass closing, > 0 a high-pass opening, 0 off.
 * The cutoff glides to the knob (no zipper); back at 0 it opens fully, then it is bypassed. Not saved: a live knob. */
static const uint16_t SP_SVF_G[128] = {    /* tan(pi f / fs) Q12, f = 30 Hz .. 19 kHz exponential */
    9, 9, 10, 10, 11, 11, 12, 12, 13, 14, 15, 15, 16, 17, 18, 19,
    20, 21, 22, 23, 24, 25, 27, 28, 30, 31, 33, 34, 36, 38, 40, 42,
    44, 47, 49, 52, 54, 57, 60, 63, 67, 70, 74, 78, 82, 86, 90, 95,
    100, 105, 111, 117, 123, 129, 136, 143, 150, 158, 166, 175, 184, 194, 204, 214,
    226, 237, 250, 263, 277, 291, 306, 322, 339, 357, 376, 395, 416, 438, 461, 485,
    510, 537, 566, 595, 627, 660, 695, 732, 771, 812, 856, 902, 950, 1002, 1056, 1114,
    1175, 1239, 1308, 1381, 1459, 1542, 1630, 1724, 1825, 1933, 2050, 2175, 2310, 2457, 2616, 2790,
    2981, 3191, 3425, 3685, 3978, 4311, 4692, 5136, 5659, 6288, 7062, 8040, 9323, 11093, 13709, 18007
};
static struct {
    int8_t v, res;                              /* the knobs: -64..63, 0..127 */
    int8_t mode;                                /* -1 LP, 1 HP, 0 off */
    int32_t cut;                                /* now, 0..127 << 8 */
    int32_t l1, l2, r1, r2;
} djf = {0, 40, 0, 0, 0, 0, 0, 0};

static void sp_djf_block(int32_t *out)
{
    int32_t v = djf.v, to, i, g, k, den, a1, a2, a3, c;
    if (v < 0 && djf.mode >= 0) {               /* (switching side: from open) */
        djf.mode = -1;
        djf.cut = 127 << 8;
        djf.l1 = djf.l2 = djf.r1 = djf.r2 = 0;
    } else if (v > 0 && djf.mode <= 0) {
        djf.mode = 1;
        djf.cut = 0;
        djf.l1 = djf.l2 = djf.r1 = djf.r2 = 0;
    }
    if (!djf.mode)
        return;
    to = djf.mode < 0 ? (v < 0 ? (127 << 8) + v * 90 * 4 : 127 << 8) : (v > 0 ? v * 90 * 4 : 0);
    djf.cut += sp_clamp(to - djf.cut, -384, 384);
    if (!v && djf.cut == to) {
        djf.mode = 0;
        return;
    }
    c = sp_clamp(djf.cut, 0, 127 << 8);         /* the TPT state-variable filter's coefficients (sloopDX's tsvf) */
    g = SP_SVF_G[c >> 8];
    if ((c >> 8) < 127)
        g += ((SP_SVF_G[(c >> 8) + 1] - g) * (c & 255)) >> 8;
    k = 8192 - djf.res * 7600 / 127;
    den = 4096 + ((g * (g + k)) >> 12);
    a1 = (int32_t)((4096u << 13) / (uint32_t)den);
    a2 = (a1 * g) >> 12;
    a3 = (a2 * g) >> 12;
    for (i = 0; i < SP_BLK; i++) {
        int32_t x = sp_clamp(out[2 * i], -140000, 140000), y = sp_clamp(out[2 * i + 1], -140000, 140000), v1, v2, v3;
        v3 = x - djf.l2;
        v1 = (a1 * djf.l1 + a2 * v3) >> 13;
        v2 = djf.l2 + ((a2 * djf.l1 + a3 * v3) >> 13);
        djf.l1 = sp_clamp(2 * v1 - djf.l1, -150000, 150000);
        djf.l2 = sp_clamp(2 * v2 - djf.l2, -150000, 150000);
        out[2 * i] = djf.mode < 0 ? v2 : x - v2;
        v3 = y - djf.r2;
        v1 = (a1 * djf.r1 + a2 * v3) >> 13;
        v2 = djf.r2 + ((a2 * djf.r1 + a3 * v3) >> 13);
        djf.r1 = sp_clamp(2 * v1 - djf.r1, -150000, 150000);
        djf.r2 = sp_clamp(2 * v2 - djf.r2, -150000, 150000);
        out[2 * i + 1] = djf.mode < 0 ? v2 : y - v2;
    }
}

static void sp_fx_block(int32_t *out)
{
    static int32_t wl[SP_BLK], wr[SP_BLK];
    uint32_t i;
    fx_buses(sp_bus[0], sp_bus[1], sp_bus[2], wl, wr, SP_BLK);
    for (i = 0; i < SP_BLK; i++) {
        out[2u * i] += wl[i];
        out[2u * i + 1u] += wr[i];
    }
    sp_djf_block(out);
}
