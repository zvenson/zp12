/* SPDX-License-Identifier: GPL-3.0-only */
/* zp12's sequencer (firmware/src/sp_seq.c) with the sound core and sloopDX's effects, on the host: a beat recorded
 * through the real path (count-in, hits as the audio ISR takes them, AUTO CORRECT), played back, swing, erase,
 * a song of two segments; the result as a WAV with reverb and delay to listen to.
 *   cc -O2 -Wall -Wno-unused-function -Ifirmware/src -Ibuild/gen -o build/host/sp_seq_test tests/sp_seq_test.c -lm
 *   build/host/sp_seq_test build/host/zp12-seq.wav */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define SP_WITH_FX 1
#include "../firmware/src/sp_core.c"
#include "../firmware/src/sp_fx.c"
#include "zp12_kit.h"
#include "../firmware/src/sp_seq.c"
static int flash_ok;                                    /* (sp_store.c: only pack / unpack here) */
static void st_read(uint32_t a, void *d, uint32_t n) { (void)a; (void)d; (void)n; }
static int fl_erase4k(uint32_t a, uint32_t *t) { (void)a; (void)t; return -1; }
static int fl_write(uint32_t a, const void *d, uint32_t n) { (void)a; (void)d; (void)n; return -1; }
static void fm1_wdt_feed(void) {}
static struct { uint8_t back; } ui;                   /* (what sp_store.c saves of the UI) */
#include "../firmware/src/sp_store.c"

static int bad;
static void check(int ok, const char *what) { printf("seq: %-70s %s\n", what, ok ? "ok" : "FAIL"); bad += !ok; }
static FILE *wf;
static uint32_t frames;
static int32_t peak;

static void put32(uint32_t v) { fputc((int)(v & 255), wf); fputc((int)(v >> 8 & 255), wf); fputc((int)(v >> 16 & 255), wf); fputc((int)(v >> 24), wf); }
static void block(void)                                 /* one audio block, as the ISR does it */
{
    int32_t o[2 * SP_BLK];
    uint32_t i;
    memset(o, 0, sizeof o);
    sq_block();
    sp_render(o);
    for (i = 0; i < 2u * SP_BLK; i++) {
        int32_t v = o[i] > 32767 ? 32767 : o[i] < -32768 ? -32768 : o[i];
        int16_t s = (int16_t)v;
        if (abs(o[i]) > peak) peak = abs(o[i]);
        if (wf) fwrite(&s, 2, 1, wf);
    }
    frames += SP_BLK;
}
static uint32_t tick(void) { return sq.pos >> 16; }
static void until_tick(uint32_t t) { while (sq.playing && (sq.countin > 0 || tick() < t)) block(); }
static void until_wrap(void) { uint32_t last = tick(); while (sq.playing) { block(); if (tick() < last) break; last = tick(); } }
static uint32_t count(const sq_seg_t *s, uint32_t pad) { uint32_t i, n = 0; for (i = 0; i < s->n; i++) n += s->ev[i].pad == pad; return n; }

