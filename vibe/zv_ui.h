/* SPDX-License-Identifier: GPL-3.0-only */
/* ZP12 on the VIBE – the panel. Few pages, everything at a glance (480 x 320):
 *
 *   top        ZP12 · the six pages (K1-K6; the same K again = its next page) · loop, tempo, transport
 *   the LCD    the page: a waveform with START / END, the faders, the loop's hits, the steps, the song
 *   right      the 16 pads as they lie under the fingers: bank A + B (MENU: C + D), lit when they play
 *   below      ENCODER 1-4 = the four values of the page · F1-F6 (F5 SAVE, F6 SETUP everywhere)
 *
 *   K1 SAMPLE  REC onto the selected pad: SOURCE (LINE MIC HI-Z IN 1+2 USB) · GAIN · THRESH · MON; F2 RATE, F3 BITS, F4 LOAD
 *   K2 EDIT    SOUND (TUNE FINE DECAY LEVEL) · TRUNC (START END REVERSE SPEED) · OUT (CHOP PAN CUT RESO) · SENDS
 *   K3 MIX     the eight channel faders, 1-4 / 5-8 · F1 MUTE, F2 SOLO (tapped: the selected pad's channel, held + pads)
 *   K4 FX      CHORUS · DELAY · REVERB · FILTER (a DJ filter on the mix)
 *   K5 SEQ     LOOP (LOOP BARS QUANT SWING, the hits of the loop) · STEPS (the pads are the 16 steps of a bar)
 *   K6 SONG    four songs of loops with repeats
 *   SHIFT + K1-K6 plays pads 1-6 (as in ZVAMPLER) · A = tempo · B = loop · DATA = the sound · KEYS = the sound, chromatic */
#define C_BG 0x0B1226
#define C_PANEL 0x1E3060
#define C_RED 0xC82E32
#define C_LCD 0xB2BE92
#define C_LCD2 0xA3B083
#define C_INK 0x10180E
#define C_DIM 0x6E7C60
#define C_LT 0xE4E6EA
#define C_MUT 0x8C9CC4
#define C_KEY 0x26304A
static const uint32_t CH_RGB[8] = { 0xFF5A5A, 0xFF9A3C, 0xF0C83C, 0x8CD45A, 0x3CC8B4, 0x3FA9F5, 0x9C8CFF, 0xE070D0 };

enum { PG_SAMPLE, PG_EDIT, PG_MIX, PG_FX, PG_SEQ, PG_SONG, PG_N };
static const char *const PG_NAME[PG_N] = { "SAMPLE", "EDIT", "MIX", "FX", "SEQ", "SONG" };
static const int PG_SUBS[PG_N] = { 1, 4, 3, 4, 2, 1 };
static const char *const SUB_NAME[PG_N][4] = { { "" }, { "SOUND", "TRUNC", "OUT", "SENDS" }, { "CH 1-4", "CH 5-8", "SENDS" },
                                               { "CHORUS", "DELAY", "REVERB", "FILTER" }, { "LOOP", "STEPS" }, { "" } };
static int page = PG_SAMPLE, sub[PG_N], sel, bk, key_oct, shift, help, setup_on, copy_from = -1;
/* SHIFT as in VIBE DJ / ZVAMPLER: held, or tapped – then the K keys and the pads count as SHIFT for 1.5 s (each use
 * gives it 1.5 s more, a second tap ends it); the SDK's soft-key frame lets SHIFT go with value -1: not a tap */
static int shift_used;
static uint32_t shift_down_t, shift_tap;
static float btn_val;                             /* the button event's value (main.c sets it) */
static int shift_on(void) { return shift || (shift_tap && now_ms() - shift_tap < 1500u); }
static void shift_use(void) { shift_used = 1; if (!shift && shift_tap) shift_tap = now_ms(); }
static int mix_hold, mix_used, erase_held, stp_bar, song_cur, br_on;
static uint32_t flash_t[SP_NSOUND], turn_t;
static char msg[64];
static uint32_t msg_t;
static void say(const char *s) { scopy(msg, s, sizeof msg); msg_t = now_ms(); }
static int sure_id = -1;
static uint32_t sure_t;
static int sure(int id, const char *ask)          /* a destructive key: the second press within 3 s does it */
{
    if (sure_id == id && now_ms() - sure_t < 3000u) { sure_id = -1; return 1; }
    sure_id = id; sure_t = now_ms(); say(ask); return 0;
}
static int asking(int id) { return sure_id == id && now_ms() - sure_t < 3000u; }
static void pad_label(char *b, int k) { b[0] = (char)('A' + k / 8); b[1] = (char)('1' + k % 8); b[2] = 0; }

/* ---------------------------------------------------------------- the values ------------------------------------ */
enum { P_NONE, P_SRC, P_GAIN, P_THR, P_MON, P_TUNE, P_FINE, P_DECAY, P_LEVEL, P_START, P_END, P_REV, P_SPEED,
       P_CHOP, P_PAN, P_CUT, P_RESO, P_DRIVE, P_CHO, P_DLY, P_RVB, P_FADER, P_FADER_END = P_FADER + 7,
       P_CRATE, P_CDEPTH, P_CMIX, P_DTIME, P_FDBK, P_COLR, P_DMIX, P_SIZE, P_DAMP, P_PRE, P_DJF, P_DJRES,
       P_LOOP, P_BARS, P_QUANT, P_SWING, P_BAR, P_SONG, P_SSTEP, P_SLOOP, P_SREP, P_CLICK, P_COUNT, P_DUB, P_TIME };
static const uint8_t PIDS[PG_N][4][4] = {
    { { P_SRC, P_GAIN, P_THR, P_MON } },
    { { P_TUNE, P_FINE, P_DECAY, P_LEVEL }, { P_START, P_END, P_REV, P_SPEED }, { P_CHOP, P_PAN, P_CUT, P_RESO }, { P_DRIVE, P_CHO, P_DLY, P_RVB } },
    { { P_FADER, P_FADER + 1, P_FADER + 2, P_FADER + 3 }, { P_FADER + 4, P_FADER + 5, P_FADER + 6, P_FADER + 7 }, { P_CHO, P_DLY, P_RVB, P_DRIVE } },
    { { P_CRATE, P_CDEPTH, P_CMIX, P_NONE }, { P_DTIME, P_FDBK, P_COLR, P_DMIX }, { P_SIZE, P_DAMP, P_PRE, P_NONE }, { P_DJF, P_DJRES, P_NONE, P_NONE } },
    { { P_LOOP, P_BARS, P_QUANT, P_SWING }, { P_BAR, P_LOOP, P_BARS, P_QUANT } },
    { { P_SONG, P_SSTEP, P_SLOOP, P_SREP } } };
static const uint8_t SETUP_IDS[4] = { P_CLICK, P_COUNT, P_DUB, P_TIME };
static const char *const PNAME[] = { "", "SOURCE", "GAIN", "THRESH", "MONITOR", "TUNE", "FINE", "DECAY", "LEVEL", "START", "END",
    "DIRECTION", "SPEED", "CHOP", "PAN", "CUT", "RESO", "DRIVE", "CHORUS", "DELAY", "REVERB",
    "FADER 1", "FADER 2", "FADER 3", "FADER 4", "FADER 5", "FADER 6", "FADER 7", "FADER 8",
    "RATE", "DEPTH", "MIX", "TIME", "FEEDBACK", "COLOR", "MIX", "SIZE", "DAMP", "PRE-DELAY", "FILTER", "RESO",
    "LOOP", "BARS", "QUANTIZE", "SWING", "BAR", "SONG", "STEP", "LOOP", "REPEAT", "CLICK", "COUNT-IN", "OVERDUB", "SAMPLE TIME" };
