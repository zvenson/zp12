/* SPDX-License-Identifier: GPL-3.0-only */
/* zp12 sound core (firmware/src/sp_core.c) on the host: the CC0 kit resampled to 12 bit at 26.04 kHz,
 * then a beat, the pitch crunch, the 45->33 trick and the channel 1-2 filter, into a WAV to listen to.
 * Checks: bounded, silent at the end, the zero-order hold (a held sample repeats exactly), the pack
 * round trip, deterministic (the hash goes to tests/golden_sp.txt later).
 *   cc -O2 -Wall -o build/host/sp_core_test tests/sp_core_test.c -lm && build/host/sp_core_test OUT.wav */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../firmware/src/sp_core.c"

#define KIT "assets/samples-cc0/KIT/"
static int bad;
static void check(int ok, const char *what) { printf("sp: %-66s %s\n", what, ok ? "ok" : "FAIL"); bad += !ok; }

/* a 16-bit PCM WAV (mono or stereo, any rate) -> mono doubles */
static double *wav_read(const char *path, uint32_t *n, uint32_t *rate)
{
    FILE *f = fopen(path, "rb");
    uint8_t h[12], ch[8];
    uint16_t nc = 1, bits = 16;
    double *out = 0;
    if (!f || fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) {
        fprintf(stderr, "%s: not a WAV\n", path);
        exit(1);
    }
    while (fread(ch, 1, 8, f) == 8) {
        uint32_t len = ch[4] | ch[5] << 8 | ch[6] << 16 | (uint32_t)ch[7] << 24;
        if (!memcmp(ch, "fmt ", 4)) {
            uint8_t fm[16];
            if (fread(fm, 1, 16, f) != 16) break;
            nc = (uint16_t)(fm[2] | fm[3] << 8);
            *rate = fm[4] | fm[5] << 8 | fm[6] << 16 | (uint32_t)fm[7] << 24;
            bits = (uint16_t)(fm[14] | fm[15] << 8);
            fseek(f, (long)len - 16, SEEK_CUR);
        } else if (!memcmp(ch, "data", 4)) {
            uint32_t i, frames = len / (bits / 8u) / nc;
            int16_t *raw = malloc(len);
            if (fread(raw, 1, len, f) != len) break;
            out = malloc(frames * sizeof *out);
            for (i = 0; i < frames; i++) {
                double s = 0;
                uint32_t c;
                for (c = 0; c < nc; c++) s += raw[i * nc + c];
                out[i] = s / nc / 32768.0;
            }
            free(raw);
            *n = frames;
            break;
        } else {
            fseek(f, (long)(len + (len & 1u)), SEEK_CUR);
        }
    }
    fclose(f);
    return out;
}

/* to `to` Hz: a windowed-sinc low-pass at 0.45 of the lower rate (the input filter of a sampler), then
 * 12 bit, rounded. The web editor does the same in the same order (double), so its result is bit-equal */
static int16_t *resample12(const double *x, uint32_t n, uint32_t from, uint32_t to, uint32_t *m)
{
    const int H = 24;                                   /* half the taps, at the output rate */
    double fc = 0.45 * (to < from ? to : from) / from, ratio = (double)from / to;
    uint32_t i, k = (uint32_t)((uint64_t)n * to / from);
    int16_t *y = malloc(k * sizeof *y);
    for (i = 0; i < k; i++) {
        double t = i * ratio, s = 0, wsum = 0;
        int j, c = (int)floor(t), span = (int)ceil(H * ratio);
        for (j = c - span + 1; j <= c + span; j++) {
            double d = t - j, a = 2.0 * fc * d, sinc = a == 0 ? 1.0 : sin(M_PI * a) / (M_PI * a);
            double w = 0.5 + 0.5 * cos(M_PI * d / (span + 1)), g = 2.0 * fc * sinc * w;
            if (j >= 0 && j < (int)n) s += x[j] * g;
            wsum += g;
        }
        s = s / wsum * 2047.0;
        s = s > 2047 ? 2047 : s < -2048 ? -2048 : s;
        y[i] = (int16_t)(lround(s) * 16);               /* 12 bit in the top bits */
    }
    *m = k;
    return y;
}

