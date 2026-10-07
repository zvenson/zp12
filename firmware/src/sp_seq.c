/* SPDX-License-Identifier: GPL-3.0-only */
/* zp12's sequencer, as a 12-bit drum machine's: segments of 1-32 bars (or AUTO: the first take sets the length)
 * at 96 ticks a quarter, hits recorded in real time (quantised by AUTO CORRECT as they come in) or set step by
 * step, swing at playback, four songs of loops with repeats, a count-in and a click.
 *
 * Everything that touches a segment runs in the audio ISR (sq_block): the main loop posts requests into a
 * ring (sq_post), so the playing never sees half an edit. A hit is one word: tick, pad, one of 8 levels (as the
 * original's dynamic buttons), semitones (MULTI PITCH), and SKIP: recorded ahead of the playhead (quantised
 * forward) and already heard live, so not played again this pass. LK: the hit has its own TUNE / DECAY / CUT
 * (sq_lk, beside it): recorded while those knobs were turned, so a sweep played in stays in the loop. */
#define SQ_PPQ 96u
#define SQ_BAR (SQ_PPQ * 4u)
#define SQ_NSEG 16u
#define SQ_MAXEV 512u
#define SQ_MAXBARS 32u
#define SQ_NSONG 32u

typedef struct {
    uint32_t t : 14;                            /* tick in the segment (32 bars = 12288) */
    uint32_t pad : 5;
    uint32_t lvl : 3;                           /* 0..7: velocity (lvl + 1) * 16 - 1 */
    uint32_t skip : 1;
    int32_t semis : 6;                          /* -32..31 */
    uint32_t lk : 1;                            /* its lock in sq_lk */
    uint32_t rsv : 2;
} sq_ev_t;
typedef struct { uint8_t bars, rsv; uint16_t n; sq_ev_t ev[SQ_MAXEV]; } sq_seg_t;   /* bars 0: AUTO; ev by t */

static sq_seg_t sq_seg[SQ_NSEG] __attribute__((section(".pool")));
static uint32_t sq_lk[SQ_NSEG][SQ_MAXEV] __attribute__((section(".pool")));   /* the hits' locks (sp_lock_of) */
static sq_seg_t sq_undo __attribute__((section(".pool")));                    /* a segment before CLEAR / ERASE / COPY */
static uint32_t sq_undo_lk[SQ_MAXEV] __attribute__((section(".pool")));
static uint8_t sq_undo_of = 0xFF;                                             /* its segment (0xFF: nothing to undo) */
#define SQ_LK(s) (sq_lk[(s) - sq_seg])
#define SQ_SONGS 4u
static struct { uint8_t seg, rep; } sq_songs[SQ_SONGS][SQ_NSONG];   /* four songs, each a chain of loops */
static uint8_t sq_song_len[SQ_SONGS];
#define sq_song (sq_songs[sq.song_sel & 3u])    /* the song chosen: played, edited */
#define SQ_SONG_N (sq_song_len[sq.song_sel & 3u])

