/* SPDX-License-Identifier: GPL-3.0-only */
/* ZP12 on the VIBE – the core: zp12's own engine (../firmware/src, the same files the FM-1 builds), the own samples
 * in one pool with a time budget, and what stays when the VIBE is switched off.
 *
 *   the engine   sp_core.c (12-bit waves, pitch by the playback rate without interpolation, 8 channels with their
 *                filters, the master), sp_fx.c (chorus, delay, reverb, the DJ filter), sp_seq.c (loops, songs) –
 *                at the host's rate (SP_FS), with 16-bit waves (SP_WIDE) and fine START / END (SP_TR)
 *   own samples  int16 in `pool`, packed one after the other; a wave plays at 12 bits (the low four dropped, as the
 *                kit's packing) or at 16. The budget counts what the original would: 12 bits = 1.5 bytes a sample,
 *                16 bits = 2 – so 16 bits costs a quarter more time. The kit is free (the factory's ROM). */
#include "vibe.h"
#include "vibe_gfx.h"
#include "vibe_dsp.h"
void *memcpy(void *d, const void *s, size_t n);   /* (the SDK's: no libc in a firmware) */
void *memmove(void *d, const void *s, size_t n);
void *memset(void *d, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);

static uint32_t sp_fs = 48000u;                  /* the host's rate (vibe_on_init) */
static uint32_t sq_ticks_k = 69905u;             /* sp_seq.c: ticks Q16 a block per 0.1 BPM x 10000, at sp_fs */
#define SP_FS sp_fs
#define SQ_TICKS_K sq_ticks_k
#define SP_WIDE 1
#define SP_NWAVE 128
#define SP_TR 60000u
#define SP_POOL
#define SP_WITH_FX 1
#include "sp_core.c"
#include "sp_fx.c"
#include "zp12_kit.h"
#include "sp_seq.c"
/* a hit: to the sequencer (it sounds it, and records it while REC) */
#define SP_HIT(k, vel, semis) sq_post(RQ_HIT, (uint32_t)(k), (uint32_t)(vel) | (uint32_t)((semis) + 64) << 8)

static void say(const char *s);                  /* zv_ui.h: a line on the screen for a while */
static uint32_t now_ms(void) { return (uint32_t)(vibe_frames() * 1000u / sp_fs); }
static void scopy(char *d, const char *s, int n) { int i = 0; for (; s && s[i] && i < n - 1; i++) d[i] = s[i]; d[i] = 0; }
static void scat(char *d, const char *s, int n) { int i = 0; while (d[i]) i++; scopy(d + i, s, n - i); }
static void icat(char *d, int v, int n) { char b[16]; scat(d, vg_itoa(v, b), n); }
static void icat2(char *d, int v, int n) { if (v < 10 && v >= 0) scat(d, "0", n); icat(d, v, n); }

/* ---------------------------------------------------------------- own samples ----------------------------------- */
#define POOL_MAX 15700000u                       /* int16: the 5-minute budget twice (a take may replace a sample that */
static int16_t pool[POOL_MAX];                   /*  still lies in the pool while it records) */
static uint32_t pool_top;                        /* samples in use: the own waves, packed from 0 */
typedef struct { uint32_t off; char name[9]; uint8_t used; } own_t;
static own_t own[SP_NWAVE];                      /* index = the wave's (sp_wave[]); the kit's are below KIT_NWAVE */
static const uint16_t BUDGET_S[4] = { 10, 30, 120, 300 };
static const char *const BUDGET_N[4] = { "10 S", "30 S", "2 MIN", "5 MIN" };
static int budget_i = 2;                         /* SETUP → TIME: 2 minutes unless chosen */
#define NIB_S (26040u * 3u)                      /* a second of 12 bits at 26.04 kHz, in nibbles */
static uint32_t budget_nib(void) { return BUDGET_S[budget_i] * NIB_S; }
static uint32_t wave_nib(int w) { return own[w].used ? sp_wave[w].n * (sp_wave[w].fmt == SPW_16 ? 4u : 3u) : 0u; }
static uint32_t used_nib(void) { uint32_t s = 0; for (int w = KIT_NWAVE; w < SP_NWAVE; w++) s += wave_nib(w); return s; }
static int wave_is_own(int w) { return w >= KIT_NWAVE && w < SP_NWAVE && own[w].used; }
static const char *wave_name(int w)
{
    if (w < KIT_NWAVE) return KIT_WAVE[w].name;
    if (w < SP_NWAVE && own[w].used) return own[w].name;
    return "--";
}
static int wave_users(int w, int except)          /* pads that play wave w (but `except`) */
{
    int n = 0;
    for (int k = 0; k < SP_NSOUND; k++) if (k != except && sp_sound[k].wave == w) n++;
    return n;
}
static int wave_slot(void)                        /* a free index for a new own wave, -1: none */
{
    for (int w = KIT_NWAVE; w < SP_NWAVE; w++) if (!own[w].used) return w;
    return -1;
}

static void store_forget(int w);                 /* zv_core.h, below */
static uint32_t st_changed;                      /* when something worth keeping changed (the autosave) */
static int st_dirty;
static void touched(void) { st_dirty = 1; st_changed = now_ms(); }