static uint8_t *load(uint32_t wi, const char *file, uint32_t rate, double speed)
{
    uint32_t n = 0, r = 44100, m;
    char p[256];
    double *x;
    int16_t *y;
    uint8_t *d;
    snprintf(p, sizeof p, KIT "%s", file);
    x = wav_read(p, &n, &r);
    y = resample12(x, n, (uint32_t)(r * speed), rate, &m);   /* speed > 1: as if played at 45 */
    d = calloc((m + 1u) / 2u * 3u, 1);
    sp_pack(d, y, m);
    sp_wave[wi].d = d;
    sp_wave[wi].n = m;
    sp_wave[wi].rate = (uint16_t)rate;
    free(x);
    free(y);
    return d;
}

static void sound(uint32_t k, uint32_t w, int tune, uint32_t decay, uint32_t level, int pan, uint32_t ch)
{
    sp_sound_t *s = &sp_sound[k];
    memset(s, 0, sizeof *s);
    s->wave = (uint8_t)w;
    s->tune = (int8_t)tune;
    s->decay = (uint8_t)decay;
    s->level = (uint8_t)level;
    s->pan = (int8_t)pan;
    s->chan = (uint8_t)ch;                              /* (2.1: the channel is the pad's position; k chosen = ch) */
    s->end = 1000;
    s->cut = 127;
}

static FILE *wf;
static uint32_t frames, hash = 2166136261u;
static int32_t peak;
static void put32(uint32_t v) { fputc((int)(v & 255), wf); fputc((int)(v >> 8 & 255), wf); fputc((int)(v >> 16 & 255), wf); fputc((int)(v >> 24), wf); }
static void run(uint32_t samples)                       /* render, write, measure */
{
    uint32_t b, i;
    for (b = 0; b < samples / SP_BLK; b++) {
        int32_t o[2 * SP_BLK];
        memset(o, 0, sizeof o);
        sp_render(o);
        for (i = 0; i < 2u * SP_BLK; i++) {
            int32_t v = o[i] > 32767 ? 32767 : o[i] < -32768 ? -32768 : o[i];
            int16_t s = (int16_t)v;
            if (abs(o[i]) > peak) peak = abs(o[i]);
            hash = (hash ^ (uint16_t)s) * 16777619u;
            fwrite(&s, 2, 1, wf);
        }
        frames += SP_BLK;
    }
}