static const char *const DTIME_N[6] = { "1/4", "1/8", "1/16", "1/32", "1/8T", "1/16T" };
static int ids_now(int i) { return setup_on ? SETUP_IDS[i] : PIDS[page][sub[page] % PG_SUBS[page]][i]; }
static sp_sound_t *S(void) { return &sp_sound[sel]; }
static sq_seg_t *SEG(void) { return &sq_seg[sq.seg % SQ_NSEG]; }
static int seg_bars(void) { return SEG()->bars ? SEG()->bars : 1; }
static void prange(int id, int *lo, int *hi)
{
    *lo = 0; *hi = 127;
    switch (id) {
    case P_SRC: *hi = SRC_N - 1; break;
    case P_GAIN: *lo = -12; *hi = 36; break;
    case P_THR: *hi = 6; break;
    case P_MON: case P_REV: case P_SPEED: case P_DUB: *hi = 1; break;
    case P_TUNE: *lo = -24; *hi = 12; break;
    case P_FINE: *lo = -50; *hi = 50; break;
    case P_START: case P_END: *hi = (int)SP_TR; break;
    case P_CHOP: *hi = 8; break;
    case P_PAN: case P_DJF: *lo = -64; *hi = 63; break;
    case P_DTIME: *hi = 5; break;
    case P_PRE: *hi = 90; break;
    case P_LOOP: *hi = SQ_NSEG - 1; break;
    case P_BARS: *hi = SQ_MAXBARS; break;
    case P_QUANT: *hi = 6; break;
    case P_SWING: *hi = 5; break;
    case P_BAR: *hi = seg_bars() - 1; break;
    case P_SONG: *hi = 3; break;
    case P_SSTEP: *hi = SQ_SONG_N; break;
    case P_SLOOP: *hi = SQ_NSEG - 1; break;
    case P_SREP: *lo = 1; *hi = 16; break;
    case P_CLICK: case P_COUNT: *hi = 2; break;
    case P_TIME: *hi = 3; break;
    }
}
static int pget(int id)
{
    sp_sound_t *s = S();
    if (id >= P_FADER && id <= P_FADER_END) return sp_mix[id - P_FADER];
    switch (id) {
    case P_SRC: return in_src; case P_GAIN: return in_gain; case P_THR: return in_thr; case P_MON: return in_mon;
    case P_TUNE: return s->tune; case P_FINE: return s->fine; case P_DECAY: return s->decay; case P_LEVEL: return s->level;
    case P_START: return s->start; case P_END: return s->end ? s->end : (int)SP_TR;
    case P_REV: return s->flags & SPF_REVERSE ? 1 : 0; case P_SPEED: return s->flags & SPF_33 ? 1 : 0;
    case P_CHOP: return (int)SPF_CHOP(s->flags); case P_PAN: return s->pan; case P_CUT: return s->cut; case P_RESO: return s->reso;
    case P_DRIVE: return s->drive; case P_CHO: return s->send[0]; case P_DLY: return s->send[1]; case P_RVB: return s->send[2];
    case P_CRATE: return fxp.crate; case P_CDEPTH: return fxp.cdepth; case P_CMIX: return fxp.cmix;
    case P_DTIME: return fxp.dtime; case P_FDBK: return fxp.fdbk; case P_COLR: return fxp.colr; case P_DMIX: return fxp.dmix;
    case P_SIZE: return fxp.size; case P_DAMP: return fxp.damp; case P_PRE: return fxp.pre;
    case P_DJF: return djf.v; case P_DJRES: return djf.res;
    case P_LOOP: return sq.next_seg < SQ_NSEG ? sq.next_seg : sq.seg;
    case P_BARS: return SEG()->bars; case P_QUANT: return sq.quant; case P_SWING: return sq.swing;
    case P_BAR: return stp_bar; case P_SONG: return sq.song_sel; case P_SSTEP: return song_cur;
    case P_SLOOP: return song_cur < SQ_SONG_N ? sq_song[song_cur].seg : -1;
    case P_SREP: return song_cur < SQ_SONG_N ? (sq_song[song_cur].rep ? sq_song[song_cur].rep : 1) : -1;
    case P_CLICK: return sq.click; case P_COUNT: return sq.cin_bars; case P_DUB: return sq.dub_bar; case P_TIME: return budget_i;
    }
    return 0;
}
static void pset(int id, int v)
{
    int lo, hi;
    sp_sound_t *s = S();
    prange(id, &lo, &hi);
    v = v < lo ? lo : v > hi ? hi : v;
    if (id >= P_FADER && id <= P_FADER_END) { sp_mix[id - P_FADER] = (uint8_t)v; touched(); return; }
    switch (id) {
    case P_SRC: in_src = v; input_on(); break;
    case P_GAIN: in_gain = v; break;
    case P_THR: in_thr = v; break;
    case P_MON: in_mon = v; break;
    case P_TUNE: s->tune = (int8_t)v; break;
    case P_FINE: s->fine = (int8_t)v; break;
    case P_DECAY: s->decay = (uint8_t)v; break;
    case P_LEVEL: s->level = (uint8_t)v; break;
    case P_START: { int e = pget(P_END); s->start = (uint16_t)(v < e - 1 ? v : e - 1 < 0 ? 0 : e - 1); break; }
    case P_END: { int b = s->start; s->end = (uint16_t)(v > b + 1 ? v : b + 1); break; }
    case P_REV: s->flags = (uint8_t)(v ? s->flags | SPF_REVERSE : s->flags & ~SPF_REVERSE); break;
    case P_SPEED: s->flags = (uint8_t)(v ? s->flags | SPF_33 : s->flags & ~SPF_33); break;
    case P_CHOP: s->flags = SPF_CHOP_SET(s->flags, v); break;
    case P_PAN: s->pan = (int8_t)v; break;
    case P_CUT: if (sel % 8 > 1) { say("FILTER: ONLY PADS 1-2 OF A BANK (CHANNELS 1-2)"); return; } s->cut = (uint8_t)v; break;
    case P_RESO: if (sel % 8 > 1) { say("FILTER: ONLY PADS 1-2 OF A BANK (CHANNELS 1-2)"); return; } s->reso = (uint8_t)v; break;
    case P_DRIVE: s->drive = (uint8_t)v; break;
    case P_CHO: s->send[0] = (uint8_t)v; break;
    case P_DLY: s->send[1] = (uint8_t)v; break;
    case P_RVB: s->send[2] = (uint8_t)v; break;
    case P_CRATE: fxp.crate = (int16_t)v; break; case P_CDEPTH: fxp.cdepth = (int16_t)v; break; case P_CMIX: fxp.cmix = (int16_t)v; break;
    case P_DTIME: fxp.dtime = (int16_t)v; break; case P_FDBK: fxp.fdbk = (int16_t)v; break; case P_COLR: fxp.colr = (int16_t)v; break;
    case P_DMIX: fxp.dmix = (int16_t)v; break; case P_SIZE: fxp.size = (int16_t)v; break; case P_DAMP: fxp.damp = (int16_t)v; break;
    case P_PRE: fxp.pre = (int16_t)v; break;
    case P_DJF: djf.v = (int8_t)v; return;                        /* (a live knob: not kept) */
    case P_DJRES: djf.res = (int8_t)v; return;
    case P_LOOP: sq_post(RQ_LOOP, 0, (uint32_t)v); stp_bar = 0; break;
    case P_BARS: SEG()->bars = (uint8_t)v; sq.gen++; if (stp_bar >= seg_bars()) stp_bar = seg_bars() - 1; break;
    case P_QUANT: sq.quant = (uint8_t)v; break;
    case P_SWING: sq.swing = (uint8_t)v; break;
    case P_BAR: stp_bar = v; return;
    case P_SONG: sq.song_sel = (uint8_t)v; song_cur = 0; break;
    case P_SSTEP: song_cur = v; return;
    case P_SLOOP:
        if (song_cur >= SQ_SONG_N) { if (SQ_SONG_N >= SQ_NSONG) return; sq_song[SQ_SONG_N].seg = (uint8_t)v; sq_song[SQ_SONG_N].rep = 1; SQ_SONG_N++; }
        else sq_song[song_cur].seg = (uint8_t)v;
        break;
    case P_SREP: if (song_cur < SQ_SONG_N) sq_song[song_cur].rep = (uint8_t)v; break;
    case P_CLICK: sq.click = (uint8_t)v; break;
    case P_COUNT: sq.cin_bars = (uint8_t)v; break;
    case P_DUB: sq.dub_bar = (uint8_t)v; break;
    case P_TIME:
        if (BUDGET_S[v] * NIB_S < used_nib()) { say("THE SAMPLES USE MORE – DELETE SOME FIRST"); return; }
        budget_i = v; break;
    }
    if (id == P_TUNE || id == P_FINE || id == P_DECAY || id == P_CUT) { sq.turn |= 1u << sel; turn_t = now_ms(); }   /* (REC: the hits take it) */
    touched();
}
static void secs(char *b, uint32_t n, uint32_t rate, int dec)
{
    char t[16];
    b[0] = 0;
    float s = rate ? (float)n / (float)rate : 0;
    if (s >= 60.0f) { int m = (int)(s / 60.0f); icat(b, m, 16); scat(b, ":", 16); s -= (float)m * 60.0f; if (s < 10.0f) scat(b, "0", 16); }
    scat(b, vg_ftoa(s, dec, t), 16);
}
static int pfmt(int id, char *b)                  /* the value as shown; 0 = not there (shown dim) */
{
    char t[16];
    int v = pget(id);
    sp_sound_t *s = S();
    const sp_wave_t *w = s->wave < SP_NWAVE ? &sp_wave[s->wave] : 0;
    b[0] = 0;
    if (id >= P_FADER && id <= P_FADER_END) {
        if (!v) { scopy(b, "-INF", 16); return 1; }
        float db = 40.0f * vd_log2((float)v / 100.0f) * 0.30103f;
        if (db >= 0.05f) scat(b, "+", 16);
        scat(b, vg_ftoa(db, 1, t), 16); scat(b, " dB", 16); return 1;
    }
    switch (id) {
    case P_NONE: return 0;
    case P_SRC: scopy(b, SRC_NAME[v], 16); if (v == SRC_USB && usb_input() < 0) { scopy(b, "USB --", 16); return 0; } return 1;
    case P_GAIN: if (v > 0) scat(b, "+", 16); icat(b, v, 16); scat(b, " dB", 16); return 1;
    case P_THR: if (!v) scopy(b, "OFF", 16); else { icat(b, THR_DB[v], 16); scat(b, " dB", 16); } return 1;
    case P_MON: case P_CLICK: scopy(b, v == 2 ? "ON" : v ? (id == P_CLICK ? "REC" : "ON") : "OFF", 16); return 1;
    case P_TUNE: if (v > 0) scat(b, "+", 16); icat(b, v, 16); return 1;
    case P_FINE: if (v > 0) scat(b, "+", 16); icat(b, v, 16); scat(b, " ct", 16); return 1;
    case P_DECAY: if (v >= 127) scopy(b, "FULL", 16); else icat(b, v, 16); return 1;
    case P_START: case P_END:
        if (!w || !w->n) { scopy(b, "--", 16); return 0; }
        secs(b, (uint32_t)((uint64_t)w->n * (uint32_t)v / SP_TR), w->rate, 3); scat(b, " S", 16); return 1;
    case P_REV: scopy(b, v ? "REVERSE" : "FORWARD", 16); return 1;
    case P_SPEED: scopy(b, v ? "45 > 33" : "NORMAL", 16); return 1;
    case P_CHOP: if (!v) scopy(b, "--", 16); else icat(b, v, 16); return 1;
    case P_PAN: if (!v) scopy(b, "C", 16); else { scat(b, v < 0 ? "L " : "R ", 16); icat(b, v < 0 ? -v : v, 16); } return 1;
    case P_CUT: case P_RESO: if (sel % 8 > 1) { scopy(b, "--", 16); return 0; } icat(b, v, 16); return 1;
    case P_DTIME: scopy(b, DTIME_N[v % 6], 16); return 1;
    case P_PRE: icat(b, v, 16); scat(b, " ms", 16); return 1;
    case P_DJF: if (!v) scopy(b, "OFF", 16); else { scat(b, v < 0 ? "LP " : "HP ", 16); icat(b, v < 0 ? -v : v, 16); } return 1;
    case P_LOOP: icat2(b, v + 1, 16); if (sq.next_seg < SQ_NSEG) scat(b, " NEXT", 16); return 1;
    case P_BARS: if (!v) scopy(b, "AUTO", 16); else icat(b, v, 16); return 1;
    case P_QUANT: scopy(b, SQ_GRID_NAME[v % 7], 16); return 1;
    case P_SWING: icat(b, SQ_SWING[v % 6], 16); scat(b, " %", 16); return 1;
    case P_BAR: icat(b, v + 1, 16); scat(b, " / ", 16); icat(b, seg_bars(), 16); return 1;
    case P_SONG: icat(b, v + 1, 16); return 1;
    case P_SSTEP: if (v >= SQ_SONG_N) scopy(b, "END", 16); else icat(b, v + 1, 16); return 1;
    case P_SLOOP: if (v < 0) { scopy(b, "ADD", 16); return 1; } scopy(b, "LOOP ", 16); icat2(b, v + 1, 16); return 1;
    case P_SREP: if (v < 0) { scopy(b, "--", 16); return 0; } scat(b, "x ", 16); icat(b, v, 16); return 1;
    case P_COUNT: if (!v) scopy(b, "OFF", 16); else { icat(b, v, 16); scat(b, v > 1 ? " BARS" : " BAR", 16); } return 1;
    case P_DUB: scopy(b, v ? "FROM THE 1" : "AT ONCE", 16); return 1;
    case P_TIME: scopy(b, BUDGET_N[v], 16); return 1;
    }
    icat(b, v, 16);
    return 1;
}