/* AUTO CORRECT: the grid in ticks (0: off, 1-tick resolution) and the swing steps */
static const uint8_t SQ_GRID[7] = {1, 48, 32, 24, 16, 12, 8};
static const char *const SQ_GRID_NAME[7] = {"OFF", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T"};
static const uint8_t SQ_SWING[6] = {50, 54, 58, 63, 67, 71};

static struct {
    volatile uint8_t playing, recording, song_mode;
    volatile uint8_t rec_arm;                   /* REC pressed while stopped: PLAY starts with a count-in */
    volatile uint8_t seg;                       /* the segment playing / edited */
    volatile uint8_t next_seg;                  /* a loop chosen while playing: from the end of this one (0xFF none) */
    uint8_t quant, swing;                       /* SQ_GRID index, SQ_SWING index */
    uint8_t click;                              /* 0 off, 1 while recording, 2 always */
    uint8_t song_sel, song_i, song_rep;
    uint16_t bpm10;
    volatile uint32_t pos;                      /* the playhead in the segment, ticks Q16 */
    volatile int32_t countin;                   /* ticks Q16 of count-in left (> 0: counting) */
    volatile uint32_t erase;                    /* pads held with ERASE: their hits go as the playhead passes */
    volatile uint32_t beat;                     /* beats since PLAY (the UI's blink) */
    volatile uint32_t played;                   /* pads hit since the UI looked (their keys light) */
    volatile uint32_t gen;                      /* bumps on every change of a segment (the UI, the autosave) */
    volatile uint32_t turn;                     /* pads whose TUNE / DECAY / CUT is being turned: recorded with the hits */
} sq = {0, 0, 0, 0, 0, 0xFF, 3, 0, 1, 0, 0, 0, 900, 0, 0, 0, 0, 0, 0};

static uint32_t sq_lvl_vel(uint32_t lvl) { return (lvl + 1u) * 16u - 1u; }
static uint32_t sq_vel_lvl(uint32_t vel) { return vel >= 127u ? 7u : vel / 16u; }

/* the length in ticks; an AUTO segment while its first take runs: the most there is */
static uint32_t sq_len(const sq_seg_t *s)
{
    if (!s->bars)
        return sq.recording ? SQ_MAXBARS * SQ_BAR : SQ_BAR;
    return (uint32_t)s->bars * SQ_BAR;
}

/* the tick a hit sounds at: odd 1/16 (or 1/8 with AUTO CORRECT 1/8) late by the swing */
static uint32_t sq_play_t(const sq_ev_t *e)
{
    uint32_t g = sq.quant == 1u ? 48u : 24u, sw = SQ_SWING[sq.swing % 6u] - 50u;
    if (sw && e->t % (2u * g) == g)
        return e->t + (2u * g) * sw / 100u;
    return e->t;
}

static int sq_find(const sq_seg_t *s, uint32_t t, uint32_t pad)   /* index of the hit, -1 */
{
    uint32_t i;
    for (i = 0; i < s->n && s->ev[i].t <= t; i++)
        if (s->ev[i].t == t && s->ev[i].pad == pad)
            return (int)i;
    return -1;
}

static void sq_insert(sq_seg_t *s, uint32_t t, uint32_t pad, uint32_t lvl, int32_t semis, uint32_t skip, uint32_t lk)
{
    uint32_t i, j, *L = SQ_LK(s);
    int k = sq_find(s, t, pad);
    if (k >= 0) {                               /* the same hit again: the louder stays */
        if (lvl > s->ev[k].lvl) s->ev[k].lvl = lvl & 7u;
        s->ev[k].semis = semis;
        if (lk) { s->ev[k].lk = 1; L[k] = lk; }
        return;
    }
    if (s->n >= SQ_MAXEV)
        return;
    for (i = 0; i < s->n && s->ev[i].t <= t; i++)
        ;
    for (j = s->n; j > i; j--) {
        s->ev[j] = s->ev[j - 1u];
        L[j] = L[j - 1u];
    }
    s->ev[i].t = t;
    s->ev[i].pad = pad & 31u;
    s->ev[i].lvl = lvl & 7u;
    s->ev[i].semis = semis;
    s->ev[i].skip = skip & 1u;
    s->ev[i].lk = lk != 0u;
    s->ev[i].rsv = 0;
    L[i] = lk;
    s->n++;
    sq.gen++;
}

static void sq_remove_at(sq_seg_t *s, uint32_t i)
{
    uint32_t *L = SQ_LK(s);
    for (; i + 1u < s->n; i++) {
        s->ev[i] = s->ev[i + 1u];
        L[i] = L[i + 1u];
    }
    s->n--;
    sq.gen++;
}

static void sq_remove_if(sq_seg_t *s, uint32_t padmask, uint32_t t0, uint32_t t1)   /* pads in mask, t0 <= t < t1 */
{
    uint32_t i, k = 0, n = s->n, *L = SQ_LK(s);
    for (i = 0; i < n; i++) {
        sq_ev_t e = s->ev[i];
        if (((padmask >> e.pad) & 1u) && e.t >= t0 && e.t < t1)
            continue;
        L[k] = L[i];
        s->ev[k++] = e;
    }
    if (k != n) {
        s->n = (uint16_t)k;
        sq.gen++;
    }
}

/* segment s (and its locks) into d */
static void sq_seg_copy(sq_seg_t *d, uint32_t *dl, const sq_seg_t *s, const uint32_t *sl)
{
    uint32_t i;
    d->bars = s->bars;
    d->n = s->n;
    for (i = 0; i < s->n; i++) {
        d->ev[i] = s->ev[i];
        dl[i] = sl[i];
    }
}

static void sq_keep_undo(sq_seg_t *s)          /* before a segment is cleared, erased or written over */
{
    sq_seg_copy(&sq_undo, sq_undo_lk, s, SQ_LK(s));
    sq_undo_of = (uint8_t)(s - sq_seg);
}

/* a hit now (ISR): sounds, and when recording goes into the segment at the playhead, auto-corrected; a pad
 * whose knobs are being turned records them with the hit */
static void sq_hit(uint32_t pad, uint32_t vel, int32_t semis)
{
    sp_trigger_at(pad, vel, semis);
    sq.played |= 1u << (pad & 31u);
    if (sq.recording && sq.playing && sq.countin <= 0) {
        sq_seg_t *s = &sq_seg[sq.seg % SQ_NSEG];
        uint32_t len = sq_len(s), now = sq.pos >> 16, g = SQ_GRID[sq.quant % 7u], t = (now + g / 2u) / g * g;
        if (t >= len)
            t -= len;
        sq_insert(s, t, pad, sq_vel_lvl(vel), semis, t > now || (t < now && now - t > len / 2u),
                  (sq.turn >> (pad & 31u)) & 1u ? sp_lock_of(&sp_sound[pad & 31u]) : 0u);
    }
}

static void sq_stop_rec(void)                  /* recording ends: an AUTO segment gets its length */
{
    sq_seg_t *s = &sq_seg[sq.seg % SQ_NSEG];
    if (sq.recording && !s->bars && s->n) {
        uint32_t t = sq.pos >> 16, bars = (t + SQ_BAR * 3u / 4u) / SQ_BAR;   /* (up to a quarter past a bar line: that bar) */
        if (bars < 1u) bars = 1u;
        if (bars > SQ_MAXBARS) bars = SQ_MAXBARS;
        s->bars = (uint8_t)bars;
        sq.pos %= (bars * SQ_BAR) << 16;
        sq.gen++;
    }
    sq.recording = 0;
}

static void sq_play(uint32_t on)
{
    if (on) {
        sq.pos = 0;
        sq.beat = 0;
        sq.song_i = sq.song_rep = 0;
        if (sq.song_mode && SQ_SONG_N)
            sq.seg = sq_song[0].seg;
        sq.countin = sq.rec_arm ? (int32_t)(SQ_BAR << 16) : 0;
        if (sq.rec_arm)
            sq.recording = 1;
        sq.rec_arm = 0;
        sq.playing = 1;
    } else {
        sq_stop_rec();
        sq.playing = 0;
        sq.countin = 0;
    }
}

/* ---- requests from the main loop (one word each: op, pad, argument) */
enum { RQ_HIT, RQ_PLAY, RQ_STOP, RQ_REC, RQ_STEP, RQ_WIPE, RQ_CLEAR, RQ_COPY, RQ_LOOP, RQ_RECSET, RQ_UNDO };
#define SQ_NRQ 64u
static uint32_t sq_rq[SQ_NRQ];
static volatile uint32_t sq_rq_w, sq_rq_r;
static void sq_post(uint32_t op, uint32_t pad, uint32_t arg)   /* arg: 16 bits (RQ_HIT: vel | (semis + 64) << 8) */
{
    if (sq_rq_w - sq_rq_r < SQ_NRQ) {
        sq_rq[sq_rq_w % SQ_NRQ] = op | (pad & 31u) << 4 | (arg & 0xFFFFu) << 16;
        __asm__ volatile("" ::: "memory");
        sq_rq_w++;
    }
}

static void sq_request(uint32_t r)
{
    uint32_t op = r & 15u, pad = (r >> 4) & 31u, arg = r >> 16;
    sq_seg_t *s = &sq_seg[sq.seg % SQ_NSEG];
    switch (op) {
    case RQ_HIT:
        sq_hit(pad, arg & 0xFFu, (int32_t)(arg >> 8) - 64);
        break;
    case RQ_PLAY:
        sq_play(1);
        break;
    case RQ_STOP:
        sq_play(0);
        break;
    case RQ_REC:                                /* REC: playing: overdub on / off; stopped: armed / not */
        if (sq.playing) {
            if (sq.recording) sq_stop_rec();
            else sq.recording = 1;
        } else {
            sq.rec_arm ^= 1u;
        }
        break;
    case RQ_RECSET:                             /* REC held: its press undone (arg: recording | rec_arm << 1) */
        sq.recording = (uint8_t)(arg & 1u && sq.playing);
        sq.rec_arm = (uint8_t)((arg >> 1) & 1u && !sq.playing);
        break;
    case RQ_STEP: {                             /* step edit: the hit of pad at tick arg on / off */
        int k = sq_find(s, arg, pad);
        if (k >= 0)
            sq_remove_at(s, (uint32_t)k);
        else
            sq_insert(s, arg, pad, 6u, 0, 0, 0);
        break;
    }
    case RQ_WIPE:
        sq_keep_undo(s);
        sq_remove_if(s, 1u << pad, 0, 0xFFFFu);
        break;
    case RQ_CLEAR:
        if (s->n)
            sq_keep_undo(s);
        s->n = 0;
        sq.gen++;
        break;
    case RQ_COPY: {                             /* the segment into segment arg (its length too) */
        sq_seg_t *d = &sq_seg[arg % SQ_NSEG];
        if (d != s) {
            sq_keep_undo(d);
            sq_seg_copy(d, SQ_LK(d), s, SQ_LK(s));
            sq.gen++;
        }
        break;
    }
    case RQ_UNDO:                               /* the segment as it was; again: as it is now (redo) */
        if (sq_undo_of < SQ_NSEG) {
            sq_seg_t *d = &sq_seg[sq_undo_of];
            uint32_t i, n = d->n > sq_undo.n ? d->n : sq_undo.n, *L = SQ_LK(d);
            uint8_t b = d->bars;
            uint16_t c = d->n;
            for (i = 0; i < n; i++) {
                sq_ev_t e = d->ev[i];
                uint32_t l = L[i];
                d->ev[i] = sq_undo.ev[i];
                L[i] = sq_undo_lk[i];
                sq_undo.ev[i] = e;
                sq_undo_lk[i] = l;
            }
            d->bars = sq_undo.bars;
            d->n = sq_undo.n;
            sq_undo.bars = b;
            sq_undo.n = c;
            sq.gen++;
        }
        break;
    case RQ_LOOP:                               /* a loop (segment): stopped at once, playing from this one's end */
        if (arg >= SQ_NSEG)
            break;
        if (!sq.playing || sq.countin > 0) {
            sq.seg = (uint8_t)arg;
            sq.next_seg = 0xFF;
        } else if (sq.recording && !s->bars) {
            break;                              /* (an AUTO take runs: it decides its length first) */
        } else {
            sq.next_seg = arg == sq.seg ? 0xFFu : (uint8_t)arg;
        }
        sq.song_mode = 0;
        break;
    default:
        break;
    }
}

/* the segment's end: the next one of the song, or the same again */
static void sq_wrap(void)
{
    if (sq.next_seg < SQ_NSEG) {                /* the loop chosen */
        sq.seg = sq.next_seg;
        sq.next_seg = 0xFF;
        return;
    }
    if (!sq.song_mode || !SQ_SONG_N)
        return;
    if (++sq.song_rep >= (sq_song[sq.song_i].rep ? sq_song[sq.song_i].rep : 1u)) {
        sq.song_rep = 0;
        if (++sq.song_i >= SQ_SONG_N) {         /* the song's end: stop */
            sq_play(0);
            return;
        }
    }
    sq.seg = sq_song[sq.song_i].seg % SQ_NSEG;
}

static void sq_click_at(uint32_t t0, uint32_t t1)   /* a beat in [t0, t1): the click, the bar's first accented */
{
    uint32_t b = (t0 + SQ_PPQ - 1u) / SQ_PPQ * SQ_PPQ;
    if (b < t1) {
        sq.beat++;
        if (sq.click == 2u || (sq.click == 1u && sq.recording))
            sp_click(b % SQ_BAR == 0u);
    }
}

/* one audio block (SP_BLK samples): the requests, the count-in, the hits whose time has come */
static void sq_block(void)
{
    sq_seg_t *s;
    uint32_t len, from, to, i, d;
    while (sq_rq_r != sq_rq_w) {
        sq_request(sq_rq[sq_rq_r % SQ_NRQ]);
        sq_rq_r++;
    }
    if (!sq.playing)
        return;
    s = &sq_seg[sq.seg % SQ_NSEG];
    d = (uint32_t)sq.bpm10 * 76087u / 10000u;   /* ticks Q16 a block: bpm10 * 96 * 32 * 65536 / (600 * 44100) */
    if (sq.countin > 0) {                        /* the count-in: a bar of clicks, whatever CLICK is */
        uint32_t c0 = SQ_BAR - (uint32_t)(sq.countin >> 16), c1, b;
        sq.countin -= (int32_t)d;
        c1 = sq.countin > 0 ? SQ_BAR - (uint32_t)(sq.countin >> 16) : SQ_BAR;
        b = (c0 + SQ_PPQ - 1u) / SQ_PPQ * SQ_PPQ;
        if (b < c1) {
            sp_click(b == 0u);
            sq.beat++;
        }
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
        uint32_t *L = SQ_LK(s);
        if (sq.erase)
            sq_remove_if(s, sq.erase, from, end);
        for (i = 0; i < s->n; i++) {
            sq_ev_t *e = &s->ev[i];
            uint32_t pt = sq_play_t(e);
            if (pt >= len)
                pt -= len;
            if (pt < from || pt >= end || e->t >= len)
                continue;
            if (e->skip) {
                e->skip = 0;
                continue;
            }
            if (sq.recording && (sq.turn >> e->pad) & 1u) {   /* its pad's knobs turned: the hit takes them */
                L[i] = sp_lock_of(&sp_sound[e->pad]);
                if (!e->lk) { e->lk = 1; sq.gen++; }
            }
            sp_trigger_lk(e->pad, sq_lvl_vel(e->lvl), e->semis, e->lk ? L[i] : 0u);
            sq.played |= 1u << e->pad;
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
        sq_seg[i].bars = 0;                     /* AUTO until a take or LENGTH sets it */
        sq_seg[i].n = 0;
    }
    sq_undo_of = 0xFF;
}
