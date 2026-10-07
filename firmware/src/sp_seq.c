/* SPDX-License-Identifier: GPL-3.0-only */
/* zp12's sequencer, as a 12-bit drum machine's: segments of 1-8 bars at 96 ticks a quarter, hits recorded in
 * real time (quantised by AUTO CORRECT as they come in), swing at playback, a song of segments with
 * repeats, a count-in and a click. Everything that changes a segment runs in the audio ISR (sq_block), so
 * the playing never sees half an edit: the main loop only posts requests.
 *
 * A hit is (tick, pad, velocity, semitones (MULTI PITCH), flags). SQF_SKIP: recorded ahead of the playhead
 * (quantised forward), already heard live: not played again this pass. */
#define SQ_PPQ 96u
#define SQ_BAR (SQ_PPQ * 4u)
#define SQ_NSEG 16u
#define SQ_MAXEV 512u
#define SQ_NSONG 32u
enum { SQF_SKIP = 1 };

typedef struct { uint16_t t; uint8_t pad, vel; int8_t semis; uint8_t flags; } sq_ev_t;
typedef struct { uint8_t bars, rsv; uint16_t n; sq_ev_t ev[SQ_MAXEV]; } sq_seg_t;   /* ev sorted by t */

static sq_seg_t sq_seg[SQ_NSEG] __attribute__((section(".pool")));
static struct { uint8_t seg, rep; } sq_song[SQ_NSONG];