/* ---------------------------------------------------------------- doing ----------------------------------------- */
static void choose(int k)                         /* the sound to edit (its bank shown) */
{
    sel = (k % SP_NSOUND + SP_NSOUND) % SP_NSOUND;
    bk = sel / 16;
}
static void hit(int k, float v)
{
    uint32_t vel = (uint32_t)(1.0f + v * 126.0f);
    SP_HIT(k, vel > 127u ? 127u : vel, 0);
    flash_t[k] = now_ms();
}
static void steps_of(int k, uint32_t bar, uint8_t on[16])   /* which of a bar's 16 steps sound k has */
{
    const sq_seg_t *s = SEG();
    for (int i = 0; i < 16; i++) on[i] = 0;
    uint32_t t0 = bar * SQ_BAR;
    for (uint32_t i = 0; i < s->n; i++)
        if (s->ev[i].pad == (uint32_t)k && s->ev[i].t >= t0 && s->ev[i].t < t0 + SQ_BAR) on[(s->ev[i].t - t0) / 24u] = (uint8_t)(1 + s->ev[i].lvl);
}
static void step_toggle(int k, int st)
{
    const sq_seg_t *s = SEG();
    uint32_t t0 = (uint32_t)stp_bar * SQ_BAR + (uint32_t)st * 24u;
    for (uint32_t i = 0; i < s->n; i++)          /* a hit in the step (wherever it was played): off */
        if (s->ev[i].pad == (uint32_t)k && s->ev[i].t >= t0 && s->ev[i].t < t0 + 24u) { sq_post(RQ_STEP, (uint32_t)k, s->ev[i].t); return; }
    if (!s->bars) { sq_seg[sq.seg % SQ_NSEG].bars = 1; sq.gen++; }   /* (an AUTO loop gets a bar to put steps in) */
    sq_post(RQ_STEP, (uint32_t)k, t0);
    if (!sq.playing) SP_HIT(k, 100, 0);
}
static void mute_toggle(int ch, int solo)
{
    if (solo) sp_solo ^= (uint8_t)(1u << ch); else sp_mute ^= (uint8_t)(1u << ch);
}
static void pad_event(int id, int down, float v)
{
    if (id < 0 || id > 15) return;
    int k = bk * 16 + id;
    if (!down) { sq.erase &= ~(1u << k); return; }
    if (copy_from >= 0) {
        char m[40] = "COPIED ";
        char a[3], b[3];
        sp_sound[k] = sp_sound[copy_from];
        pad_label(a, copy_from); pad_label(b, k);
        scat(m, a, 40); scat(m, " TO ", 40); scat(m, b, 40); say(m);
        copy_from = -1; choose(k); hit(k, v); touched(); return;
    }
    if (mix_hold) { mute_toggle(k % 8, mix_hold == 2); mix_used = 1; return; }
    if (erase_held) {
        if (sq.playing) sq.erase |= 1u << k; else sq_post(RQ_WIPE, (uint32_t)k, 0);
        return;
    }
    if (page == PG_SEQ && sub[PG_SEQ] == 1 && !setup_on && !shift_on()) { step_toggle(sel, id); return; }
    if (shift_on()) shift_use();
    choose(k);
    hit(k, v);
}
static void rec_button(void)
{
    if (rs == RS_OFF) { input_on(); if (rec_begin(sel, 0, sp_fs)) say(in_thr ? "ARMED – PLAY A SOUND" : "RECORDING"); }
    else if (rs == RS_ARMED || rs == RS_REC) rec_end();
    else if (rs == RS_LOAD) rec_end();
}
static void area_go(int i)
{
    setup_on = 0;
    if (page == i) sub[i] = (sub[i] + 1) % PG_SUBS[i];
    else page = i;
    if (page == PG_SAMPLE) input_on();
}

/* ---- LOAD: the media browser (sources, folders, WAV files) */
static char br_dir[VIBE_MEDIA_PATH], br_txt[12288];
static int br_n, br_sel, br_top, br_wait;
static char *br_name[256], *br_key[256];
static char br_kind[256];
static int ends_wav(const char *s)
{
    int n = 0; while (s[n]) n++;
    if (n < 4) return 0;
    const char *e = s + n - 4;
    return e[0] == '.' && (e[1] | 32) == 'w' && (e[2] | 32) == 'a' && (e[3] | 32) == 'v';
}
static void br_load(void)
{
    int n = br_dir[0] ? vibe_media_list(br_dir, 0, br_txt, sizeof br_txt - 1) : vibe_media_sources(br_txt, sizeof br_txt - 1);
    br_n = 0; br_wait = 0;
    if (br_dir[0]) { br_kind[0] = 'b'; br_name[0] = "\xE2\x97\x80 BACK"; br_key[0] = 0; br_n = 1; }
    if (n == VIBE_MEDIA_PENDING) { br_wait = 1; return; }
    if (n < 0) n = 0;
    br_txt[n] = 0;
    char *p = br_txt;
    while (*p && br_n < 256) {
        char *f[3] = { p, 0, 0 };
        int nf = 1;
        while (*p && *p != '\n') { if (*p == '\t' && nf < 3) { *p = 0; f[nf++] = p + 1; } p++; }
        if (*p) *p++ = 0;
        if (nf < 3) continue;
        if (!br_dir[0]) { br_kind[br_n] = 's'; br_key[br_n] = f[0]; br_name[br_n] = f[2]; br_n++; }
        else if (f[0][0] == 'd') { br_kind[br_n] = 'd'; br_key[br_n] = 0; br_name[br_n] = f[2]; br_n++; }
        else if (f[0][0] == 'f' && ends_wav(f[2])) { br_kind[br_n] = 'f'; br_key[br_n] = 0; br_name[br_n] = f[2]; br_n++; }
    }
    if (br_sel >= br_n) br_sel = br_n ? br_n - 1 : 0;
}
static void br_open(void)
{
    if (rs != RS_OFF) { say("WAIT – A TAKE IS RUNNING"); return; }
    br_on = 1; br_sel = 0; br_top = 0;
    if (vibe_media_sources(br_txt, 8) < 0) { br_on = 0; say("THIS HOST HAS NO MEDIA"); return; }
    br_load();
}
static void br_up(void)
{
    int i = 0, last = -1;
    while (br_dir[i]) { if (br_dir[i] == '/') last = i; i++; }
    if (last < 0) br_dir[0] = 0; else br_dir[last] = 0;
    br_sel = 0; br_top = 0; br_load();
}
static void br_enter(void)
{
    if (!br_n) return;
    char k = br_kind[br_sel];
    if (k == 'b') { br_up(); return; }
    if (k == 's') { scopy(br_dir, br_key[br_sel], sizeof br_dir); br_sel = 0; br_top = 0; br_load(); return; }
    char p[VIBE_MEDIA_PATH];
    scopy(p, br_dir, sizeof p); scat(p, "/", sizeof p); scat(p, br_name[br_sel], sizeof p);
    if (k == 'd') { scopy(br_dir, p, sizeof br_dir); br_sel = 0; br_top = 0; br_load(); return; }
    br_on = 0;
    load_start(sel, p, br_name[br_sel]);
    say("LOADING");
}

