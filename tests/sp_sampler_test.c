/* SPDX-License-Identifier: GPL-3.0-only */
/* The sampler (firmware/src/sp_sampler.c) on the host: the free run beside the samples there, a take of a 1 kHz
 * sine played in over "USB" in 44 / 45-frame packets, THRESH skipping the silence before it, REC ending it early,
 * the directory the web editor reads. The flash is RAM here (erased = 0xFF; a write can only clear bits).
 *   cc -O2 -Wall -Wno-unused-function -Ifirmware/src -o build/host/sp_sampler_test tests/sp_sampler_test.c -lm && build/host/sp_sampler_test */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static uint32_t ms_reads;
#define fm1_ms (ms_reads++ / 64u)                        /* (time passes as it is looked at: no loop waits forever) */
static uint8_t flash[0x100000], flash_ok = 1;
static int bad_write;
#define FL_IN(off, n, lo, hi) ((uint32_t)(off) >= (lo) && (uint32_t)(off) <= (hi) && (uint32_t)(n) <= (hi) - (uint32_t)(off))
#define FL_XIP(off) (flash + (off))
static int st_read(uint32_t off, void *d, uint32_t n) { memcpy(d, flash + off, n); return 0; }
static int fl_erase4k_quiet(uint32_t off, uint32_t *took) { (void)took; if (off & 0xFFFu) bad_write++; memset(flash + off, 0xFF, 4096); return 0; }
static int fl_write(uint32_t off, const uint8_t *s, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        if ((flash[off + i] & s[i]) != s[i]) bad_write++;   /* (a bit set where the flash holds 0: not erased) */
        flash[off + i] &= s[i];
    }
    return 0;
}
static uint32_t zs_crc(const uint8_t *p, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu, k;
    while (n--) { c ^= *p++; for (k = 0; k < 8u; k++) c = c >> 1 ^ (0xEDB88320u & -(c & 1u)); }
    return ~c;
}

/* what the sampler needs of the rest */
#define KIT_NWAVE 20u
#define SP_NUSER 24
#define SP_NCH 8
#define SP_NSOUND 32u
#define SPF_REVERSE 1u
#define SPF_33 2u
typedef struct { const uint8_t *d; uint32_t n; uint16_t rate; } sp_wave_t;
static sp_wave_t sp_wave[64];
static struct { const sp_wave_t *w; uint8_t on; } sp_ch[SP_NCH];
typedef struct { uint8_t wave; int8_t tune, fine; uint8_t flags; uint16_t start, end; } sp_sound_t;
static sp_sound_t sp_sound[SP_NSOUND];
static char ui_uname[SP_NUSER][9];
static uint8_t ui_uflags[SP_NUSER];
static struct { uint8_t force, sel, save_req; } ui;
static char said[32];
static void ui_say(const char *m) { strncpy(said, m, sizeof said - 1); }
static char *cat(char *d, const char *s) { while (*s) *d++ = *s++; *d = 0; return d; }
static void pad_label(char *b, uint32_t k) { b[0] = (char)('A' + k / 16u % 2u); b[1] = (char)('0' + k % 16u / 9u); b[2] = (char)('0' + (k % 16u + 1u) % 10u); b[3] = 0; }
static uint32_t hits;
#define SP_HIT(k, vel, semis) (hits++)
#include "sp_samples.c"

/* usb.c's side of it */
#define UAI_N 4096u
static uint32_t uai_ring[UAI_N];
static volatile uint32_t uai_w;
static struct { uint8_t alt, e0_rx; uint32_t pkts, peak, polls, last_poll; int16_t vol; uint8_t mute; } uai;
#define UAI_LIVE() (uai.alt || (uint32_t)(uai.polls - uai.last_poll) < 3000u)
/* sp_ui.c's side of it */
static const uint16_t SMP_RATE[3] = {40000, 26040, 20000};
static const int32_t SMP_LP[3] = {29786, 25854, 22872};
enum { SM_IDLE, SM_ERASE, SM_WAIT, SM_REC, SM_DONE };
static struct { uint8_t state, rate, thr, mon, slot; uint16_t len10; uint32_t off, end, erased, n, want, peak; } smp = {0, 0, 0, 80, 0, 20};
#include "sp_sampler.c"

static int bad;
static void check(int ok, const char *what) { printf("sampler: %-72s %s\n", what, ok ? "ok" : "FAIL"); bad += !ok; }