/* AUTO CORRECT: the grid in ticks (0: off, 1-tick resolution) and the swing steps */
static const uint8_t SQ_GRID[7] = {1, 48, 32, 24, 16, 12, 8};
static const char *const SQ_GRID_NAME[7] = {"OFF", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T"};
static const uint8_t SQ_SWING[6] = {50, 54, 58, 63, 67, 71};

static struct {
    volatile uint8_t playing, recording, song_mode;
    volatile uint8_t rec_arm;                   /* REC pressed while stopped: PLAY starts with a count-in */
    uint8_t seg;                                /* the segment playing / edited */
    uint8_t quant, swing;                       /* SQ_GRID index, SQ_SWING index */
    uint8_t click;                              /* 0 off, 1 while recording, 2 always */
    uint8_t song_n, song_i, song_rep;
    uint16_t bpm10;
    uint32_t pos;                               /* the playhead in the segment, ticks Q16 */
    int32_t countin;                            /* ticks Q16 of count-in left (> 0: counting) */
    volatile uint32_t erase;                    /* pads held with ERASE: their hits go as the playhead passes */
    volatile uint32_t beat;                     /* beats since PLAY (the UI's blink) */
    volatile uint8_t req_clear, req_wipe;       /* requests to the ISR: clear the segment / a pad's hits (pad + 1) */
    volatile uint8_t req_play;                  /* 1 PLAY, 2 STOP */
} sq = {0, 0, 0, 0, 0, 3, 0, 1, 0, 0, 0, 900, 0, 0, 0, 0, 0, 0, 0};

static uint32_t sq_len(const sq_seg_t *s) { return (uint32_t)(s->bars ? s->bars : 1u) * SQ_BAR; }

/* the tick a hit sounds at: its own, odd 1/16 (or 1/8 with AUTO CORRECT 1/8) late by the swing */
static uint32_t sq_play_t(const sq_ev_t *e)
{
    uint32_t g = sq.quant == 1u ? 48u : 24u, sw = SQ_SWING[sq.swing % 6u] - 50u;
    if (sw && e->t % (2u * g) == g)
        return e->t + (2u * g) * sw / 100u;
    return e->t;
}

static void sq_insert(sq_seg_t *s, uint32_t t, uint32_t pad, uint32_t vel, int32_t semis, uint32_t flags)
{
    uint32_t i, j;
    for (i = 0; i < s->n && s->ev[i].t < t; i++)
        ;
    for (j = i; j < s->n && s->ev[j].t == t; j++)
        if (s->ev[j].pad == pad && s->ev[j].semis == semis) {   /* the same hit again: the louder stays */
            if (vel > s->ev[j].vel) s->ev[j].vel = (uint8_t)vel;
            return;
        }
    if (s->n >= SQ_MAXEV)
        return;
    for (j = s->n; j > i; j--)
        s->ev[j] = s->ev[j - 1u];
    s->ev[i].t = (uint16_t)t;
    s->ev[i].pad = (uint8_t)pad;
    s->ev[i].vel = (uint8_t)vel;
    s->ev[i].semis = (int8_t)semis;
    s->ev[i].flags = (uint8_t)flags;
    s->n++;
}

static void sq_remove_if(sq_seg_t *s, uint32_t padmask, uint32_t t0, uint32_t t1)   /* pads in mask, t0 <= t < t1 */
{
    uint32_t i, k = 0;
    for (i = 0; i < s->n; i++) {
        const sq_ev_t *e = &s->ev[i];
        if (((padmask >> e->pad) & 1u) && e->t >= t0 && e->t < t1)
            continue;
        s->ev[k++] = *e;
    }
    s->n = (uint16_t)k;
}

/* a hit now (ISR): sounds, and when recording goes into the segment at the playhead, auto-corrected */
static void sq_hit(uint32_t pad, uint32_t vel, int32_t semis)
{
    sp_trigger_at(pad, vel, semis);
    if (sq.recording && sq.playing && sq.countin <= 0) {
        sq_seg_t *s = &sq_seg[sq.seg % SQ_NSEG];
        uint32_t len = sq_len(s), now = sq.pos >> 16, g = SQ_GRID[sq.quant % 7u], t = (now + g / 2u) / g * g;
        if (t >= len)
            t -= len;
        sq_insert(s, t, pad, vel, semis, t > now || (t < now && now - t > len / 2u) ? SQF_SKIP : 0u);
    }
}

static void sq_play(uint32_t on)               /* PLAY / STOP (ISR side: from sq_block's requests) */
{
    if (on) {
        sq.pos = 0;
        sq.beat = 0;
        sq.song_i = sq.song_rep = 0;
        if (sq.song_mode && sq.song_n)
            sq.seg = sq_song[0].seg;
        sq.countin = sq.rec_arm ? (int32_t)(SQ_BAR << 16) : 0;
        if (sq.rec_arm)
            sq.recording = 1;
        sq.rec_arm = 0;
        sq.playing = 1;
    } else {
        sq.playing = 0;
        sq.recording = 0;
        sq.countin = 0;
    }
}

/* the segment's end: the next one of the song, or the same again */
static void sq_wrap(void)
{
    if (!sq.song_mode || !sq.song_n)
        return;
    if (++sq.song_rep >= (sq_song[sq.song_i].rep ? sq_song[sq.song_i].rep : 1u)) {
        sq.song_rep = 0;
        if (++sq.song_i >= sq.song_n) {         /* the song's end: stop */
            sq_play(0);
            return;
        }
    }
    sq.seg = sq_song[sq.song_i].seg % SQ_NSEG;
}

/* the click: an accent on the bar */
static void sq_click_at(uint32_t t0, uint32_t t1)   /* beats in [t0, t1) */
{
    uint32_t b = (t0 + SQ_PPQ - 1u) / SQ_PPQ * SQ_PPQ;
    if (b < t1) {
        sq.beat++;
        if (sq.click == 2u || (sq.click == 1u && sq.recording))
            sp_click(b % SQ_BAR == 0u);
    }
}

/* one audio block (SP_BLK samples): requests, the count-in, the hits whose time has come */
static void sq_block(void)
{
    sq_seg_t *s = &sq_seg[sq.seg % SQ_NSEG];
    uint32_t len, from, to, i, d;
    if (sq.req_clear) {
        sq.req_clear = 0;
        s->n = 0;
    }
    if (sq.req_wipe) {
        sq_remove_if(s, 1u << ((sq.req_wipe - 1u) & 31u), 0, 0xFFFFu);
        sq.req_wipe = 0;
    }
    if (sq.req_play) {
        sq_play(sq.req_play == 1u);
        sq.req_play = 0;
        s = &sq_seg[sq.seg % SQ_NSEG];
    }
    if (!sq.playing)
        return;
    d = (uint32_t)sq.bpm10 * 76087u / 10000u;   /* ticks Q16 a block: bpm10 * 96 * 32 * 65536 / (600 * 44100) */
    if (sq.countin > 0) {                        /* the count-in: a bar of clicks, whatever CLICK is */
        uint32_t c0 = SQ_BAR - (uint32_t)(sq.countin >> 16), c1, b;
        sq.countin -= (int32_t)d;
        c1 = sq.countin > 0 ? SQ_BAR - (uint32_t)(sq.countin >> 16) : SQ_BAR;
        b = (c0 + SQ_PPQ - 1u) / SQ_PPQ * SQ_PPQ;
        if (b < c1)
            sp_click(b == 0u);
        if (sq.countin > 0)
            return;
        sq.pos = (uint32_t)(-sq.countin);       /* (what is left of the block) */
        sq.countin = 0;
        from = 0;
    } else {
        from = sq.pos >> 16;
        sq.pos += d;
    }
    len = sq_len(s);
    to = sq.pos >> 16;
    for (;;) {
        uint32_t end = to < len ? to : len;
        if (end > from)
            sq_click_at(from, end);
        if (sq.erase)
            sq_remove_if(s, sq.erase, from, end);
        for (i = 0; i < s->n; i++) {
            sq_ev_t *e = &s->ev[i];
            uint32_t pt = sq_play_t(e);
            if (pt >= len)
                pt -= len;
            if (pt < from || pt >= end || (e->t >= len))
                continue;
            if (e->flags & SQF_SKIP) {
                e->flags &= (uint8_t)~SQF_SKIP;
                continue;
            }
            sp_trigger_at(e->pad, e->vel, e->semis);
        }
        if (to < len)
            break;
        to -= len;                                   /* the end of the segment: around, or on in the song */
        sq.pos -= len << 16;
        from = 0;
        sq_wrap();
        if (!sq.playing)
            return;
        s = &sq_seg[sq.seg % SQ_NSEG];
        len = sq_len(s);
        if (to >= len)
            to = len - 1u;
    }
}

static void sq_init(void)
{
    uint32_t i;
    for (i = 0; i < SQ_NSEG; i++) {
        sq_seg[i].bars = 1;
        sq_seg[i].n = 0;
    }
}