static void fkey(int i, int down)
{
    if (!down) {
        if (mix_hold && i == mix_hold - 1) { if (!mix_used) mute_toggle(sel % 8, mix_hold == 2); mix_hold = 0; }
        if (erase_held && i == 1) { erase_held = 0; sq.erase = 0; }
        return;
    }
    if (i == 4) { store_all(); say("SAVED"); return; }
    if (i == 5) { setup_on = !setup_on; return; }
    if (setup_on) {
        if (i == 0) { sp_smooth = !sp_smooth; touched(); say(sp_smooth ? "SMOOTH ON: CUT SOUNDS DO NOT CLICK" : "SMOOTH OFF: CUTS CLICK, AS THE ORIGINAL"); }
        return;
    }
    sp_sound_t *s = S();
    switch (page) {
    case PG_SAMPLE:
        if (i == 0) rec_button();
        else if (i == 1 && rs == RS_OFF) { rec_rate_i ^= 1; say(rec_rate_i ? "NEXT TAKE AT 27.5 kHz" : "NEXT TAKE AT 26.04 kHz"); }
        else if (i == 2 && rs == RS_OFF) { rec_16 ^= 1; say(rec_16 ? "NEXT TAKE AT 16 BIT (A QUARTER MORE TIME)" : "NEXT TAKE AT 12 BIT"); }
        else if (i == 3) br_open();
        break;
    case PG_EDIT:
        if (i == 0) {                            /* BITS of the pad's sample: 12 <-> 16 (the data is kept: nothing lost) */
            int w = s->wave;
            if (!wave_is_own(w)) { say("THE FACTORY SOUNDS ARE 12 BIT"); break; }
            if (sp_wave[w].fmt != SPW_16 && used_nib() + sp_wave[w].n > budget_nib()) { say("NOT ENOUGH SAMPLE TIME FOR 16 BIT"); break; }
            sp_wave[w].fmt = sp_wave[w].fmt == SPW_16 ? SPW_16AT12 : SPW_16;
            say(sp_wave[w].fmt == SPW_16 ? "16 BIT" : "12 BIT"); touched();
        } else if (i == 1) {
            if (copy_from >= 0) { copy_from = -1; say("NO COPY"); }
            else { char m[40] = "COPY "; char a[3]; pad_label(a, sel); scat(m, a, 40); scat(m, " – HIT THE PAD IT GOES TO", 40); copy_from = sel; say(m); }
        } else if (i == 2) {
            if (rs != RS_OFF) { say("WAIT – A TAKE IS RUNNING"); break; }
            if (!wave_is_own(s->wave)) { say("ONLY OWN SAMPLES CAN BE DELETED"); break; }
            if (sure(2, wave_users(s->wave, -1) > 1 ? "DELETE? ON MORE PADS – F3 AGAIN" : "DELETE THE SAMPLE? F3 AGAIN")) { wave_free(s->wave); say("DELETED – THE PAD HAS ITS FACTORY SOUND"); }
        }
        break;
    case PG_MIX:
        if (i == 0 || i == 1) { mix_hold = i + 1; mix_used = 0; }
        else if (i == 2) { sp_mute = sp_solo = 0; say("ALL CHANNELS HEARD"); }
        break;
    case PG_SEQ:
        if (i == 0) { if (sure(10, "CLEAR THE LOOP? F1 AGAIN")) { sq_post(RQ_CLEAR, 0, 0); say("CLEARED – F4 UNDO"); } }
        else if (i == 1) { erase_held = 1; say("ERASE: HOLD F2 + PADS"); }
        else if (i == 2) {
            int to = -1;
            for (int j = 1; j < (int)SQ_NSEG && to < 0; j++) { int c = (sq.seg + j) % SQ_NSEG; if (!sq_seg[c].n) to = c; }
            if (to < 0) { say("NO EMPTY LOOP"); break; }
            sq_post(RQ_COPY, 0, (uint32_t)to); sq_post(RQ_LOOP, 0, (uint32_t)to);
            { char m[32] = "COPIED TO LOOP "; icat2(m, to + 1, 32); say(m); }
        } else if (i == 3) { sq_post(RQ_UNDO, 0, 0); say("UNDO"); }
        break;
    case PG_SONG:
        if (i == 0 && SQ_SONG_N < SQ_NSONG) {    /* INSERT: the step at the cursor once more */
            int n = SQ_SONG_N, c = song_cur < n ? song_cur : n;
            for (int j = n; j > c; j--) sq_song[j] = sq_song[j - 1];
            if (c >= n) { sq_song[c].seg = sq.seg; sq_song[c].rep = 1; }
            SQ_SONG_N++; touched();
        } else if (i == 1 && song_cur < SQ_SONG_N) {
            for (int j = song_cur; j + 1 < SQ_SONG_N; j++) sq_song[j] = sq_song[j + 1];
            SQ_SONG_N--; touched();
        } else if (i == 2) { sq.song_mode = !sq.song_mode; say(sq.song_mode ? "SONG ON – PLAY PLAYS THE SONG" : "SONG OFF – PLAY PLAYS THE LOOP"); }
        break;
    }
}
static int k_pad[6];
static int x_pad[4] = { -1, -1, -1, -1 };
static void button(int id, int down)
{
    if (id == VB_SHIFT) {
        uint32_t now = now_ms();
        if (down) { shift = 1; shift_used = 0; shift_down_t = now; return; }
        shift = 0;
        if (btn_val >= 0 && !shift_used && now - shift_down_t < 400u) shift_tap = shift_tap && now - shift_tap < 1500u ? 0 : now;   /* (a tap; again: off) */
        return;
    }
    if (shift && down) shift_used = 1;
    if (id >= VB_X1 && id < VB_X1 + 16) {        /* encoders 1-4 pressed (a device's extras, SDK 19): the row of the pad DATA chose */
        int i = id - VB_X1;
        if (i >= 4) return;
        if (!down) { if (x_pad[i] >= 0) pad_event(x_pad[i], 0, 0); x_pad[i] = -1; return; }
        int p = (sel % 16) / 4 * 4 + i, k = bk * 16 + p;
        if (page == PG_SEQ && sub[PG_SEQ] == 1 && !setup_on) { hit(k, 0.9f); return; }   /* (STEPS: only heard, the pads are steps) */
        x_pad[i] = p; pad_event(p, 1, 0.9f);
        return;
    }
    if (id >= VB_F1 && id <= VB_F6 && !down) { fkey(id - VB_F1, 0); return; }
    if (id >= VB_K1 && id <= VB_K6 && !down) { int i = id - VB_K1; if (k_pad[i]) k_pad[i] = 0; return; }
    if (!down) return;
    if (help) { help = 0; if (id == VB_HOME || id == VB_RETURN) return; }
    if (id == VB_HOME) { help = 1; return; }
    if (br_on) {
        if (id == VB_ENTER || id == VB_DATA_PUSH || id == VB_RIGHT) { br_enter(); return; }
        if (id == VB_RETURN || id == VB_LEFT) { if (br_dir[0]) br_up(); else br_on = 0; return; }
        br_on = 0;
    }
    if (id >= VB_K1 && id <= VB_K6) {
        int i = id - VB_K1;
        if (shift_on()) { int k = bk * 16 + i; shift_use(); choose(k); hit(k, 0.8f); k_pad[i] = 1; return; }   /* SHIFT + K1-K6: pads 1-6 */
        area_go(i);
        return;
    }
    if (id >= VB_F1 && id <= VB_F6) { fkey(id - VB_F1, 1); return; }
    switch (id) {
    case VB_PLAY: sq_post(sq.playing ? RQ_STOP : RQ_PLAY, 0, 0); break;
    case VB_STOP:
        if (rs == RS_ARMED || rs == RS_REC || rs == RS_LOAD) rec_end();
        else if (sq.playing || sq.rec_arm) { sq_post(RQ_STOP, 0, 0); if (sq.rec_arm) sq_post(RQ_REC, 0, 0); }
        else for (int c = 0; c < SP_NCH; c++) sp_ch[c].on = 0;      /* (stopped: STOP silences what still rings) */
        break;
    case VB_REC: if (page == PG_SAMPLE && !setup_on) rec_button(); else sq_post(RQ_REC, 0, 0); break;
    case VB_MENU: bk ^= 1; say(bk ? "PADS: BANKS C + D" : "PADS: BANKS A + B"); break;
    case VB_OCT_DOWN: if (key_oct > -2) key_oct--; break;
    case VB_OCT_UP: if (key_oct < 2) key_oct++; break;
    case VB_LEFT: case VB_RIGHT: {
        int d = id == VB_LEFT ? -1 : 1;
        if (page == PG_SEQ && sub[PG_SEQ] == 1) { int b = stp_bar + d; stp_bar = b < 0 ? 0 : b >= seg_bars() ? seg_bars() - 1 : b; }
        else if (page == PG_SONG) { int c = song_cur + d; song_cur = c < 0 ? 0 : c > SQ_SONG_N ? SQ_SONG_N : c; }
        else choose(sel + d);
        break;
    }
    case VB_ENTER: case VB_DATA_PUSH: hit(sel, 0.8f); break;
    case VB_RETURN: if (copy_from >= 0) { copy_from = -1; say("NO COPY"); } else setup_on = 0; break;
    }
}
/* clicks into the display (SDK 18: the plugin, the browser – the VIBE has no touch): where the draw put what */
enum { HK_TAB, HK_CHIP, HK_PAD };
static struct { int16_t x, y, w, h; uint8_t k, a; } hits[40];
static int nhits;
static void hit_at(int x, int y, int w, int h, int k, int a)
{
    if (nhits < 40) { hits[nhits].x = (int16_t)x; hits[nhits].y = (int16_t)y; hits[nhits].w = (int16_t)w; hits[nhits].h = (int16_t)h; hits[nhits].k = (uint8_t)k; hits[nhits].a = (uint8_t)a; nhits++; }
}
static int touch_pad = -1;
static void touch(const vibe_event *e)
{
    int x = VIBE_TOUCH_X(e), y = VIBE_TOUCH_Y(e);
    if (e->id == VIBE_TOUCH_UP) { if (touch_pad >= 0) { pad_event(touch_pad, 0, 0); touch_pad = -1; } return; }
    if (e->id != VIBE_TOUCH_DOWN) return;
    if (help) { help = 0; return; }
    for (int i = nhits - 1; i >= 0; i--) {
        if (x < hits[i].x || y < hits[i].y || x >= hits[i].x + hits[i].w || y >= hits[i].y + hits[i].h) continue;
        int a = hits[i].a;
        if (hits[i].k == HK_TAB) { br_on = 0; setup_on = 0; page = a; if (page == PG_SAMPLE) input_on(); }
        else if (hits[i].k == HK_CHIP) sub[page] = a;
        else if (hits[i].k == HK_PAD) { touch_pad = a; pad_event(a, 1, 0.8f); }
        return;
    }
}
static float enc_acc[VE_COUNT];
static void encoder(int id, float v)
{
    if (id >= VE_COUNT) return;
    enc_acc[id] += v;
    int st = (int)enc_acc[id];
    enc_acc[id] -= (float)st;
    if (!st) return;
    if (br_on && id == VE_DATA) {
        br_sel += st; br_sel = br_sel < 0 ? 0 : br_sel >= br_n ? (br_n ? br_n - 1 : 0) : br_sel;
        return;
    }
    if (id <= VE_4) {
        int p = ids_now(id);
        if (p == P_NONE) return;
        int m = 1;
        if (p == P_START || p == P_END) m = shift ? 6 : 120;              /* (1/500 of the sound a detent; SHIFT finer) */
        pset(p, pget(p) + st * m);
        return;
    }
    if (id == VE_A) {                              /* tempo: 1 BPM a detent, SHIFT 0.1 */
        int b = sq.bpm10 + st * (shift ? 1 : 10);
        sq.bpm10 = (uint16_t)(b < 300 ? 300 : b > 3000 ? 3000 : b); touched();
        return;
    }
    if (id == VE_B) { pset(P_LOOP, pget(P_LOOP) + st); return; }
    if (id == VE_DATA) choose(sel + st * (shift ? 8 : 1));
}
static void area_event(float v)                    /* a controller's BANK - / + (and TAP: the same page again) */
{
    if (v > 0.5f) area_go((page + 1) % PG_N);
    else if (v < -0.5f) area_go((page + PG_N - 1) % PG_N);
    else area_go(page);
}

