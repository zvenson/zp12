/* SPDX-License-Identifier: GPL-3.0-only */
/* PUNCH-IN: four moves of the sampler era, after SLOOP's punch.c (isod89, GPL-3.0), kept to what a 12-bit drum
 * machine of the 80s was made to do by hand, tape and a turntable. Hold FX and a white key 1-4 (zp12.c):
 *   1 ROLL 1/8, 2 ROLL 1/16: the slice since the last 1/8 or 1/16 of the playhead, repeated (stopped: from now)
 *   3 REVERSE: the last beat backwards, over and over
 *   4 TAPE STOP: the mix slows to a halt over a beat
 * The whole mix, mono as the machine was, from a ring of the last 0.74 s kept at half the rate (22 kHz, held
 * two samples on the way out: the old samplers' grain, and half the room); every change crossfades over 64
 * samples. Runs in the audio ISR after sp_render, before MASTER (zp12.c). */
#define PN_N 16384u                               /* power of two: 0.74 s at 22 kHz */
#define PN_FR (PN_N * 2u)                         /* the frames it holds */
#define PN_FADE 512                               /* Q15 a sample: 64 samples */
enum { PN_ROLL8, PN_ROLL16, PN_REV, PN_STOP, PN_NFX };
static int16_t pn_ring[PN_N] __attribute__((section(".pool")));
static struct {
    volatile int8_t req;                          /* asked for by the keys (-1: none), the main loop */
    int8_t cur;                                   /* playing (fading out while req differs) */
    int32_t g;                                    /* wet, Q15 */
    uint32_t w;                                   /* frames written (the ring holds every pair as one) */
    int32_t half;                                 /* the pair's first mono sample */
    uint32_t start, len, k, frz;                  /* roll / reverse: the slice, where in it, the ring held */
    uint32_t rp, spd, dspd;                       /* tape stop: read position Q16, speed Q16, its step */
} pn = {.req = -1, .cur = -1};

static uint32_t pn_beat(void)                     /* samples a beat (bpm10 40.0..240.0: 11025..66150) */
{
    return 26460000u / (sq.bpm10 ? sq.bpm10 : 900u);   /* 44100 * 600 / bpm10 */
}

/* samples since the playhead's last division of div ticks (stopped: none), at the end of this half */
static uint32_t pn_since(uint32_t div, uint32_t len)
{
    uint32_t pos = sq.pos, spt = pn_beat() / SQ_PPQ, s;
    if (!sq.playing || sq.countin > 0)
        return 0;
    s = ((pos >> 16) % div) * pn_beat() / SQ_PPQ + (((pos & 0xFFFFu) * spt) >> 16);
    return s % len;
}

static void pn_start(int32_t fx, uint32_t frames)
{
    uint32_t beat = pn_beat(), since;
    pn.frz = 0;
    pn.k = 0;
    switch (fx) {
    case PN_ROLL8:
    case PN_ROLL16:
        pn.len = beat / (fx == PN_ROLL8 ? 2u : 4u);
        if (pn.len > PN_FR / 2u) pn.len = PN_FR / 2u;
        since = pn_since(fx == PN_ROLL8 ? SQ_PPQ / 2u : SQ_PPQ / 4u, pn.len);
        since = (since + pn.len - frames % pn.len) % pn.len;      /* (back to this half's first frame) */
        pn.start = pn.w - since;                                  /* the slice began there: heard live until its end */
        pn.k = since;
        break;
    case PN_REV:
        pn.len = beat < PN_FR - 512u ? beat : PN_FR - 512u;
        pn.start = pn.w;
        pn.frz = 1;
        break;
    case PN_STOP:
        pn.rp = pn.w << 16;
        pn.spd = 65536u;
        pn.dspd = 65536u / beat + 1u;
        break;
    default:
        break;
    }
}

static void pn_feed(int32_t m)                     /* a mono frame (mix / 8 + mix / 8): every second one stores the pair */
{
    if (pn.w & 1u)
        pn_ring[(pn.w >> 1) & (PN_N - 1u)] = (int16_t)sp_clamp((pn.half + m) >> 1, -32768, 32767);
    else
        pn.half = m;
    pn.w++;
}
static int32_t pn_at(uint32_t f) { return pn_ring[(f >> 1) & (PN_N - 1u)]; }   /* the frame f, held */

#define PN_EDGE 32u
static int32_t pn_edge(int32_t y)                 /* the slice's ends, 32 samples in and out: no click at the seam */
{
    uint32_t e = pn.k < pn.len - pn.k ? pn.k : pn.len - pn.k;
    return e < PN_EDGE ? y * (int32_t)e / (int32_t)PN_EDGE : y;
}

/* n stereo frames of the mix (interleaved, the scale sp_out takes), in place */
static void sp_punch(int32_t *o, uint32_t n)
{
    uint32_t i;
    if (pn.cur < 0 && pn.req < 0 && !pn.g) {                    /* idle: only the ring is fed */
        for (i = 0; i < n; i++)
            pn_feed((o[2u * i] + o[2u * i + 1u]) >> 3);
        return;
    }
    for (i = 0; i < n; i++) {
        int32_t l = o[2u * i], r = o[2u * i + 1u], y = 0;
        if (pn.req != pn.cur) {                                   /* a change: out to dry, then in */
            if (pn.g > 0) pn.g = pn.g > PN_FADE ? pn.g - PN_FADE : 0;
            if (!pn.g) {
                pn.cur = pn.req;
                if (pn.cur >= 0) pn_start(pn.cur, n - i);
            }
        } else if (pn.cur >= 0 && pn.g < 32768) {
            pn.g = pn.g + PN_FADE < 32768 ? pn.g + PN_FADE : 32768;
        }
        if (!pn.frz)
            pn_feed((l + r) >> 3);
        switch (pn.cur) {
        case PN_ROLL8:
        case PN_ROLL16:
            y = pn.frz ? pn_at(pn.start + pn.k) : (l + r) >> 3;  /* (the first pass is the slice being played: live) */
            y = pn_edge(y);
            if (++pn.k >= pn.len) { pn.k = 0; pn.frz = 1; }      /* (the slice is whole: the ring holds it) */
            break;
        case PN_REV:
            y = pn_edge(pn_at(pn.start - 1u - pn.k));
            if (++pn.k >= pn.len) pn.k = 0;
            break;
        case PN_STOP:
            if (pn.spd) {
                uint32_t q = pn.rp >> 1, a = q >> 16, f = (q & 0xFFFFu) >> 1;   /* (in the ring's pairs) */
                int32_t s0 = pn_ring[a & (PN_N - 1u)], s1 = pn_ring[(a + 1u) & (PN_N - 1u)];
                y = s0 + (((s1 - s0) * (int32_t)f) >> 15);
                pn.rp += pn.spd;
                pn.spd = pn.spd > pn.dspd ? pn.spd - pn.dspd : 0;
            }
            break;
        default:
            break;
        }
        if (pn.g) {
            y <<= 2;                                              /* (the ring keeps the mix / 4, mono) */
            o[2u * i] = l + (int32_t)(((int64_t)(y - l) * pn.g) >> 15);
            o[2u * i + 1u] = r + (int32_t)(((int64_t)(y - r) * pn.g) >> 15);
        }
        if (pn.cur < 0 && pn.req < 0 && !pn.g)
            pn.frz = 0;
    }
}