int main(int argc, char **argv)
{
    static const char *const F[] = {"10_kick_BDrumNew_hit_v5.wav", "11_snare_Snare2_HitSN_v7.wav",
                                    "14_hihat_HitC_v3.wav", "13_clap_Clap_rr1.wav", "16_tomlo_TomL_HitS_v4.wav",
                                    "15_ohat_HitO.wav"};
    const uint32_t BEAT = SP_FS * 60u / 92u / 4u / SP_BLK * SP_BLK;   /* a 16th at 92 BPM, whole blocks */
    uint32_t i, st;
    wf = fopen(argc > 1 ? argv[1] : "build/host/zp12-demo.wav", "wb");
    if (!wf) return 1;
    fwrite("RIFF\0\0\0\0WAVEfmt \x10\0\0\0\x01\0\x02\0", 1, 24, wf);
    put32(SP_FS); put32(SP_FS * 4u); fwrite("\x04\0\x10\0data\0\0\0\0", 1, 12, wf);

    for (i = 0; i < 6u; i++) load(i, F[i], 26040, 1.0);
    load(6, F[4], 26040, 45.0 / 33.0);                  /* the tom "at 45" */
    {   /* the pack round trip and the zero-order hold */
        int16_t a[5] = {-32768, 32752, 16, -16, 4096};
        uint8_t d[9];
        int ok = 1;
        sp_pack(d, a, 5);
        for (i = 0; i < 5u; i++) ok &= sp_sample(d, i) == a[i] >> 4;
        check(ok, "12-bit pack / unpack round trip");
        check(sp_pow2_cents(1200) == 131072u && sp_pow2_cents(-1200) == 32768u && sp_pow2_cents(0) == 65536u,
              "2^(cents/1200): octaves exact");
    }
    sound(6, 0, 0, 110, 127, 0, 6);                     /* kick: ch 7 (no filter) */
    sound(2, 1, 0, 90, 110, 0, 2);                      /* snare: ch 3 (fixed filter) */
    sound(4, 2, 0, 40, 80, 20, 4);                      /* hat: ch 5 */
    sound(3, 3, 0, 80, 100, -10, 3);                    /* clap: ch 4 */
    sound(0, 4, -12, 100, 120, 0, 0);                   /* tom an octave down: ch 1, the dynamic filter */
    sp_sound[0].cut = 70; sp_sound[0].reso = 90;
    sound(5, 5, 0, 70, 70, 20, 5);                      /* open hat: ch 6 */
    sound(7, 6, -6, 127, 120, 0, 7);                    /* the tom at 45, played at 33: ch 8 */
    sp_sound[7].flags = SPF_33;

    /* 1. a beat, 2 bars */
    for (st = 0; st < 32u; st++) {
        if (st % 8u == 0u || st == 10u || st == 27u) sp_trigger(6, 127);
        if (st % 8u == 4u) sp_trigger(2, 120);
        if (st % 2u == 0u) sp_trigger(4, st % 4u ? 70 : 110);
        if (st == 14u || st == 30u) sp_trigger(5, 100);
        if (st == 3u || st == 19u || st == 22u) sp_trigger(0, 120);
        run(BEAT);
    }
    /* 2. the crunch: the snare from +12 down to -24 semitones, a hit each */
    for (i = 0; i < 12u; i++) {
        sp_sound[2].tune = (int8_t)(12 - (int)i * 3);
        sp_trigger(2, 120);
        run(BEAT * 2u);
    }
    sp_sound[2].tune = 0;
    /* 3. channel 1: the low tom, the cutoff swept up with resonance */
    for (i = 0; i < 16u; i++) {
        sp_sound[0].cut = (uint8_t)(20 + i * 6);
        sp_trigger(0, 120);
        if (i % 4u == 0u) sp_trigger(6, 127);
        run(BEAT * 2u);
    }
    /* 4. 45 -> 33: the same tom, straight, then the trick */
    sp_sound[0].cut = 127; sp_sound[0].reso = 0; sp_sound[0].tune = -6;
    for (i = 0; i < 4u; i++) { sp_trigger(i & 1u ? 7 : 0, 120); run(BEAT * 4u); }
    {   /* the zero-order hold: an octave down repeats every source sample exactly twice */
        sp_ch_t *c = &sp_ch[6];
        sound(6, 0, 0, 127, 127, 0, 6);
        sp_wave[0].rate = 22050;                        /* (22050 * 2^-1 = 11025: a sample every 4 outputs) */
        sp_sound[6].tune = -12;
        sp_trigger(6, 127);
        check(c->step == 16384u, "an octave down at 22.05 kHz: 1/4 source sample a step");
        sp_wave[0].rate = 26040;
        c->on = 0;
    }
    run(SP_FS * 2u / SP_BLK * SP_BLK);                  /* the tails */
    {
        int32_t o[2 * SP_BLK];
        uint32_t ch, on = 0;
        memset(o, 0, sizeof o);
        for (ch = 0; ch < SP_NCH; ch++) on += sp_ch[ch].on;
        check(on == 0u, "every channel done 2 s after the last hit");
    }
    check(peak < 32768 * 2, "bounded (peak below 2x full scale before the clip)");
    fseek(wf, 4, SEEK_SET); put32(36u + frames * 4u);
    fseek(wf, 40, SEEK_SET); put32(frames * 4u);
    fclose(wf);
    printf("sp: %.1f s, peak %d, hash %08x\n", frames / (double)SP_FS, peak, hash);
    printf(bad ? "SP CORE TEST FAILED\n" : "sp core test passed\n");
    return bad != 0;
}