/* ---------------------------------------------------------------- the picture ----------------------------------- */
static struct { const void *d; uint32_t n; int fmt, cols; int16_t mn[300], mx[300]; } WV;
static void wave_cols(int w, int cols)
{
    const sp_wave_t *v = &sp_wave[w];
    if (WV.d == v->d && WV.n == v->n && WV.cols == cols && WV.fmt == v->fmt) return;
    WV.d = v->d; WV.n = v->n; WV.cols = cols; WV.fmt = v->fmt;
    for (int c = 0; c < cols; c++) {
        uint32_t a = (uint32_t)((uint64_t)v->n * (uint32_t)c / (uint32_t)cols), b = (uint32_t)((uint64_t)v->n * (uint32_t)(c + 1) / (uint32_t)cols);
        uint32_t step = (b - a) / 96u + 1u;        /* (a long sample: 96 looks a column are enough) */
        int32_t mn = 0, mx = 0;
        for (uint32_t i = a; i < b; i += step) { int32_t x = sp_sample_w(v, i); if (x < mn) mn = x; if (x > mx) mx = x; }
        WV.mn[c] = (int16_t)mn; WV.mx[c] = (int16_t)mx;
    }
}
static void draw_wave(int w, int x, int y, int wd, int h, int marks)
{
    vg_rect(x, y, wd, h, C_LCD2);
    if (w >= SP_NWAVE || !sp_wave[w].n) { vg_text("EMPTY PAD", x + wd / 2, y + h / 2 - 8, VG_MED, C_DIM, VG_CENTER); return; }
    wave_cols(w, wd);
    int cy = y + h / 2, s0 = 0, s1 = wd;
    if (marks) { const sp_sound_t *s = S(); s0 = (int)((int64_t)s->start * wd / SP_TR); s1 = (int)((int64_t)(s->end ? s->end : SP_TR) * wd / SP_TR); }
    for (int c = 0; c < wd; c++) {
        int a = cy - WV.mx[c] * (h / 2 - 2) / 32768, b = cy - WV.mn[c] * (h / 2 - 2) / 32768;
        vg_rect(x + c, a, 1, b - a + 1, c >= s0 && c < s1 ? C_INK : C_DIM);
    }
    if (marks) {
        vg_rect(x + s0, y, 2, h, C_RED); vg_rect(x + s0, y, 8, 8, C_RED);
        int e = s1 > 1 ? s1 - 2 : 0;
        vg_rect(x + e, y, 2, h, C_RED); vg_rect(x + e - 6, y + h - 8, 8, 8, C_RED);
    }
}
static void lcd_line(const char *s, int x, int y, int font, uint32_t c, int al) { vg_text(s, x, y, font, c, al); }
static void wave_info(char *m, int k, int n)      /* "A3 SNARE · CH 3 · 0.38 S · 26K 12 BIT" */
{
    const sp_sound_t *s = &sp_sound[k];
    char a[3], t[16];
    pad_label(a, k);
    m[0] = 0; scat(m, a, n); scat(m, " ", n); scat(m, wave_name(s->wave), n);
    scat(m, " \xC2\xB7 CH ", n); icat(m, k % 8 + 1, n);
    if (s->wave < SP_NWAVE && sp_wave[s->wave].n) {
        const sp_wave_t *w = &sp_wave[s->wave];
        scat(m, " \xC2\xB7 ", n); secs(t, w->n, w->rate, 2); scat(m, t, n); scat(m, " S \xC2\xB7 ", n);
        scat(m, w->rate == 27500 ? "27K " : "26K ", n); scat(m, w->fmt == SPW_16 ? "16 BIT" : "12 BIT", n);
    }
}
static void time_left(char *m, int n, int bits16)
{
    uint32_t b = budget_nib(), u = used_nib(), l = u >= b ? 0 : b - u;
    char t[16];
    m[0] = 0; scat(m, "TIME LEFT ", n);
    secs(t, l / (bits16 ? 4u : 3u), REC_RATE[rec_rate_i], 1); scat(m, t, n); scat(m, " S", n);
}
static uint32_t meter_t; static int meter_q;
static void draw_sample(void)
{
    char m[64], t[16];
    int bx = 12, by = 54, bw = 294, bh = 96;
    if (rs == RS_ARMED || rs == RS_REC || rs == RS_NORM || rs == RS_LOAD) {
        vg_rect(bx, by, bw, bh, C_LCD2);
        const char *what = rs == RS_ARMED ? "ARMED \xE2\x80\x93 PLAY A SOUND" : rs == RS_REC ? "RECORDING" : rs == RS_NORM ? "KEEPING IT" : "LOADING";
        vg_text(what, bx + bw / 2, by + 14, VG_MED, rs == RS_REC ? C_RED : C_INK, VG_CENTER);
        uint32_t n = rec_n - rec_n % (REC_RATE[rec_rate_i] / 4u);     /* (in quarter seconds: a calm picture) */
        secs(m, n, REC_RATE[rec_rate_i], 2); scat(m, " S", 64);
        vg_text(m, bx + bw / 2, by + 38, VG_BIG, C_INK, VG_CENTER);
        int f = rec_cap ? (int)((uint64_t)n * (uint32_t)(bw - 20) / rec_cap) : 0;
        vg_frame(bx + 10, by + 72, bw - 20, 12, 1, C_INK);
        vg_rect(bx + 10, by + 72, f, 12, C_INK);
    } else draw_wave(sp_sound[sel].wave, bx, by, bw, bh, 0);
    /* the meter: 16 steps of 3 dB, five times a second (the display stays calm: no SPI noise into the inputs) */
    if (now_ms() - meter_t > 200u) {
        float p = in_peak; in_peak = 0;
        int q = 0;
        if (p > 0.0039f) { float db = 20.0f * vd_log2(p) * 0.30103f; q = (int)((db + 48.0f) / 3.0f) + 1; q = q < 0 ? 0 : q > 16 ? 16 : q; }
        meter_q = q; meter_t = now_ms();
    }
    for (int i = 0; i < 16; i++) vg_rect(12 + i * 18, 156, 16, 10, i < meter_q ? (i >= 14 ? C_RED : C_INK) : C_LCD2);
    if (in_thr) { int tx = 12 + (int)((THR_DB[in_thr] + 48) / 3) * 18; vg_rect(tx - 1, 152, 2, 18, C_RED); }
    m[0] = 0; scat(m, SRC_NAME[in_src], 64); scat(m, " \xC2\xB7 ", 64); scat(m, rec_rate_i ? "27.5 kHz" : "26.04 kHz", 64);
    scat(m, " \xC2\xB7 ", 64); scat(m, rec_16 ? "16 BIT" : "12 BIT", 64);
    lcd_line(m, 12, 174, VG_SMALL, C_INK, VG_LEFT);
    time_left(m, 64, rec_16);
    lcd_line(m, 306, 174, VG_SMALL, C_INK, VG_RIGHT);
    if (rs == RS_OFF) {
        char a[3]; pad_label(a, sel);
        m[0] = 0; scat(m, "REC: onto pad ", 64); scat(m, a, 64); scat(m, in_thr ? " (waits for the sound)" : " (at once)", 64);
        lcd_line(m, 12, 190, VG_SMALL, C_DIM, VG_LEFT);
    }
    (void)t;
}
static void draw_edit(void)
{
    char m[64];
    wave_info(m, sel, 64);
    lcd_line(m, 12, 50, VG_SMALL, C_INK, VG_LEFT);
    draw_wave(sp_sound[sel].wave, 12, 72, 294, 112, sub[PG_EDIT] == 1);
    if (sub[PG_EDIT] == 2) lcd_line(sel % 8 < 2 ? "CHANNEL 1-2: THE RESONANT FILTER" : "CHANNEL 3-6: FIXED LOW-PASS \xC2\xB7 7-8: NONE", 12, 188, VG_SMALL, C_DIM, VG_LEFT);
    else if (copy_from >= 0) lcd_line("COPY: HIT THE PAD IT GOES TO", 12, 188, VG_SMALL, C_RED, VG_LEFT);
}
static void draw_sends(void)                     /* MIX · SENDS: the 8 pads of the bank (= channels 1-8), what each sends */
{
    static const char *const L[3] = { "C", "D", "R" };
    int k0 = sel / 8 * 8;
    for (int c = 0; c < 8; c++) {
        int k = k0 + c, x = 12 + c * 37;
        const sp_sound_t *s = &sp_sound[k];
        char a[3], nm[6];
        vg_rect(x + 2, 52, 28, 4, CH_RGB[c]);
        pad_label(a, k);
        vg_text(a, x + 16, 58, VG_BOLD, k == sel ? C_RED : C_INK, VG_CENTER);
        scopy(nm, wave_name(s->wave), 6);
        for (int j = 4; j > 2 && vg_text_width(nm, VG_SMALL) > 34; j--) nm[j] = 0;
        vg_text(nm, x + 16, 74, VG_SMALL, C_INK, VG_CENTER);
        for (int b = 0; b < 3; b++) {
            int bx = x + 3 + b * 10, h = s->send[b] * 80 / 127;
            vg_rect(bx, 94, 8, 80, C_LCD2);
            vg_rect(bx, 174 - h, 8, h, C_INK);
            vg_text(L[b], bx + 4, 177, VG_SMALL, C_DIM, VG_CENTER);
        }
        if (s->drive) vg_rect(x + 3, 194, s->drive * 28 / 127 + 1, 3, C_RED);
        if (k == sel) vg_frame(x - 1, 49, 35, 151, 2, C_RED);
    }
}
static void draw_mix(void)
{
    char t[8];
    if (sub[PG_MIX] % 3 == 2) { draw_sends(); return; }
    int f0 = sub[PG_MIX] % 3 * 4;
    vg_frame(10 + f0 * 37, 54, 4 * 37 - 2, 146, 2, C_INK);
    for (int c = 0; c < 8; c++) {
        int x = 12 + c * 37, cx = x + 16, v = sp_mix[c];
        int heard = sp_solo ? (sp_solo >> c & 1) : !(sp_mute >> c & 1);
        vg_rect(x + 2, 58, 28, 4, CH_RGB[c]);
        t[0] = (char)('1' + c); t[1] = 0;
        vg_text(t, cx, 64, VG_BOLD, C_INK, VG_CENTER);
        vg_rect(cx - 1, 84, 3, 90, C_DIM);
        int y = 174 - v * 90 / 127;
        vg_rect(cx - 11, y - 4, 22, 9, heard ? C_INK : C_DIM);
        vg_rect(cx - 11, y, 22, 1, C_LCD);
        if (sp_mute >> c & 1) { vg_rect(x + 3, 180, 12, 15, C_RED); vg_text("M", x + 9, 180, VG_SMALL, C_LT, VG_CENTER); }
        if (sp_solo >> c & 1) { vg_rect(x + 17, 180, 12, 15, C_INK); vg_text("S", x + 23, 180, VG_SMALL, C_LCD, VG_CENTER); }
    }
}
static void draw_fx(void)
{
    int s = sub[PG_FX] % 4;
    for (int r = 0; r < 4; r++) {
        int id = PIDS[PG_FX][s][r];
        if (id == P_NONE) continue;
        int lo, hi, v = pget(id), y = 64 + r * 32;
        prange(id, &lo, &hi);
        lcd_line(PNAME[id], 14, y, VG_BOLD, C_INK, VG_LEFT);
        vg_frame(110, y + 2, 190, 12, 1, C_INK);
        if (lo < 0) { int z = 110 + 95, e = 110 + (v - lo) * 189 / (hi - lo); vg_rect(e < z ? e : z, y + 2, e < z ? z - e : e - z, 12, C_INK); }
        else vg_rect(110, y + 2, (v - lo) * 190 / (hi - lo ? hi - lo : 1), 12, C_INK);
    }
    if (s == 3) lcd_line("THE WHOLE MIX \xC2\xB7 NOT KEPT", 14, 190, VG_SMALL, C_DIM, VG_LEFT);
}
static void draw_seq(void)
{
    char m[48];
    const sq_seg_t *s = SEG();
    m[0] = 0; scat(m, "LOOP ", 48); icat2(m, sq.seg % SQ_NSEG + 1, 48); scat(m, " \xC2\xB7 ", 48);
    if (s->bars) { icat(m, s->bars, 48); scat(m, s->bars > 1 ? " BARS" : " BAR", 48); } else scat(m, "AUTO", 48);
    if (sq.next_seg < SQ_NSEG) { scat(m, " > ", 48); icat2(m, sq.next_seg + 1, 48); }
    lcd_line(m, 306, 47, VG_BOLD, C_INK, VG_RIGHT);
    uint32_t len = sq_len(s), ph = sq.pos >> 16;
    int playing = sq.playing && sq.countin <= 0;
    if (sub[PG_SEQ] == 0) {                      /* the loop: a lane a channel, its hits where they are */
        int x0 = 26, w = 280, y0 = 66, lh = 16;
        for (int r = 0; r < 8; r++) {
            if (r & 1) vg_rect(x0, y0 + r * lh, w, lh, C_LCD2);
            char t[2] = { (char)('1' + r), 0 };
            vg_text(t, 14, y0 + r * lh + 1, VG_SMALL, C_INK, VG_CENTER);
        }
        uint32_t bars = len / SQ_BAR;
        for (uint32_t b = 1; b < bars; b++) vg_rect(x0 + (int)(b * SQ_BAR * (uint32_t)w / len), y0, 1, 8 * lh, C_DIM);
        for (uint32_t i = 0; i < s->n; i++) {
            const sq_ev_t *e = &s->ev[i];
            if (e->t >= len) continue;
            int x = x0 + (int)(e->t * (uint32_t)w / len), r = e->pad % 8, h = 5 + e->lvl;
            vg_rect(x, y0 + r * lh + (lh - h) / 2, 2, h, e->pad == (uint32_t)sel ? C_RED : C_INK);
        }
        if (playing && ph < len) vg_rect(x0 + (int)(ph * (uint32_t)w / len), y0, 1, 8 * lh, C_RED);
    } else {                                     /* STEPS: the 8 sounds of the selected one's bank, 16 steps of a bar */
        int k0 = sel / 8 * 8, x0 = 50, y0 = 66, lh = 16, cw = 16;
        if (playing && ph / SQ_BAR == (uint32_t)stp_bar) vg_rect(x0 + (int)(ph % SQ_BAR / 24u) * cw, y0 - 2, cw, 8 * lh + 2, C_LCD2);
        for (int r = 0; r < 8; r++) {
            uint8_t on[16];
            char a[3];
            int k = k0 + r;
            steps_of(k, (uint32_t)stp_bar, on);
            pad_label(a, k);
            vg_text(a, 12, y0 + r * lh + 1, VG_SMALL, k == sel ? C_RED : C_INK, VG_LEFT);
            for (int i = 0; i < 16; i++) {
                int x = x0 + i * cw, y = y0 + r * lh;
                if (on[i]) vg_rect(x + 1, y + 2, cw - 3, lh - 4, k == sel ? C_RED : C_INK);
                else vg_rect(x + cw / 2 - 1, y + lh / 2 - 1, 3, 3, i % 4 ? C_DIM : C_INK);
            }
        }
        m[0] = 0; scat(m, "BAR ", 48); icat(m, stp_bar + 1, 48); scat(m, " \xC2\xB7 PADS = ITS 16 STEPS \xC2\xB7 \xE2\x97\x80 \xE2\x96\xB6 BAR", 48);
        lcd_line(m, 12, 196, VG_SMALL, C_DIM, VG_LEFT);
        return;
    }
    if (sq.countin > 0) lcd_line("COUNT-IN", 160, 196, VG_BOLD, C_RED, VG_CENTER);
    else if (erase_held) lcd_line("ERASE: PADS HELD LOSE THEIR HITS", 12, 196, VG_SMALL, C_RED, VG_LEFT);
}
static void draw_song(void)
{
    int n = SQ_SONG_N;
    for (int i = 0; i <= n && i < (int)SQ_NSONG; i++) {
        int x = 12 + (i % 8) * 37, y = 56 + (i / 8) * 36;
        int now = sq.song_mode && sq.playing && i == sq.song_i;
        vg_rect(x, y, 34, 30, now ? C_INK : C_LCD2);
        uint32_t tc = now ? C_LCD : C_INK;
        if (i < n) {
            char t[8] = "L"; icat2(t, sq_song[i].seg + 1, 8);
            vg_text(t, x + 17, y + 1, VG_BOLD, tc, VG_CENTER);
            t[0] = 0; scat(t, "x", 8); icat(t, sq_song[i].rep ? sq_song[i].rep : 1, 8);
            vg_text(t, x + 17, y + 15, VG_SMALL, tc, VG_CENTER);
        } else vg_text("END", x + 17, y + 8, VG_SMALL, C_DIM, VG_CENTER);
        if (i == song_cur) vg_frame(x - 2, y - 2, 38, 34, 2, C_RED);
    }
}
static void draw_setup(void)
{
    char m[64], t[16];
    lcd_line("ENCODERS: CLICK \xC2\xB7 COUNT-IN \xC2\xB7 OVERDUB \xC2\xB7 TIME", 12, 60, VG_SMALL, C_INK, VG_LEFT);
    m[0] = 0; scat(m, "F1 SMOOTH: ", 64); scat(m, sp_smooth ? "ON (cut sounds do not click)" : "OFF (as the original)", 64);
    lcd_line(m, 12, 84, VG_SMALL, C_INK, VG_LEFT);
    m[0] = 0; scat(m, "SAMPLES USE ", 64); secs(t, used_nib() / 3u, 26040, 1); scat(m, t, 64); scat(m, " S OF ", 64);
    scat(m, BUDGET_N[budget_i], 64); scat(m, " (12 BIT, 26 kHz)", 64);
    lcd_line(m, 12, 108, VG_SMALL, C_INK, VG_LEFT);
    lcd_line("16 BIT COSTS A QUARTER MORE TIME", 12, 126, VG_SMALL, C_DIM, VG_LEFT);
    lcd_line("ZP12 3.0 \xC2\xB7 GPL-3.0 \xC2\xB7 BUILT ON SLOOP (ISOD89)", 12, 160, VG_SMALL, C_DIM, VG_LEFT);
    lcd_line("AND FELUCCA (H\xC3\x9CGELTON INSTRUMENTS)", 12, 176, VG_SMALL, C_DIM, VG_LEFT);
}
static void draw_browser(void)
{
    if (br_wait) br_load();
    if (br_sel < br_top) br_top = br_sel;
    if (br_sel >= br_top + 8) br_top = br_sel - 7;
    if (!br_n || (br_n == 1 && br_dir[0])) lcd_line(br_wait ? "READING \xE2\x80\xA6" : "NO WAV FILES HERE", 160, 120, VG_MED, C_DIM, VG_CENTER);
    for (int i = 0; i < 8 && br_top + i < br_n; i++) {
        int j = br_top + i, y = 54 + i * 18;
        if (j == br_sel) vg_rect(10, y - 1, 298, 18, C_INK);
        char m[80] = "";
        if (br_kind[j] == 'd') scat(m, "/ ", 80);
        scat(m, br_name[j], 80);
        vg_text(m, 16, y, VG_MED, j == br_sel ? C_LCD : C_INK, VG_LEFT);
    }
    lcd_line("DATA CHOOSE \xC2\xB7 ENTER LOAD \xC2\xB7 RETURN BACK", 12, 200, VG_SMALL, C_DIM, VG_LEFT);
}
static void draw_pads(void)
{
    uint32_t now = now_ms();
    int steps = page == PG_SEQ && sub[PG_SEQ] == 1 && !setup_on && !br_on;
    uint8_t on[16];
    if (steps) steps_of(sel, (uint32_t)stp_bar, on);
    uint32_t ph = sq.pos >> 16;
    int cur = steps && sq.playing && sq.countin <= 0 && ph / SQ_BAR == (uint32_t)stp_bar ? (int)(ph % SQ_BAR / 24u) : -1;
    for (int id = 0; id < 16; id++) {
        int k = bk * 16 + id, x = 316 + (id % 4) * 41, y = 44 + (id / 4) * 45, c = k % 8;
        int lit = now - flash_t[k] < 140u;
        uint32_t bg = steps ? (on[id] ? C_RED : id == cur ? 0x3A3F55 : 0x16181E) : lit ? vg_mix(0x16181E, CH_RGB[c], 0.6f) : 0x16181E;
        vg_round(x, y, 39, 43, 4, bg);
        hit_at(x, y, 39, 43, HK_PAD, id);
        if (steps) {
            char t[4]; vg_itoa(id + 1, t);
            vg_text(t, x + 20, y + 13, VG_MED, on[id] ? C_LT : C_MUT, VG_CENTER);
            vibe_led_pad(id, on[id] ? 0xFF2820 : id == cur ? 0x606080 : 0x101010, on[id] ? 1.0f : id == cur ? 0.6f : 0.05f);
            continue;
        }
        int heard = sp_solo ? (sp_solo >> c & 1) : !(sp_mute >> c & 1);
        vg_rect(x + 4, y + 3, 31, 3, heard ? CH_RGB[c] : 0x40404A);
        char a[3], nm[6];
        pad_label(a, k);
        vg_text(a, x + 4, y + 7, VG_SMALL, k == sel ? C_LT : C_MUT, VG_LEFT);
        scopy(nm, wave_name(sp_sound[k].wave), 6);
        for (int j = 4; j > 2 && vg_text_width(nm, VG_SMALL) > 40; j--) nm[j] = 0;   /* (fits the tile) */
        vg_text(nm, x + 20, y + 24, VG_SMALL, heard ? C_LT : C_MUT, VG_CENTER);
        if (k == sel) vg_frame(x, y, 39, 43, 2, C_RED);
        vibe_led_pad(id, CH_RGB[c], lit ? 1.0f : k == sel ? 0.35f : heard ? 0.12f : 0.02f);
    }
}
static void title_of(char *m, int n)               /* row 2 where a page has no sub-pages: what it is about */
{
    m[0] = 0;
    if (br_on) { scopy(m, br_dir[0] ? br_dir : "LOAD A WAV ONTO THE PAD \xC2\xB7 FROM WHERE?", n); return; }
    if (setup_on) { scopy(m, "SETUP \xC2\xB7 F6 OR RETURN CLOSES", n); return; }
    if (page == PG_SONG) { scat(m, "SONG ", n); icat(m, sq.song_sel + 1, n); scat(m, sq.song_mode ? " \xC2\xB7 ON: PLAY PLAYS IT" : " \xC2\xB7 OFF (F3)", n); return; }
    wave_info(m, sel, n);
}
static void draw_header(void)                     /* two rows: ZP12, the pages, loop / tempo / transport · the page's sub-pages as chips */
{
    char m[96], t[16];
    vg_rect(0, 0, VIBE_SCREEN_W, 42, C_PANEL);
    vg_text("ZP12", 8, 1, VG_BOLD, C_RED, VG_LEFT);
    for (int i = 0; i < PG_N; i++) {
        int x = 52 + i * 52, on = i == page && !setup_on;
        if (on) vg_round(x, 1, 50, 17, 4, C_RED);
        vg_text(PG_NAME[i], x + 25, 2, VG_BOLD, on ? C_LT : C_MUT, VG_CENTER);
        hit_at(x, 0, 50, 19, HK_TAB, i);
    }
    m[0] = 0; scat(m, "L", 32); icat2(m, sq.seg % SQ_NSEG + 1, 32); scat(m, "  ", 32);
    scat(m, vg_ftoa(sq.bpm10 / 10.0f, 1, t), 32);
    vg_text(m, 452, 2, VG_BOLD, C_LT, VG_RIGHT);
    if (sq.recording || sq.rec_arm || sq.dub_wait) vg_circle(466, 10, 6, C_RED);
    else if (sq.playing) vg_tri(461, 3, 461, 17, 473, 10, 0x5AD45A);
    else vg_rect(461, 4, 12, 12, C_MUT);
    int n = br_on || setup_on ? 1 : PG_SUBS[page];
    if (n < 2) {                                  /* (one page: its title in the chips' row) */
        title_of(m, sizeof m);
        vg_text(m, 12, 22, VG_MED, C_LT, VG_LEFT);
        return;
    }
    int x = 12, gap = 5, w = (456 - gap * (n - 1)) / n;   /* the pages over the whole width (the same K / TAP = the next) */
    for (int k = 0; k < n; k++) {
        int on = k == sub[page] % n;
        vg_round(x, 21, w, 19, 5, on ? C_RED : C_KEY);
        vg_text(SUB_NAME[page][k], x + w / 2, 21, VG_MED, on ? C_LT : 0xD8DCE6, VG_CENTER);
        hit_at(x - 2, 19, w + gap, 23, HK_CHIP, k);
        x += w + gap;
    }
}
static void draw_encoders(void)
{
    char v[24];
    vg_rect(0, 224, VIBE_SCREEN_W, 58, C_BG);
    for (int i = 0; i < 4; i++) {
        int id = ids_now(i), cx = 60 + 120 * i;
        if (id == P_NONE) continue;
        int ok = pfmt(id, v);
        vg_text(PNAME[id], cx, 228, VG_SMALL, C_MUT, VG_CENTER);
        vg_text(v, cx, 247, VG_BIG, ok ? C_LT : 0x50586E, VG_CENTER);
        vibe_link_encoder(VE_1 + i, PNAME[id], v);
    }
}
static void draw_softkeys(void)
{
    const char *f[6] = { "", "", "", "", "SAVE", setup_on ? "CLOSE" : "SETUP" };
    uint32_t lit = (uint32_t)setup_on << 5;
    if (setup_on) { f[0] = "SMOOTH"; lit |= (uint32_t)(sp_smooth != 0); }
    else switch (page) {
    case PG_SAMPLE:
        f[0] = rs == RS_OFF ? "REC" : "STOP"; f[1] = rec_rate_i ? "27.5 kHz" : "26 kHz"; f[2] = rec_16 ? "16 BIT" : "12 BIT"; f[3] = "LOAD";
        lit |= (uint32_t)(rs != RS_OFF) | (uint32_t)br_on << 3; break;
    case PG_EDIT: {
        const sp_wave_t *w = sp_sound[sel].wave < SP_NWAVE ? &sp_wave[sp_sound[sel].wave] : 0;
        f[0] = w && w->fmt == SPW_16 ? "16 BIT" : "12 BIT"; f[1] = copy_from >= 0 ? "COPY TO" : "COPY"; f[2] = asking(2) ? "SURE?" : "DELETE";
        lit |= (uint32_t)(copy_from >= 0) << 1 | (uint32_t)asking(2) << 2; break;
    }
    case PG_MIX: f[0] = "MUTE"; f[1] = "SOLO"; f[2] = "CLEAR"; lit |= mix_hold ? 1u << (mix_hold - 1) : 0u; break;
    case PG_SEQ: f[0] = asking(10) ? "SURE?" : "CLEAR"; f[1] = "ERASE"; f[2] = "COPY"; f[3] = "UNDO"; lit |= (uint32_t)asking(10) | (uint32_t)erase_held << 1; break;
    case PG_SONG: f[0] = "INSERT"; f[1] = "DELETE"; f[2] = sq.song_mode ? "SONG ON" : "SONG OFF"; lit |= (uint32_t)sq.song_mode << 2; break;
    }
    vg_rect(0, 282, VIBE_SCREEN_W, 38, 0x0E1220);
    for (int i = 0; i < 6; i++) {
        int x = 40 + i * 80, on = lit >> i & 1u;
        vg_round(x - 37, 286, 74, 30, 5, on ? C_RED : C_KEY);
        vg_text(f[i], x, 293, VG_MED, on ? C_LT : 0xD8DCE6, VG_CENTER);
        vibe_label(VB_F1 + i, f[i]);
    }
    for (int i = 0; i < PG_N; i++) vibe_label(VB_K1 + i, PG_NAME[i]);
}
static const char *const HELP_ALL[] = {
    "K1 - K6", "SAMPLE EDIT MIX FX SEQ SONG \xC2\xB7 again: next page",
    "SHIFT + K1 - K6", "play pads 1-6 (SHIFT held or tapped)",
    "ENCODERS PRESSED", "play the row of the pad DATA chose",
    "PADS", "the sounds: banks A + B (MENU: C + D)",
    "KEYS", "the selected sound, chromatic (OCT - / +)",
    "ENCODER 1 - 4", "the four values on screen (SHIFT: START / END fine)",
    "A \xC2\xB7 B \xC2\xB7 DATA", "tempo (SHIFT: 0.1) \xC2\xB7 loop \xC2\xB7 the sound",
    "PLAY \xC2\xB7 STOP", "the loop (or the song: SONG F3) \xC2\xB7 stop",
    "REC", "on SAMPLE: sample onto the pad \xC2\xB7 else: record hits",
    "F5 \xC2\xB7 F6", "save now (it saves by itself) \xC2\xB7 setup",
    "ENTER", "hear the selected sound",
    "\xE2\x97\x80 \xE2\x96\xB6", "the sound \xC2\xB7 on STEPS the bar, on SONG the step" };