int main(int argc, char **argv)
{
    uint32_t i, b;
    sq_seg_t *s0 = &sq_seg[0];
    wf = fopen(argc > 1 ? argv[1] : "build/host/zp12-seq.wav", "wb");
    fwrite("RIFF\0\0\0\0WAVEfmt \x10\0\0\0\x01\0\x02\0", 1, 24, wf);
    put32(SP_FS); put32(SP_FS * 4u); fwrite("\x04\0\x10\0data\0\0\0\0", 1, 12, wf);
    for (i = 0; i < KIT_NWAVE; i++) { sp_wave[i].d = KIT_DATA + KIT_WAVE[i].off; sp_wave[i].n = KIT_WAVE[i].n; sp_wave[i].rate = 26040; }
    memcpy(sp_sound, KIT_PADS, sizeof sp_sound);
    sp_sound[1].send[2] = 70;                           /* the snare into the reverb, the clap into the delay */
    sp_sound[3].send[1] = 60;
    sp_sound[4].send[2] = 30;
    sq_init();
    sq.bpm10 = 920;
    sq.quant = 3;                                       /* AUTO CORRECT 1/16 */
    sq_seg[0].bars = 1;

    /* 1. REC armed, PLAY: a bar of count-in, then the hits recorded as they come (a little late / early) */
    sq_post(RQ_REC, 0, 0);
    sq_post(RQ_PLAY, 0, 0);
    block();
    check(sq.playing && sq.recording && sq.countin > 0, "REC armed + PLAY: count-in, recording");
    until_tick(0);
    check(sq.countin <= 0, "the count-in ends after a bar");
    for (b = 0; b < 16u; b++) {                         /* kick on 1 and 3 (a little late), hat on 8ths, snare 2 and 4 */
        until_tick(b * 24u + (b & 1u ? 0u : 3u));       /* (the even ones 3 ticks late: AUTO CORRECT pulls them back) */
        if (b % 8u == 0u) sq_hit(0, 120, 0);
        if (b % 2u == 0u) sq_hit(4, 90, 0);
        if (b == 4u || b == 12u) sq_hit(1, 115, 0);
        if (b == 14u) sq_hit(3, 110, 0);
    }
    until_wrap();
    check(count(s0, 0) == 2u && count(s0, 4) == 8u && count(s0, 1) == 2u && count(s0, 3) == 1u, "every hit recorded once");
    {
        int ok = 1;
        for (i = 0; i < s0->n; i++) ok &= s0->ev[i].t % 24u == 0u;
        check(ok, "AUTO CORRECT 1/16: every hit on the grid (3 ticks late pulled back)");
        ok = 1;
        for (i = 1; i < s0->n; i++) ok &= s0->ev[i - 1u].t <= s0->ev[i].t;
        check(ok, "the segment stays in time order");
    }
    sq.recording = 0;
    for (i = 0; i < 2u; i++) until_wrap();              /* two bars of playback */

    /* 2. swing: 63 % moves the odd 16ths (and leaves the even ones) */
    {
        sq_ev_t e = {0}, f = {0};
        e.t = 24; f.t = 48;
        sq.swing = 3;
        check(sq_play_t(&e) == 24u + 48u * 13u / 100u && sq_play_t(&f) == 48u, "SWING 63 %: an odd 16th 6 ticks late, an even one on time");
    }
    for (i = 0; i < 2u; i++) until_wrap();              /* two bars with swing */
    sq.swing = 0;

    /* 3. erase: LFO + the hat held for a bar: its hits go as the playhead passes */
    sq.erase = 1u << 4;
    until_wrap();
    sq.erase = 0;
    check(count(s0, 4) == 0u && count(s0, 0) == 2u, "ERASE: the hat gone, the kick kept");

    /* 3a. locks: recording while DECAY is turned, the snare's passing hits and a new hat take it; the rest keep none */
    {
        uint32_t lk, n0 = 0, n1 = 0, k;
        sq.recording = 1;
        sp_sound[1].decay = 20;
        sp_sound[1].tune = 3;
        sq.turn = 1u << 1;
        until_wrap();
        sq_hit(4, 100, 0);                              /* (a hat: not turned, no lock) */
        sq.turn = (1u << 1) | (1u << 4);
        until_tick(SQ_BAR / 2u + 2u);
        sq_hit(4, 100, 0);
        sq.turn = 0;
        sq.recording = 0;
        for (k = 0; k < s0->n; k++) {
            if (s0->ev[k].pad == 1u) n1 += s0->ev[k].lk;
            if (s0->ev[k].pad == 0u) n0 += s0->ev[k].lk;
        }
        k = (uint32_t)sq_find(s0, SQ_BAR / 2u, 4);
        check(n1 == 2u && n0 == 0u, "LOCK: the turned snare's hits take its knobs, the kick none");
        check(s0->ev[k].lk && sq_lk[0][k] == sp_lock_of(&sp_sound[4]), "LOCK: a hit played while turning takes the knobs");
        lk = sq_lk[0][(uint32_t)sq_find(s0, 96, 1)];
        check(((lk >> 12) & 127u) == 20u && (lk & 0xFFFu) == 2860u, "LOCK: DECAY and TUNE as turned");
        sp_sound[1].decay = 100;
        sp_sound[1].tune = 0;
        sq_post(RQ_STOP, 0, 0);
        block();
        sp_trigger_lk(1, 100, 0, lk);
        check(sp_ch[sp_sound[1].chan].emul == sp_decay_mul(20), "LOCK: played with its own DECAY, not the sound's");
        sq_post(RQ_PLAY, 0, 0);
        block();
    }

    /* 3a store: the locks and the UI's BACK saved and read back; older saves (3: no settings, 2: no locks either):
     * the same bytes without the 8 of the settings */
    {
        static uint8_t keep[ZS_SLOT], old[ZS_SLOT];
        uint32_t n, k = (uint32_t)sq_find(s0, 96, 1), lk = sq_lk[0][k];
        uint32_t at = sizeof sp_sound + sizeof sp_mix + sizeof fxp + 16u + sizeof sq_songs + sizeof sq_song_len;
        ui.back = 4; sp_smooth = 0;
        n = zs_pack(keep);
        sq_lk[0][k] = 0; ui.back = 1; sp_smooth = 1;
        check(zs_unpack(keep, n, ZS_VER) == 0 && s0->ev[k].lk && sq_lk[0][k] == lk, "STORE: a lock saved and read back");
        check(ui.back == 4 && !sp_smooth, "STORE: BACK (pages never back) and SMOOTH off saved and read back");
        memcpy(old, keep, at); memcpy(old + at, keep + at + 8u, n - at - 8u);
        {   /* an older save's LEVEL (linear) becomes the one that sounds the same squared: 64 -> 90, 127 -> 127 */
            uint32_t l = at;                         /* (the sounds are the record's first bytes) */
            uint8_t l0 = sp_sound[0].level, l1 = sp_sound[1].level;
            (void)l;
            sp_sound[0].level = 64; sp_sound[1].level = 127;
            n = zs_pack(keep);
            memcpy(old, keep, at); memcpy(old + at, keep + at + 8u, n - at - 8u);
            sp_sound[0].level = l0; sp_sound[1].level = l1;
        }
        check(zs_unpack(old, n - 8u, 3) == 0 && s0->ev[k].lk && ui.back == 1 && sp_smooth, "STORE: a version 3 save: its locks, BACK 12 s, SMOOTH on");
        check(sp_sound[0].level == 90u && sp_sound[1].level == 127u, "STORE: an older save's LEVEL in the audio taper (64 -> 90, 127 -> 127)");
        check(zs_unpack(old, n - 8u, 2) == 0 && !s0->ev[k].lk, "STORE: a version 2 save has no locks");
        sp_sound[0].level = 64; sp_sound[1].level = 127;
        check(zs_unpack(keep, n, ZS_VER) == 0 && s0->ev[k].lk && sp_sound[0].level == 64u, "STORE: back as it was (a version 5 save: LEVEL as it is)");
        memcpy(sp_sound, KIT_PADS, sizeof sp_sound);
        sp_smooth = 1;
    }

    /* 3a''. a 2.0 save (the kit laid out the old way): every pad and every loop's hit moves to its 2.1 place
     * (KIT_MOVED), the pads' edits with them; a 2.1 save stays as it is */
    {
        static uint8_t now[ZS_SLOT], was[ZS_SLOT];
        uint32_t n, i, ok = 1, at = sizeof sp_sound + sizeof sp_mix + sizeof fxp + 14u;   /* (st[7]: the kit's id) */
        uint16_t id20 = ZS_KIT_ID_20;
        uint8_t pads[SQ_MAXEV];
        for (i = 0; i < SP_NSOUND; i++) sp_sound[i].level = (uint8_t)(10u + i);   /* (a mark on every pad) */
        for (i = 0; i < s0->n; i++) pads[i] = (uint8_t)s0->ev[i].pad;
        n = zs_pack(now);
        memcpy(was, now, n); memcpy(was + at, &id20, 2u);
        check(zs_unpack(was, n, ZS_VER) == 0, "KIT 2.1: a 2.0 save read");
        for (i = 0; i < SP_NSOUND; i++) ok &= sp_sound[KIT_MOVED[i]].level == 10u + i;
        check(ok, "KIT 2.1: a 2.0 save's pads moved to their new places, their edits with them");
        ok = s0->n > 0u;
        for (i = 0; i < s0->n; i++) ok &= s0->ev[i].pad == KIT_MOVED[pads[i]];
        check(ok, "KIT 2.1: a 2.0 save's hits play the same sounds (their pads moved)");
        check(zs_unpack(now, n, ZS_VER) == 0 && s0->ev[0].pad == pads[0] && sp_sound[0].level == 10u, "KIT 2.1: a 2.1 save as it is");
        memcpy(sp_sound, KIT_PADS, sizeof sp_sound);
    }

    /* 3a'. CLEAR, then UNDO brings it back (locks too), UNDO again clears it again */
    {
        uint32_t n = s0->n, k1 = (uint32_t)sq_find(s0, 96, 1);
        sq_post(RQ_CLEAR, 0, 0);
        block();
        check(s0->n == 0u, "CLEAR: the loop empty");
        sq_post(RQ_UNDO, 0, 0);
        block();
        check(s0->n == n && s0->ev[k1].lk && ((sq_lk[0][k1] >> 12) & 127u) == 20u, "UNDO: the loop back, with its locks");
        sq_post(RQ_UNDO, 0, 0);
        block();
        check(s0->n == 0u, "UNDO again: cleared again (redo)");
        sq_post(RQ_UNDO, 0, 0);
        block();
    }

    /* 3a''. REC held: its press undone */
    sq_post(RQ_REC, 0, 0);
    sq_post(RQ_RECSET, 0, 0);
    block();
    check(!sq.recording && sq.playing, "REC held: the overdub it started is undone");

    /* 3b. AUTO: an empty segment of no length, a take of two bars: it becomes two bars long */
    sq_post(RQ_STOP, 0, 0);
    block();
    sq.seg = 2;
    sq_seg[2].bars = 0;
    sq_post(RQ_REC, 0, 0);
    sq_post(RQ_PLAY, 0, 0);
    block();
    until_tick(0);
    for (b = 0; b < 8u; b++) { until_tick(b * SQ_PPQ); sq_hit(0, 120, 0); }
    until_tick(2u * SQ_BAR + 20u);                      /* REC again just after the second bar */
    sq_post(RQ_REC, 0, 0);
    block();
    check(sq_seg[2].bars == 2u && count(&sq_seg[2], 0) == 8u && !sq.recording && sq.playing, "AUTO: a take of two bars makes the segment two bars long");
    check(tick() < SQ_BAR, "AUTO: the playhead wraps into the new length");
    sq_post(RQ_STEP, 3, SQ_BAR + 3u * 24u);             /* step edit: a clap on bar 2, step 4 ... */
    block();
    check(sq_find(&sq_seg[2], SQ_BAR + 72u, 3) >= 0, "step edit: a step set");
    sq_post(RQ_STEP, 3, SQ_BAR + 3u * 24u);             /* ... and off again */
    block();
    check(sq_find(&sq_seg[2], SQ_BAR + 72u, 3) < 0, "step edit: the same step cleared");
    sq.seg = 0;

    /* 3c. loops: a black key while playing switches at the loop's end */
    sq_post(RQ_PLAY, 0, 0);
    block();
    sq_post(RQ_LOOP, 0, 2);
    block();
    check(sq.seg == 0u && sq.next_seg == 2u, "LOOP 3 chosen while loop 1 plays: waits");
    until_wrap();
    check(sq.seg == 2u && sq.next_seg == 0xFFu, "LOOP 3 from loop 1's end");
    sq_post(RQ_STOP, 0, 0);
    sq_post(RQ_LOOP, 0, 0);
    block();
    check(sq.seg == 0u, "stopped: a loop at once");

    /* 4. a second segment and a song: seg 1 twice, seg 2 once, then the end stops */
    sq_post(RQ_STOP, 0, 0);
    block();
    sq_seg[1].bars = 1;
    sq_insert(&sq_seg[1], 0, 0, 7, 0, 0, 0);
    sq_insert(&sq_seg[1], 96, 6, 6, -5, 0, 0);
    sq_insert(&sq_seg[1], 192, 0, 7, 0, 0, 0);
    sq_insert(&sq_seg[1], 288, 7, 6, 0, 0, 0);
    sq_insert(&sq_seg[1], 288 + 48, 7, 5, 2, 0, 0);
    sq_song[0].seg = 0; sq_song[0].rep = 2;
    sq_song[1].seg = 1; sq_song[1].rep = 1;
    SQ_SONG_N = 2;
    sq.song_mode = 1;
    sq_post(RQ_PLAY, 0, 0);
    block();
    check(sq.playing && sq.seg == 0u && !sq.recording, "SONG: starts on its first step");
    until_wrap();
    check(sq.seg == 0u && sq.song_rep == 1u, "SONG: the first segment again (x2)");
    until_wrap();
    check(sq.seg == 1u && sq.song_i == 1u, "SONG: then the second");
    until_wrap();
    check(!sq.playing, "SONG: stops at its end");
    sq.song_sel = 1;                                    /* song 2: loop 2 once, then loop 1 */
    sq_song[0].seg = 1; sq_song[0].rep = 1; sq_song[1].seg = 0; sq_song[1].rep = 1; SQ_SONG_N = 2;
    sq_post(RQ_PLAY, 0, 0);
    block();
    check(sq.seg == 1u, "SONG 2: its own first loop");
    until_wrap();
    check(sq.seg == 0u && sq_songs[0][0].rep == 2u, "SONG 2: its own second loop; song 1 as it was");
    sq_post(RQ_STOP, 0, 0);
    block();
    sq.song_sel = 0;
    {   /* the DJ filter: a hat through the low-pass shut, then the high-pass open, resonance up: quieter, bounded */
        int32_t pk[3] = {0, 0, 0}, v[3] = {0, -64, 63}, o[2 * SP_BLK];
        uint32_t m, j;
        for (m = 0; m < 3u; m++) {
            djf.v = (int8_t)v[m];
            djf.res = m == 2u ? 127 : 40;               /* (the high-pass with the resonance up: still bounded) */
            for (i = 0; i < 300u; i++) block();          /* (the glide) */
            sp_trigger(KIT_MOVED[4], 127);               /* (the hat: 2.0's pad 4, 2.1's pad 3) */
            for (i = 0; i < 200u; i++) {
                memset(o, 0, sizeof o); sp_render(o);
                for (j = 0; j < 2u * SP_BLK; j++) if (abs(o[j]) > pk[m]) pk[m] = abs(o[j]);
            }
        }
        check(pk[1] < pk[0] / 4 && pk[2] < 3 * pk[0], "DJ FILTER: the low-pass shut takes the hat away, the high-pass bounded");
        djf.v = 0;
        djf.res = 40;
        for (i = 0; i < 400u; i++) block();
        check(djf.mode == 0, "DJ FILTER: back at 0 it is off");
    }
    {   /* COUNT: 2 bars and off; DUB BAR: an overdub from the next 1 */
        uint32_t ticks = 0, last;
        sq.song_mode = 0; sq.seg = 5; sq_seg[5].bars = 2; sq.cin_bars = 2;
        sq_post(RQ_REC, 0, 0);
        sq_post(RQ_PLAY, 0, 0);
        block();
        check(sq.cin_len == 2u * SQ_BAR && sq.recording, "COUNT 2 BARS: a count-in of two bars");
        while (sq.countin > 0) { block(); ticks++; }
        check((ticks * (sq.bpm10 * 76087u / 10000u) >> 16) >= 2u * SQ_BAR - 12u, "COUNT 2 BARS: as long as two bars");
        sq_post(RQ_STOP, 0, 0);
        sq.cin_bars = 0;
        sq_post(RQ_REC, 0, 0);
        sq_post(RQ_PLAY, 0, 0);
        block();
        check(sq.countin <= 0 && sq.recording, "COUNT OFF: recording from the PLAY on");
        sq_post(RQ_STOP, 0, 0);
        sq.cin_bars = 1;
        sq.dub_bar = 1;
        sq_post(RQ_PLAY, 0, 0);
        until_tick(SQ_PPQ + 10u);
        sq_post(RQ_REC, 0, 0);
        block();
        check(sq.dub_wait && !sq.recording, "DUB BAR: REC while playing waits for the next 1");
        sq_post(RQ_REC, 0, 0);
        block();
        check(!sq.dub_wait && !sq.recording, "DUB BAR: REC again while waiting: not");
        sq_post(RQ_REC, 0, 0);
        block();
        last = tick();
        while (sq.dub_wait) { last = tick(); block(); }
        check(sq.recording && last >= SQ_BAR - SQ_PPQ / 8u - 30u && last < SQ_BAR, "DUB BAR: recording from the bar's 1");
        sq_hit(2, 100, 0);                              /* (a hit just before the 1: it lands on it) */
        check(sq_find(&sq_seg[5], SQ_BAR, 2) >= 0, "DUB BAR: a hit just early lands on the 1");
        sq_post(RQ_REC, 0, 0);
        sq_post(RQ_STOP, 0, 0);
        block();
        sq.dub_bar = 0;
        sq.seg = 0;
    }
    {   /* MUTE / SOLO (the channel alone, no effects' tails): it goes silent in a few ms, comes back; a solo silences the others */
        int32_t o[2 * SP_BLK], pk;
        uint32_t j, ch = sp_sound[0].chan;
        sp_mute = (uint8_t)(1u << ch);
        sp_trigger(0, 127);
        for (i = 0; i < 4u; i++) { memset(o, 0, sizeof o); sp_channel(ch, o); }
        for (pk = 0, i = 0; i < 20u; i++) { memset(o, 0, sizeof o); sp_channel(ch, o); for (j = 0; j < 2u * SP_BLK; j++) if (abs(o[j]) > pk) pk = abs(o[j]); }
        check(pk == 0, "MUTE: the kick's channel silent");
        sp_mute = 0;
        sp_trigger(0, 127);
        for (pk = 0, i = 0; i < 20u; i++) { memset(o, 0, sizeof o); sp_channel(ch, o); for (j = 0; j < 2u * SP_BLK; j++) if (abs(o[j]) > pk) pk = abs(o[j]); }
        check(pk > 1000, "MUTE off: heard again");
        sp_solo = (uint8_t)(1u << ((ch + 1u) % SP_NCH));
        sp_trigger(0, 127);
        for (i = 0; i < 4u; i++) { memset(o, 0, sizeof o); sp_channel(ch, o); }
        for (pk = 0, i = 0; i < 20u; i++) { memset(o, 0, sizeof o); sp_channel(ch, o); for (j = 0; j < 2u * SP_BLK; j++) if (abs(o[j]) > pk) pk = abs(o[j]); }
        check(pk == 0, "SOLO of another channel: the kick silent");
        sp_solo = 0;
    }
    for (i = 0; i < SP_FS * 2u / SP_BLK; i++) block();  /* the reverb's tail */
    check(peak < 65536, "bounded");
    fseek(wf, 4, SEEK_SET); put32(36u + frames * 4u);
    fseek(wf, 40, SEEK_SET); put32(frames * 4u);
    fclose(wf);
    printf("seq: %.1f s rendered, peak %d\n", frames / (double)SP_FS, peak);
    printf(bad ? "SEQ TEST FAILED\n" : "seq test passed\n");
    return bad != 0;
}