static double phase;
static uint32_t frames_in;
static void packet(double amp)                           /* one millisecond of the host playing: 44 or 45 frames */
{
    uint32_t i, n = (uint32_t)(((uint64_t)(uai.pkts + 1u) * 44100u) / 1000u - ((uint64_t)uai.pkts * 44100u) / 1000u);
    for (i = 0; i < n; i++) {
        int16_t v = (int16_t)lrint(amp * 32767.0 * sin(phase));
        phase += 2.0 * M_PI * 1000.0 / 44100.0;
        uai_ring[uai_w % UAI_N] = (uint16_t)v | (uint32_t)(uint16_t)v << 16;
        uai_w++;
        frames_in++;
    }
    uai.pkts++;
    uai.last_poll = uai.polls;
}

static int32_t s12(const uint8_t *p, uint32_t i)        /* sample i of a packed take */
{
    const uint8_t *q = p + i / 2u * 3u;
    uint32_t v = i & 1u ? (uint32_t)(q[1] >> 4) | (uint32_t)q[2] << 4 : (uint32_t)q[0] | (uint32_t)(q[1] & 15u) << 8;
    return (int32_t)(v ^ 0x800u) - 0x800;
}

int main(void)
{
    uint32_t off, end, i, guard;
    uai.polls = 100000u;                                 /* (long after any packet: no stream yet) */
    memset(flash, 0xFF, sizeof flash);
    zu_load();
    smp_find(&off, &end);
    check(off == 0xEA000u && end == 0xFC000u, "no directory: the largest room, 0xEA000-0xFC000 (72 KB)");

    {   /* a sample of the editor at 0xEA000 (15000 bytes): the run starts at the next sector */
        static zu_dir_t d;
        memset(&d, 0, sizeof d);
        d.magic = ZU_MAGIC; d.gen = 5;
        d.s[0].off = 0xEA000u; d.s[0].n = 10000; d.s[0].rate = 26040; memcpy(d.s[0].name, "EDIT", 4);
        d.crc = zs_crc((const uint8_t *)d.s, sizeof d.s);
        memcpy(flash + ZU_DIR0, &d, sizeof d);
        memset(flash + 0xEA000u, 0x5A, 15000);
        zu_load();
        smp_find(&off, &end);
        check(off == 0xEE000u && end == 0xFC000u, "a sample in the room: the run after it, from its next whole sector");
        d.flags = 1; d.crc = zs_crc((const uint8_t *)d.s, sizeof d.s);
        memcpy(flash + ZU_DIR0, &d, sizeof d);
        zu_load();
        smp_find(&off, &end);
        check(off == 0xA0000u && end == 0xC4000u, "the bank room allowed (the editor's flag): its 144 KB are the largest");
        d.flags = 0; d.crc = zs_crc((const uint8_t *)d.s, sizeof d.s);
        memcpy(flash + ZU_DIR0, &d, sizeof d);
        zu_load();
    }

    /* no stream: refused */
    uai.alt = 0;
    smp_press();
    check(smp.state == SM_IDLE && !strcmp(said, "PLAY INTO USB"), "REC without USB audio: PLAY INTO USB");

    /* a take: 0.5 s at 40 kHz of a 1 kHz sine at half scale, onto pad A03 */
    uai.alt = 1;
    ui.sel = 2;
    smp.rate = 0; smp.len10 = 5; smp.thr = 0;
    smp_press();
    check(smp.state == SM_ERASE && smp.slot == 1u, "REC: the room erased first, into the first free slot (2)");
    for (guard = 0; smp.state != SM_IDLE && guard < 20000u; guard++) {
        if (smp.state != SM_ERASE) packet(0.5);
        smp_service();
    }
    check(smp.state == SM_IDLE && !bad_write, "the take ends by itself; every page written into erased flash");
    {
        const zu_slot_t *z = &zu_dir.s[1];
        uint32_t cross = 0, pk = 0;
        check(zu_dir.gen == 6u && z->off == 0xEE000u && z->n == 20000u && z->rate == 40000u && !strncmp(z->name, "SMP02", 5),
              "the directory: generation 6, slot 2 at 0xEE000, 20000 samples at 40 kHz, SMP02");
        for (i = 1; i < z->n; i++) {
            int32_t a = s12(flash + z->off, i - 1u), b = s12(flash + z->off, i), m = b < 0 ? -b : b;
            cross += (a < 0) != (b < 0);
            if (i > 400u && (uint32_t)m > pk) pk = (uint32_t)m;
        }
        printf("sampler: %u zero crossings in 0.5 s, peak %u of 2047\n", cross, pk);
        check(cross >= 990u && cross <= 1010u, "1 kHz in: 1 kHz out (about 1000 crossings in half a second)");
        check(pk >= 960u && pk <= 1060u, "half scale in: half of 12 bit out (the low-pass at 16.8 kHz leaves 1 kHz alone)");
        check(sp_sound[2].wave == KIT_NWAVE + 1u && sp_wave[KIT_NWAVE + 1u].n == 20000u && hits == 1u && ui.save_req,
              "A03 plays it (and is heard once), the save asked for");
    }

    /* THRESH -30 dB: 0.2 s of silence first, then the sine; REC again after ~0.1 s ends it there */
    {
        uint32_t k, skipped;
        smp.rate = 2; smp.len10 = 20; smp.thr = 4;
        ui.sel = 5;
        smp_press();
        for (guard = 0; smp.state == SM_ERASE && guard < 1000u; guard++) smp_service();
        check(smp.state == SM_WAIT, "THRESH: after the erase it waits for the input");
        for (k = 0; k < 200u; k++) { packet(0.0); smp_service(); }
        skipped = smp.n;
        check(smp.state == SM_WAIT && skipped == 0u, "0.2 s of silence: still waiting, nothing taken");
        for (k = 0; k < 100u; k++) { packet(0.5); smp_service(); }
        check(smp.state == SM_REC, "the sine passes -30 dB: recording");
        smp_press();
        for (guard = 0; smp.state != SM_IDLE && guard < 2000u; guard++) { packet(0.5); smp_service(); }
        k = zu_dir.s[2].n;
        printf("sampler: ended early at %u samples (%.2f s at 20 kHz)\n", k, k / 20000.0);
        check(smp.state == SM_IDLE && k > 1700u && k < 2300u && zu_dir.s[2].rate == 20000u && zu_dir.s[2].off >= 0xEE000u + 30000u,
              "REC again: the take ends there (~0.1 s), at 20 kHz, after the first take");
        check(!bad_write && zu_dir.gen == 7u, "written into erased flash; the directory's next generation");
    }

    /* the host stops in the middle: the take keeps what came */
    {
        uint32_t k;
        smp.rate = 1; smp.len10 = 30; smp.thr = 0;
        ui.sel = 6;
        smp_press();
        for (guard = 0; smp.state == SM_ERASE && guard < 1000u; guard++) smp_service();
        for (k = 0; k < 300u; k++) { packet(0.5); smp_service(); }
        uai.alt = 0;
        for (guard = 0; guard < 100u; guard++) { uai.polls += 2u; smp_service(); }   /* 0.1 s: a pause, not the end */
        check(smp.state == SM_REC, "the host pauses 0.1 s: the take goes on");
        for (guard = 0; smp.state != SM_IDLE && guard < 4000u; guard++) { uai.polls += 2u; smp_service(); }
        k = zu_dir.s[3].n;
        printf("sampler: the host stopped at %u samples (%.2f s at 26 kHz)\n", k, k / 26040.0);
        check(smp.state == SM_IDLE && k > 7000u && k < 8300u && !bad_write, "the host stops playing: the take is what came (~0.3 s)");
    }
    /* the room full: REC on a pad with its own sample (on no other pad) replaces it in place */
    {
        uint32_t k, gen = zu_dir.gen, off0 = zu_dir.s[1].off;
        uai.alt = 1;
        smp.rate = 0; smp.len10 = 99; smp.thr = 0;       /* (as long as it goes: every room filled, pad by pad) */
        for (k = 9; k < 16u; k++) {
            ui.sel = (uint8_t)k;
            sp_sound[k].wave = 0;
            said[0] = 0;
            smp_press();
            if (smp.state == SM_IDLE) break;
            for (guard = 0; smp.state != SM_IDLE && guard < 40000u; guard++) { if (smp.state != SM_ERASE) packet(0.5); smp_service(); }
        }
        printf("sampler: %u more takes filled the rooms\n", k - 9u);
        check(smp.state == SM_IDLE && !strncmp(said, "NO ROOM", 7), "the rooms full: a pad without a sample of its own gets NO ROOM");
        ui.sel = 2;                                       /* A03 plays SMP02 (and no other pad does) */
        smp_press();
        check(smp.state == SM_ERASE && smp.slot == 1u && smp.off == off0, "REC on A03: its SMP02 replaced, in its own place");
        for (guard = 0; smp.state != SM_IDLE && guard < 20000u; guard++) { if (smp.state != SM_ERASE) packet(0.5); smp_service(); }
        check(smp.state == SM_IDLE && !bad_write && zu_dir.gen > gen && zu_dir.s[1].off == off0 && sp_sound[2].wave == KIT_NWAVE + 1u,
              "replaced: same slot, same place, written into erased flash");
    }
    printf(bad ? "SAMPLER TEST FAILED\n" : "sampler test passed\n");
    return bad != 0;
}