static const char *const HELP_PAGE[PG_N][6] = {
    { "SAMPLE", "SOURCE GAIN THRESH MONITOR \xC2\xB7 REC or F1: take it", "F2 \xC2\xB7 F3", "26 / 27.5 kHz \xC2\xB7 12 / 16 bit of the next take", "F4 LOAD", "a WAV from the card or a stick onto the pad" },
    { "EDIT", "SOUND \xC2\xB7 TRUNC (START END) \xC2\xB7 OUT \xC2\xB7 SENDS", "F1 \xC2\xB7 F2", "12 / 16 bit \xC2\xB7 copy the sound to a pad", "F3", "delete the own sample (twice)" },
    { "MIX", "faders 1-4 \xC2\xB7 5-8 \xC2\xB7 SENDS: what each pad sends to CHORUS DELAY REVERB", "F1 MUTE \xC2\xB7 F2 SOLO", "tapped: the selected pad's channel \xC2\xB7 held + pads", "F3", "everything heard again" },
    { "FX", "CHORUS \xC2\xB7 DELAY \xC2\xB7 REVERB: each pad sends (MIX > SENDS)", "FILTER", "a DJ filter on the whole mix (not kept)", "", "" },
    { "SEQ", "LOOP: LOOP BARS QUANTIZE SWING \xC2\xB7 STEPS: pads = steps", "F1 \xC2\xB7 F2", "clear (twice) \xC2\xB7 held + pads: erase their hits", "F3 \xC2\xB7 F4", "copy to an empty loop \xC2\xB7 undo" },
    { "SONG", "SONG STEP LOOP REPEAT \xC2\xB7 LOOP on END adds a step", "F1 \xC2\xB7 F2", "insert \xC2\xB7 delete the step", "F3", "song on: PLAY plays the song" } };