/* the factory sound of pad k: as the kit has it, START / END in this firmware's scale */
static sp_sound_t kit_pad(int k)
{
    sp_sound_t s = KIT_PADS[k & 31];
    s.start = (uint16_t)(s.start * (SP_TR / 1000u));
    s.end = (uint16_t)(s.end >= 1000u ? SP_TR : s.end * (SP_TR / 1000u));
    return s;
}

/* an own wave out: its room closes (the waves after it move down), its pads get their factory sound back */
static void wave_free(int w)
{
    if (!wave_is_own(w)) return;
    uint32_t off = own[w].off, n = sp_wave[w].n;
    for (int c = 0; c < SP_NCH; c++)
        if (sp_ch[c].w == &sp_wave[w]) { sp_ch[c].on = 0; sp_ch[c].tail = 0; sp_ch[c].w = 0; }
    for (uint32_t i = off; i + n < pool_top; i++) pool[i] = pool[i + n];   /* (down: forward is safe; a sample at a time, not a byte) */
    pool_top -= n;
    store_forget(w);                              /* (while n still says how many pieces it has on the card) */
    for (int i = KIT_NWAVE; i < SP_NWAVE; i++)
        if (own[i].used && own[i].off > off) { own[i].off -= n; sp_wave[i].d = (const uint8_t *)(const void *)(pool + own[i].off); }
    own[w].used = 0;
    sp_wave[w].n = 0;
    sp_wave[w].d = 0;
    for (int k = 0; k < SP_NSOUND; k++)
        if (sp_sound[k].wave == w) sp_sound[k] = kit_pad(k);
    touched();
}

/* ---------------------------------------------------------------- what stays ------------------------------------ *
 * state.bin  the pads' sounds, the mix, the effects, the sequencer's settings and songs, the own waves' table
 * seq.bin    the 16 loops' hits; lk.bin their locks
 * wNNN-CCC.raw  an own wave in pieces of 64 KB (one piece a frame is written: no gap in the sound) */
