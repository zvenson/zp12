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
    sq_insert(&sq_seg[1], 0, 0, 7, 0, 0);
    sq_insert(&sq_seg[1], 96, 6, 6, -5, 0);
    sq_insert(&sq_seg[1], 192, 0, 7, 0, 0);
    sq_insert(&sq_seg[1], 288, 7, 6, 0, 0);
    sq_insert(&sq_seg[1], 288 + 48, 7, 5, 2, 0);
    sq_song[0].seg = 0; sq_song[0].rep = 2;
    sq_song[1].seg = 1; sq_song[1].rep = 1;
    sq.song_n = 2;
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
    for (i = 0; i < SP_FS * 2u / SP_BLK; i++) block();  /* the reverb's tail */
    check(peak < 65536, "bounded");
    fseek(wf, 4, SEEK_SET); put32(36u + frames * 4u);
    fseek(wf, 40, SEEK_SET); put32(frames * 4u);
    fclose(wf);
    printf("seq: %.1f s rendered, peak %d\n", frames / (double)SP_FS, peak);
    printf(bad ? "SEQ TEST FAILED\n" : "seq test passed\n");
    return bad != 0;
}