static void ui_draw(uint16_t *fb)
{
    vg_begin(fb);
    vg_clear(C_BG);
    if (help) {
        static const char *rows[2 * 14];
        int n = 0;
        for (int i = 0; i < 6 && HELP_PAGE[page][i][0]; i += 2) { rows[2 * n] = HELP_PAGE[page][i]; rows[2 * n + 1] = HELP_PAGE[page][i + 1]; n++; }
        for (int i = 0; i < (int)(sizeof HELP_ALL / sizeof HELP_ALL[0]) && n < 14; i += 2) { rows[2 * n] = HELP_ALL[i]; rows[2 * n + 1] = HELP_ALL[i + 1]; n++; }
        vg_controls(PG_NAME[page], rows, n);
        return;
    }
    nhits = 0;
    draw_header();
    vg_round(6, 44, 306, 178, 5, C_LCD);
    if (br_on) draw_browser();
    else if (setup_on) draw_setup();
    else switch (page) {
        case PG_SAMPLE: draw_sample(); break;
        case PG_EDIT: draw_edit(); break;
        case PG_MIX: draw_mix(); break;
        case PG_FX: draw_fx(); break;
        case PG_SEQ: draw_seq(); break;
        case PG_SONG: draw_song(); break;
    }
    if (msg[0] && now_ms() - msg_t < 2600u) {
        vg_rect(8, 203, 302, 17, C_INK);
        vg_text(msg, 159, 204, VG_SMALL, C_LCD, VG_CENTER);
    }
    draw_pads();
    draw_encoders();
    draw_softkeys();
    { int r = (sel % 16) / 4; vg_rect(313, 44 + r * 45, 2, 43, 0xF0C83C); }   /* (the row encoders 1-4 play, pressed) */
    if (!shift && shift_tap && now_ms() - shift_tap < 1500u)                  /* SHIFT tapped: a line running out */
        vg_rect(0, 280, (int)(VIBE_SCREEN_W * (1500u - (now_ms() - shift_tap)) / 1500u), 2, 0xF0C83C);
    vibe_link_page(setup_on ? "SETUP" : page == PG_EDIT || page == PG_FX || page == PG_SEQ || page == PG_MIX ? SUB_NAME[page][sub[page] % PG_SUBS[page]] : PG_NAME[page]);
    for (int i = 0; i < PG_N; i++) vibe_led_button(VB_K1 + i, 0xC82E32, i == page ? 1.0f : 0.15f);
    vibe_led_button(VB_PLAY, 0x40FF40, sq.playing ? 1.0f : 0.1f);
    vibe_led_button(VB_REC, 0xFF2020, sq.recording || rs == RS_REC ? 1.0f : sq.rec_arm || rs == RS_ARMED ? 0.5f : 0.1f);
}