#define ST_MAGIC 0x3231505Au                     /* "ZP12" */
#define ST_VER 1u
#define CHUNK 32768u                             /* samples a piece */
typedef struct { uint32_t n; uint16_t rate; uint8_t fmt, used; char name[9]; } wmeta_t;
static uint16_t w_next[SP_NWAVE];                /* the next piece of wave w to write (0xFFFF: written) */
static uint16_t w_drop[SP_NWAVE];                /* pieces of an old wave w still to delete */
static uint8_t stb[16384];
static uint8_t *stp;
static int st_left;
static void W(const void *p, int n) { if (n > st_left) { st_left = -1; return; } memcpy(stp, p, (size_t)n); stp += n; st_left -= n; }
static void R(void *p, int n) { if (n > st_left) { st_left = -1; return; } memcpy(p, stp, (size_t)n); stp += n; st_left -= n; }
static void chunk_name(char *b, int w, int c)
{
    b[0] = 0; scat(b, "w", 16); if (w < 100) scat(b, "0", 16); icat2(b, w, 16); scat(b, "-", 16);
    if (c < 100) scat(b, "0", 16); icat2(b, c, 16); scat(b, ".raw", 16);
}
static uint32_t chunks_of(uint32_t n) { return (n + CHUNK - 1u) / CHUNK; }
static void store_wave_later(int w) { w_next[w] = 0; }
static void store_forget(int w)
{
    w_next[w] = 0xFFFF;
    w_drop[w] = (uint16_t)chunks_of(sp_wave[w].n);
}
static int store_state(void)
{
    stp = stb; st_left = (int)sizeof stb;
    uint32_t m = ST_MAGIC, v = ST_VER;
    W(&m, 4); W(&v, 4);
    W(sp_sound, sizeof sp_sound); W(sp_mix, sizeof sp_mix); W(&fxp, sizeof fxp); W(&sp_smooth, 1);
    W(&sq.quant, 1); W((const void *)&sq.swing, 1); W(&sq.click, 1); W(&sq.cin_bars, 1); W(&sq.dub_bar, 1);
    W(&sq.bpm10, 2); W((const void *)&sq.seg, 1); W(&sq.song_sel, 1);
    W(sq_songs, sizeof sq_songs); W(sq_song_len, sizeof sq_song_len);
    uint8_t b = (uint8_t)budget_i; W(&b, 1);
    for (int w = KIT_NWAVE; w < SP_NWAVE; w++) {
        wmeta_t t = { 0 };
        if (own[w].used) { t.n = sp_wave[w].n; t.rate = sp_wave[w].rate; t.fmt = sp_wave[w].fmt; t.used = 1; scopy(t.name, own[w].name, 9); }
        W(&t, sizeof t);
    }
    if (st_left < 0) return -1;
    int r = vibe_store_write("state.bin", stb, (int)(stp - stb));
    r |= vibe_store_write("seq.bin", sq_seg, sizeof sq_seg);
    r |= vibe_store_write("lk.bin", sq_lk, sizeof sq_lk);
    return r;
}
/* one piece of a wave (or one old piece deleted); 1 = something was done */
static int store_step(void)
{
    char nm[24];
    for (int w = KIT_NWAVE; w < SP_NWAVE; w++)
        if (w_drop[w]) {
            w_drop[w]--;
            chunk_name(nm, w, w_drop[w]);
            vibe_store_delete(nm);
            if (w_drop[w] && w_drop[w] % 16u) continue;   /* (deletes are cheap: 16 at a time) */
            return 1;
        }
    for (int w = KIT_NWAVE; w < SP_NWAVE; w++)
        if (own[w].used && w_next[w] != 0xFFFF) {
            uint32_t c = w_next[w], n = sp_wave[w].n, a = c * CHUNK, k = n - a < CHUNK ? n - a : CHUNK;
            chunk_name(nm, w, (int)c);
            vibe_store_write(nm, pool + own[w].off + a, (int)(k * sizeof pool[0]));
            w_next[w] = c + 1u >= chunks_of(n) ? 0xFFFF : (uint16_t)(c + 1u);
            if (w_next[w] == 0xFFFF) st_dirty = 1;          /* (the table says it is complete only after it) */
            return 1;
        }
    return 0;
}
static int store_pending(void)
{
    for (int w = KIT_NWAVE; w < SP_NWAVE; w++) if (w_drop[w] || (own[w].used && w_next[w] != 0xFFFF)) return 1;
    return 0;
}
static void store_all(void)                       /* everything, now (VIBE_EV_SAVE, SAVE) */
{
    while (store_step()) {}
    store_state();
    st_dirty = 0;
}
static int store_load(void)
{
    int n = vibe_store_read("state.bin", stb, sizeof stb);
    if (n < 8) return 0;
    stp = stb; st_left = n;
    uint32_t m = 0, v = 0;
    R(&m, 4); R(&v, 4);
    if (m != ST_MAGIC || v != ST_VER) return 0;
    R(sp_sound, sizeof sp_sound); R(sp_mix, sizeof sp_mix); R(&fxp, sizeof fxp); R(&sp_smooth, 1);
    R(&sq.quant, 1); R((void *)&sq.swing, 1); R(&sq.click, 1); R(&sq.cin_bars, 1); R(&sq.dub_bar, 1);
    R(&sq.bpm10, 2); R((void *)&sq.seg, 1); R(&sq.song_sel, 1);
    R(sq_songs, sizeof sq_songs); R(sq_song_len, sizeof sq_song_len);
    uint8_t b = 2; R(&b, 1); budget_i = b < 4 ? b : 2;
    wmeta_t t[SP_NWAVE];
    for (int w = KIT_NWAVE; w < SP_NWAVE; w++) R(&t[w], sizeof t[w]);
    if (st_left < 0) return 0;
    if (vibe_store_read("seq.bin", sq_seg, sizeof sq_seg) != (int)sizeof sq_seg) sq_init();
    else if (vibe_store_read("lk.bin", sq_lk, sizeof sq_lk) != (int)sizeof sq_lk) memset(sq_lk, 0, sizeof sq_lk);
    pool_top = 0;
    for (int w = KIT_NWAVE; w < SP_NWAVE; w++) {
        w_next[w] = 0xFFFF;
        if (!t[w].used || !t[w].n || pool_top + t[w].n > POOL_MAX) continue;
        int ok = 1;
        char nm[24];
        for (uint32_t c = 0; c < chunks_of(t[w].n) && ok; c++) {
            uint32_t a = c * CHUNK, k = t[w].n - a < CHUNK ? t[w].n - a : CHUNK;
            chunk_name(nm, w, (int)c);
            ok = vibe_store_read(nm, pool + pool_top + a, (int)(k * sizeof pool[0])) == (int)(k * sizeof pool[0]);
        }
        if (!ok) continue;                        /* (a wave not complete on the card: its pads get their factory sound) */
        own[w].used = 1; own[w].off = pool_top; scopy(own[w].name, t[w].name, 9);
        sp_wave[w].d = (const uint8_t *)(const void *)(pool + pool_top); sp_wave[w].n = t[w].n;
        sp_wave[w].rate = t[w].rate; sp_wave[w].fmt = t[w].fmt <= SPW_16 ? t[w].fmt : SPW_16AT12;
        pool_top += t[w].n;
    }
    for (int k = 0; k < SP_NSOUND; k++)
        if (sp_sound[k].wave >= SP_NWAVE || (sp_sound[k].wave >= KIT_NWAVE && !own[sp_sound[k].wave].used)) sp_sound[k] = kit_pad(k);
    for (int s = 0; s < (int)SQ_SONGS; s++) if (sq_song_len[s] > SQ_NSONG) sq_song_len[s] = 0;
    if (sq.bpm10 < 300 || sq.bpm10 > 3000) sq.bpm10 = 900;
    sq.seg %= SQ_NSEG; sq.quant %= 7u; sq.swing %= 6u; sq.click %= 3u; sq.cin_bars %= 3u; sq.dub_bar &= 1u; sq.song_sel &= 3u;
    return 1;
}
