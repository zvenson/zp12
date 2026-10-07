/* SPDX-License-Identifier: GPL-3.0-only */
/* zp12: the screen and the controls, laid out as an 80s sampling drum machine's panel: a light frame, a big
 * two-line LCD (the state in large type, below it what KNOB 1-4 do now and their values), the eight channel
 * faders, the eight pads, the bank and transport LEDs. Held SEQ turns the faders and pads into the step grid.
 * Drawn in regions, each only when what it shows changed. No hardware here: zp12.c reads the panel and lights
 * the LEDs (ui_leds), the host test draws the same screens (tests/zp12_ui_test.c).
 *
 * The knobs are the faders (channels 1-4, SEL held 5-8) unless a page is open; a page closes by itself after
 * a few seconds untouched, or with HOME. */

/* the panel's colours */
#define P_FRAME RGB(206, 208, 201)
#define P_NAVY RGB(38, 62, 112)
#define P_INK RGB(30, 48, 96)                   /* the frame's printing */
#define P_RED RGB(200, 46, 50)
#define P_LCD RGB(178, 190, 146)
#define P_LCDINK RGB(16, 24, 14)
#define P_LCDDIM RGB(84, 98, 70)
#define P_LED RGB(255, 40, 32)
#define P_LEDOFF RGB(70, 20, 22)
#define P_CAP RGB(214, 216, 220)
#define P_SLOT RGB(10, 12, 18)
#define P_PAD RGB(18, 18, 20)
#define P_RULE RGB(120, 140, 180)
#define P_STEP RGB(240, 200, 60)

#define UI_PAGE_MS 6000u                        /* a page untouched this long: back to the faders */

enum { PG_HOME, PG_SOUND, PG_TRUNC, PG_OUT, PG_SFX, PG_CHO, PG_DLY, PG_REV, PG_SEG, PG_SEG2, PG_SONG, PG_SETUP, PG_N };
static const char *const PG_NAME[PG_N] = {"MIX", "SOUND", "TRUNC", "OUT", "SENDS", "CHORUS", "DELAY", "REVERB",
                                          "SEGMENT", "SEG TOOLS", "SONG", "SETUP"};
static const char *const DTIME_NAME[6] = {"1/4", "1/8", "1/16", "1/32", "8T", "16T"};
static const char *const CLICK_NAME[3] = {"OFF", "REC", "ON"};

static struct {
    uint8_t pair;                               /* 0: banks A + B on the keys, 1: C + D */
    uint8_t sel;                                /* the sound the knobs edit, 0..31 */
    uint8_t page;
    uint8_t multi;                              /* MULTI PITCH */
    uint8_t shift;                              /* SEL held: the faders 5-8 */
    uint8_t steps;                              /* SEQ held: the step grid on the keys */
    uint8_t step_bar;                           /* the bar the step grid shows */
    uint8_t song_cur;                           /* SONG page: the step under the knobs */
    uint8_t copy_to;                            /* SEG TOOLS: the target */
    uint8_t arm;                                /* a destructive knob turned once (it wants AGAIN): its page + 1 */
    uint8_t mix[SP_NCH];
    uint32_t arm_ms, touch_ms;
    uint32_t hit_ms[32];                        /* when each pad was last heard (its key and pad light) */
    uint32_t chan_ms[SP_NCH];
    uint32_t tap_ms[4];                         /* TAP: the last taps */
    char msg[24];
    uint32_t msg_until;
    uint32_t sig_lcd, sig_mid, sig_pad, sig_led;   /* what each region shows now */
    uint8_t force, save_req;
} ui;

/* text blended onto bg (gfx.c's cv_text blends onto black) */
static int32_t cv_text_on(int32_t x, int32_t y, const felucca_font_t *f, const char *s, uint16_t c, uint16_t bg)
{
    uint16_t ramp[16];
    uint32_t a;
    int32_t r0 = bg >> 11, g0 = (bg >> 5) & 63, b0 = bg & 31, r1 = c >> 11, g1 = (c >> 5) & 63, b1 = c & 31;
    for (a = 0; a < 16u; a++)
        ramp[a] = (uint16_t)(((r0 + (r1 - r0) * (int32_t)a / 15) << 11) | ((g0 + (g1 - g0) * (int32_t)a / 15) << 5) |
                             (b0 + (b1 - b0) * (int32_t)a / 15));
    for (; *s; s++) {
        uint32_t gi = glyph(f, (uint8_t)*s), gx, gy, w = f->bw[gi], bpr = (w + 1u) / 2u;
        const uint8_t *gd = f->data + f->off[gi];
        for (gy = 0; gy < f->h; gy++)
            for (gx = 0; gx < w; gx++) {
                uint32_t v = gd[gy * bpr + gx / 2u];
                v = (gx & 1u) ? (v & 15u) : (v >> 4);
                if (v)
                    cv_pset(x - f->pad + (int32_t)gx, y + (int32_t)gy, ramp[v]);
            }
        x += f->adv[gi];
    }
    return x;
}

static void put_int(char *b, int32_t v, uint32_t digits, int sign)
{
    uint32_t u = (uint32_t)(v < 0 ? -v : v), i;
    if (sign)
        *b++ = v < 0 ? '-' : '+';
    for (i = digits; i; i--) {
        b[i - 1u] = (char)('0' + u % 10u);
        u /= 10u;
    }
    b[digits] = 0;
}
static char *cat(char *d, const char *s) { while (*s) *d++ = *s++; *d = 0; return d; }
static char *num(char *p, int32_t v, uint32_t digits, int sign) { put_int(p, v, digits, sign); return p + digits + (sign != 0); }

static const char *sound_name(uint32_t k)
{
    const sp_sound_t *s = &sp_sound[k];
    return s->wave < KIT_NWAVE ? KIT_WAVE[s->wave].name : s->wave == 0xFFu ? "-----" : "USER";
}
static void pad_label(char *b, uint32_t k) { b[0] = (char)('A' + k / 8u); b[1] = (char)('1' + k % 8u); b[2] = 0; }

/* the four columns of the page: label and value of KNOB 1-4 ("" = nothing on that knob) */
static void page_cols(char lab[4][8], char val[4][8])
{
    const sp_sound_t *s = &sp_sound[ui.sel];
    uint32_t i;
    for (i = 0; i < 4u; i++) lab[i][0] = val[i][0] = 0;
#define COL(i, l, ...) do { cat(lab[i], l); __VA_ARGS__; } while (0)
    switch (ui.page) {
    case PG_HOME:
        for (i = 0; i < 4u; i++) {
            uint32_t c = (ui.shift ? 4u : 0u) + i;
            lab[i][0] = 'C'; lab[i][1] = 'H'; lab[i][2] = (char)('1' + c); lab[i][3] = 0;
            num(val[i], ui.mix[c], 3, 0);
        }
        break;
    case PG_SOUND:
        COL(0, "TUNE", num(val[0], s->tune, 2, 1)); COL(1, "FINE", num(val[1], s->fine, 2, 1));
        COL(2, "DECAY", num(val[2], s->decay, 3, 0)); COL(3, "LEVEL", num(val[3], s->level, 3, 0));
        break;
    case PG_TRUNC:
        COL(0, "START", num(val[0], s->start / 10, 3, 0)); COL(1, "END", num(val[1], s->end / 10, 3, 0));
        COL(2, "DIR", cat(val[2], (s->flags & SPF_REVERSE) ? "REV" : "FWD"));
        COL(3, "SPEED", cat(val[3], (s->flags & SPF_33) ? "33" : "45"));
        break;
    case PG_OUT:
        COL(0, "CHAN", num(val[0], s->chan + 1, 1, 0)); COL(1, "PAN", num(val[1], s->pan, 2, 1));
        if (s->chan < 2u) { COL(2, "CUT", num(val[2], s->cut, 3, 0)); COL(3, "RESO", num(val[3], s->reso, 3, 0)); }
        else { COL(2, "FILT", cat(val[2], s->chan < 6u ? "FIX" : "OFF")); }
        break;
    case PG_SFX:
        COL(0, "DRIVE", num(val[0], s->drive, 3, 0)); COL(1, "CHO", num(val[1], s->send[0], 3, 0));
        COL(2, "DLY", num(val[2], s->send[1], 3, 0)); COL(3, "REV", num(val[3], s->send[2], 3, 0));
        break;
    case PG_CHO:
        COL(0, "RATE", num(val[0], fxp.crate, 3, 0)); COL(1, "DEPTH", num(val[1], fxp.cdepth, 3, 0));
        COL(2, "MIX", num(val[2], fxp.cmix, 3, 0));
        break;
    case PG_DLY:
        COL(0, "TIME", cat(val[0], DTIME_NAME[fxp.dtime % 6])); COL(1, "FDBK", num(val[1], fxp.fdbk, 3, 0));
        COL(2, "COLOR", num(val[2], fxp.colr, 3, 0)); COL(3, "MIX", num(val[3], fxp.dmix, 3, 0));
        break;
    case PG_REV:
        COL(0, "SIZE", num(val[0], fxp.size, 3, 0)); COL(1, "DAMP", num(val[1], fxp.damp, 3, 0));
        COL(2, "PRE", num(val[2], fxp.pre, 2, 0));
        break;
    case PG_SEG: {
        const sq_seg_t *g = &sq_seg[sq.seg % SQ_NSEG];
        COL(0, "SEG", num(val[0], sq.seg + 1, 2, 0));
        COL(1, "BARS", g->bars ? (void)num(val[1], g->bars, 2, 0) : (void)cat(val[1], "AUTO"));
        COL(2, "QUANT", cat(val[2], SQ_GRID_NAME[sq.quant % 7u]));
        COL(3, "SWING", num(val[3], SQ_SWING[sq.swing % 6u], 2, 0));
        break;
    }
    case PG_SEG2:
        COL(0, "CLEAR", cat(val[0], ui.arm == PG_SEG2 + 1u ? "AGAIN" : "-->"));
        COL(1, "COPY>", num(val[1], ui.copy_to + 1, 2, 0));
        COL(2, "COPY", cat(val[2], ui.arm == PG_SEG2 + 2u ? "AGAIN" : "-->"));
        break;
    case PG_SONG:
        COL(0, "STEP", num(val[0], ui.song_cur + 1, 2, 0));
        if (ui.song_cur < sq.song_n) {
            COL(1, "SEG", num(val[1], sq_song[ui.song_cur].seg + 1, 2, 0));
            COL(2, "REPEAT", num(val[2], sq_song[ui.song_cur].rep, 2, 0));
        } else {
            COL(1, "SEG", cat(val[1], "END"));
        }
        COL(3, "MODE", cat(val[3], sq.song_mode ? "SONG" : "SEG"));
        break;
    case PG_SETUP:
        COL(0, "TEMPO", num(val[0], (int32_t)(sq.bpm10 / 10u), 3, 0)); COL(1, "CLICK", cat(val[1], CLICK_NAME[sq.click % 3u]));
        break;
    default:
        break;
    }
#undef COL
}

/* ---- the regions */
static void draw_frame(void)                    /* once: the light frame, the header */
{
    cv_begin(240, 20, P_FRAME);
    cv_text_on(6, 2, &FONT_S, "zp12", P_INK, P_FRAME);
    cv_text_on(7, 2, &FONT_S, "zp12", P_INK, P_FRAME);   /* (bold: twice, a pixel apart) */
    cv_text_on(48, 2, &FONT_S, "sampling drums", P_INK, P_FRAME);
    cv_rect(170, 9, 66, 1, P_RED);
    cv_rect(170, 11, 66, 1, P_INK);
    cv_blit(0, 0);
    lcd_fill(0, 20, 4, 220, P_FRAME);
    lcd_fill(236, 20, 4, 220, P_FRAME);
    lcd_fill(4, 98, 232, 2, P_FRAME);
}

static void draw_lcd(void)                      /* the LCD: big the state, below it the knobs' four columns */
{
    char big[24], small[24], lab[4][8], val[4][8], *p = big;
    uint32_t i, bpm = sq.bpm10;
    cv_begin(232, 78, P_FRAME);
    cv_rect(0, 0, 232, 78, RGB(70, 76, 66));    /* the bezel */
    cv_rect(3, 3, 226, 72, P_LCD);
    small[0] = 0;
    if (ui.msg[0] && (int32_t)(fm1_ms - ui.msg_until) < 0) {
        cat(big, ui.msg);
    } else if (ui.steps) {                      /* the step grid: the pad, the bar of the keys */
        pad_label(p, ui.sel);
        p = cat(p + 2, " BAR ");
        p = num(p, ui.step_bar + 1, 1, 0);
        p = cat(p, "/");
        num(p, sq_seg[sq.seg % SQ_NSEG].bars ? sq_seg[sq.seg % SQ_NSEG].bars : 1, 1, 0);
    } else if (sq.playing) {                    /* SEG01 2.3 */
        uint32_t t = sq.countin > 0 ? 0u : sq.pos >> 16;
        if (sq.song_mode && sq.song_n) { p = cat(p, "S"); p = num(p, sq.song_i + 1, 2, 0); p = cat(p, ":"); }
        else p = cat(p, "SEG");
        p = num(p, sq.seg + 1, 2, 0);
        p = cat(p, " ");
        p = num(p, (int32_t)(t / SQ_BAR + 1u), t / SQ_BAR + 1u >= 10u ? 2u : 1u, 0);
        p = cat(p, ".");
        num(p, (int32_t)(t % SQ_BAR / SQ_PPQ + 1u), 1, 0);
    } else {
        pad_label(p, ui.sel);
        p = cat(p + 2, " ");
        cat(p, sound_name(ui.sel));
    }
    p = small;                                  /* top right, small: the tempo, REC / count-in */
    if (sq.playing && sq.countin > 0) p = cat(p, "COUNT ");
    else if (sq.recording) p = cat(p, "REC ");
    else if (sq.rec_arm) p = cat(p, "ARMED ");
    p = num(p, (int32_t)(bpm / 10u), 3, 0);
    cv_text_on(10, 6, &FONT_L, big, P_LCDINK, P_LCD);
    cv_text_on(222 - text_w(&FONT_S, small), 4, &FONT_S, small, P_LCDDIM, P_LCD);
    cv_text_on(222 - text_w(&FONT_S, ui.page == PG_HOME ? "BPM" : PG_NAME[ui.page]), 20, &FONT_S,
               ui.page == PG_HOME ? "BPM" : PG_NAME[ui.page], P_LCDDIM, P_LCD);
    cv_rect(6, 40, 220, 1, P_LCDDIM);
    page_cols(lab, val);
    for (i = 0; i < 4u; i++) {
        int32_t x = 8 + (int32_t)i * 56;
        cv_text_on(x, 42, &FONT_S, lab[i], P_LCDDIM, P_LCD);
        cv_text_on(x, 57, &FONT_S, val[i], P_LCDINK, P_LCD);
        cv_text_on(x + 1, 57, &FONT_S, val[i], P_LCDINK, P_LCD);   /* (bold) */
    }
    cv_blit(4, 20);
}

static uint32_t fader_y(uint32_t lv) { return 44u - lv * 38u / 127u; }   /* the cap's top, in the region */

static void draw_faders(void)                   /* the eight channel levels; a cap lights while it sounds */
{
    uint32_t i, k;
    cv_begin(232, 62, RGB(38, 62, 112));
    for (k = 0; k < 7u; k++)                    /* the scale lines behind the faders */
        cv_rect(12, 6 + (int32_t)k * 7, 208, 1, P_RULE);
    for (i = 0; i < SP_NCH; i++) {
        int32_t cx = 14 + (int32_t)i * 29, y = (int32_t)fader_y(ui.mix[i]);
        int on = (int32_t)(fm1_ms - ui.chan_ms[i]) < 90 && sp_ch[i].on;
        int knobbed = ui.page == PG_HOME && (i >= 4u) == (ui.shift != 0);
        char n[2] = {(char)('1' + i), 0};
        cv_rect(cx - 1, 3, 3, 46, P_SLOT);
        cv_rect(cx - 9, y, 19, 7, on ? P_LED : P_CAP);
        cv_rect(cx - 9, y + 3, 19, 1, on ? RGB(120, 10, 10) : RGB(90, 90, 96));
        cv_text_on(cx - 4, 47, &FONT_S, n, knobbed ? C_WHITE : P_RULE, RGB(38, 62, 112));
    }
    cv_blit(4, 100);
}

static void draw_pads(void)                     /* the eight pads of the bank the last hit was in, their labels */
{
    uint32_t i, bank = ui.sel / 8u;
    cv_begin(232, 46, P_NAVY);
    for (i = 0; i < 8u; i++) {
        int32_t cx = 14 + (int32_t)i * 29;
        uint32_t k = bank * 8u + i;
        int lit = (int32_t)(fm1_ms - ui.hit_ms[k]) < 110;
        char l[3];
        cv_rect(cx - 11, 2, 23, 23, RGB(6, 6, 8));
        cv_rect(cx - 10, 3, 21, 21, lit ? P_LED : P_PAD);
        cv_rect(cx - 8, 5, 17, 2, lit ? RGB(255, 140, 120) : RGB(48, 48, 52));
        pad_label(l, k);
        cv_text_on(cx - 8, 28, &FONT_S, l, k == ui.sel ? C_WHITE : P_FRAME, P_NAVY);
    }
    cv_blit(4, 162);
}

/* SEQ held: the step grid in place of the faders and pads: the eight pads of the bank, 16 steps of the bar,
 * the pad on the keys bright, the playhead's column marked */
static void draw_grid(void)
{
    const sq_seg_t *g = &sq_seg[sq.seg % SQ_NSEG];
    uint32_t bank = ui.sel / 8u, r, i, t0 = ui.step_bar * SQ_BAR, ph = 0xFFFFu;
    uint32_t len = g->bars ? g->bars * SQ_BAR : SQ_BAR;
    cv_begin(232, 108, P_NAVY);
    if (sq.playing && sq.countin <= 0) {
        uint32_t t = (sq.pos >> 16) % len;
        if (t >= t0 && t < t0 + SQ_BAR) ph = (t - t0) / 24u;
    }
    for (r = 0; r < 8u; r++) {
        uint32_t k = bank * 8u + r, row = 0;
        int32_t y = 4 + (int32_t)r * 13;
        char l[3];
        for (i = 0; i < g->n; i++)
            if (g->ev[i].pad == k && g->ev[i].t >= t0 && g->ev[i].t < t0 + SQ_BAR)
                row |= 1u << ((g->ev[i].t - t0) / 24u);
        pad_label(l, k);
        cv_text_on(2, y - 3, &FONT_S, l, k == ui.sel ? C_WHITE : P_RULE, P_NAVY);
        for (i = 0; i < 16u; i++) {
            int32_t x = 24 + (int32_t)i * 12 + (int32_t)(i / 4u) * 2;
            uint16_t c = (row >> i) & 1u ? (k == ui.sel ? P_STEP : P_FRAME) : (k == ui.sel ? RGB(70, 90, 140) : RGB(52, 74, 124));
            cv_rect(x, y, 10, 10, c);
            if (i == ph)
                cv_rect(x, y + 10, 10, 2, P_LED);
        }
    }
    cv_blit(4, 100);
}

static void led(int32_t x, int32_t y, int on)
{
    cv_rect(x, y, 6, 6, on ? P_LED : P_LEDOFF);
    cv_rect(x + 1, y, 4, 1, on ? RGB(255, 170, 160) : P_LEDOFF);
}

static void draw_leds(void)                     /* banks A-D (the pair on the keys), MULTI, SONG, RUN, REC */
{
    uint32_t i;
    int blink = (sq.beat & 1u) == 0u;
    cv_begin(232, 22, P_NAVY);
    for (i = 0; i < 4u; i++) {
        char b[2] = {(char)('A' + i), 0};
        led(4 + (int32_t)i * 24, 8, i / 2u == ui.pair);
        cv_text_on(12 + (int32_t)i * 24, 3, &FONT_S, b, P_FRAME, P_NAVY);
    }
    led(100, 8, ui.multi);
    cv_text_on(108, 3, &FONT_S, "M", P_FRAME, P_NAVY);
    led(124, 8, sq.song_mode);
    cv_text_on(132, 3, &FONT_S, "S", P_FRAME, P_NAVY);
    led(150, 8, sq.playing && (blink || sq.countin > 0));
    cv_text_on(158, 3, &FONT_S, "RUN", P_FRAME, P_NAVY);
    led(190, 8, sq.recording || (sq.rec_arm && blink));
    cv_text_on(198, 3, &FONT_S, "REC", P_FRAME, P_NAVY);
    cv_blit(4, 214);
}

static uint32_t sig_of(const void *p, uint32_t n, uint32_t h)
{
    const uint8_t *b = p;
    while (n--) h = (h ^ *b++) * 16777619u;
    return h;
}

static void ui_note_played(void)                /* the pads the sequencer or the keys played: their lights */
{
    uint32_t m = sq.played, k;
    sq.played = 0;
    for (k = 0; m; k++, m >>= 1)
        if (m & 1u) {
            ui.hit_ms[k] = fm1_ms;
            ui.chan_ms[sp_sound[k].chan % SP_NCH] = fm1_ms;
        }
}

static void ui_draw(void)
{
    uint32_t i, s, lit = 0, on = 0, bank = ui.sel / 8u;
    ui_note_played();
    if (ui.page != PG_HOME && (uint32_t)(fm1_ms - ui.touch_ms) > UI_PAGE_MS && !ui.steps)
        ui.page = PG_HOME;                      /* untouched: the knobs are the faders again */
    if (ui.arm && (uint32_t)(fm1_ms - ui.arm_ms) > 1500u)
        ui.arm = 0;
    if (ui.force) {
        draw_frame();
        ui.sig_lcd = ui.sig_mid = ui.sig_pad = ui.sig_led = 0;
    }
    s = sig_of(&sp_sound[ui.sel], sizeof(sp_sound_t), 2166136261u ^ ui.sel * 7u ^ ui.page * 131u ^ sq.bpm10 * 7919u ^ ui.multi);
    s = sig_of(ui.mix, sizeof ui.mix, s ^ ui.shift ^ ui.steps * 3u ^ ui.step_bar * 29u ^ ui.arm * 37u ^ ui.copy_to * 41u);
    s = sig_of(&fxp, sizeof fxp, s);
    s = sig_of(sq_song, sizeof sq_song, s ^ ui.song_cur * 31u ^ sq.song_n * 17u ^ sq.song_mode);
    s ^= (sq.seg * 977u) ^ (sq.quant * 31u) ^ (sq.swing * 7u) ^ (sq.click * 3u) ^ sq_seg[sq.seg % SQ_NSEG].bars * 101u;
    s ^= sq.recording * 5u ^ sq.rec_arm * 11u;
    if (sq.playing)
        s ^= ((sq.countin > 0 ? 0u : (sq.pos >> 16) / SQ_PPQ) + 1u) * 2654435761u ^ (sq.countin > 0) * 9u ^ sq.song_i * 13u;
    if (ui.msg[0] && (int32_t)(fm1_ms - ui.msg_until) < 0)
        s = sig_of(ui.msg, sizeof ui.msg, s ^ 0x55u);
    if (s != ui.sig_lcd || ui.force) { ui.sig_lcd = s; draw_lcd(); }
    if (ui.steps) {
        const sq_seg_t *g = &sq_seg[sq.seg % SQ_NSEG];
        uint32_t len = g->bars ? g->bars * SQ_BAR : SQ_BAR;
        s = 0x9E37u ^ sq.gen * 2654435761u ^ ui.sel * 131u ^ ui.step_bar * 7u;
        if (sq.playing) s ^= (((sq.pos >> 16) % len) / 24u + 1u) * 40503u;
        if (s != ui.sig_mid || ui.force) { ui.sig_mid = s; ui.sig_pad = 0; draw_grid(); }
    } else {
        for (i = 0; i < SP_NCH; i++)
            on |= (uint32_t)((int32_t)(fm1_ms - ui.chan_ms[i]) < 90 && sp_ch[i].on) << i;
        s = sig_of(ui.mix, sizeof ui.mix, on * 2654435761u ^ ui.shift * 3u ^ (ui.page == PG_HOME) * 5u) | 1u;
        if (s != ui.sig_mid || ui.force) { ui.sig_mid = s; draw_faders(); }
        for (i = 0; i < 8u; i++)
            lit |= (uint32_t)((int32_t)(fm1_ms - ui.hit_ms[bank * 8u + i]) < 110) << i;
        s = lit * 31u + ui.sel * 7919u + 1u;
        if (s != ui.sig_pad || ui.force) { ui.sig_pad = s; draw_pads(); }
    }
    s = ui.pair + ui.multi * 2u + sq.playing * 4u + sq.recording * 8u + sq.rec_arm * 16u + sq.song_mode * 32u +
        (sq.beat & 1u) * 64u + (sq.countin > 0) * 128u + 1u;
    if (s != ui.sig_led || ui.force) { ui.sig_led = s; draw_leds(); }
    ui.force = 0;
}

static void ui_say(const char *m)
{
    uint32_t i;
    for (i = 0; m[i] && i + 1u < sizeof ui.msg; i++) ui.msg[i] = m[i];
    ui.msg[i] = 0;
    ui.msg_until = fm1_ms + 1100u;
}

/* ---- playing. SP_HIT starts a sound: on the FM-1 a request to the audio ISR, on the host at once */
#ifndef SP_HIT
#define SP_HIT(k, vel, semis) sq_hit(k, vel, semis)
#endif
static void pad_hit_at(uint32_t k, uint32_t vel, int32_t semis)
{
    k %= SP_NSOUND;
    ui.sel = (uint8_t)k;
    ui.hit_ms[k] = fm1_ms;
    ui.chan_ms[sp_sound[k].chan % SP_NCH] = fm1_ms;
    SP_HIT(k, vel, semis);
}
static void pad_hit(uint32_t k, uint32_t vel) { pad_hit_at(k, vel, 0); }

/* the white key index of key k (0 = F3 .. 26 = G5), 0xFF for a black key */
static uint32_t white_of(uint32_t k)
{
    static const uint8_t WHITE_OF[27] = {0, 0xFF, 1, 0xFF, 2, 0xFF, 3, 4, 0xFF, 5, 0xFF, 6, 7, 0xFF, 8, 0xFF, 9,
                                         10, 0xFF, 11, 0xFF, 12, 0xFF, 13, 14, 0xFF, 15};   /* F3 .. G5 */
    return k < 27u ? WHITE_OF[k] : 0xFFu;
}
static uint32_t key_pad(uint32_t k)            /* white keys 1-8: bank A (C), 9-16: B (D) */
{
    uint32_t w = white_of(k);
    return w != 0xFFu ? ui.pair * 16u + w : 0xFFu;
}

/* key k down; erase: LFO held (the pad's hits go: as the playhead passes, or all at once when stopped) */
static void key_down(uint32_t k, int erase)
{
    uint32_t pad;
    if (k >= 27u)
        return;
    if (ui.steps) {                              /* SEQ held: a white key sets / clears the selected pad's step */
        uint32_t w = white_of(k);
        if (w != 0xFFu) {
            sq_post(RQ_STEP, ui.sel, ui.step_bar * SQ_BAR + w * 24u);
            if (!sq_seg[sq.seg % SQ_NSEG].bars)
                sq_seg[sq.seg % SQ_NSEG].bars = 1;   /* (a step written makes an AUTO segment a bar long) */
        }
        return;
    }
    if (ui.multi && !erase) {                    /* MULTI PITCH: the sound, F4 (key 12) as written */
        pad_hit_at(ui.sel, 110, (int32_t)k - 12);
        return;
    }
    pad = ui.multi ? ui.sel : key_pad(k);
    if (pad == 0xFFu)
        return;
    if (erase) {
        ui.sel = (uint8_t)pad;
        if (!sq.playing) {
            sq_post(RQ_WIPE, pad, 0);
            ui_say("ERASED");
        }
        return;
    }
    pad_hit(pad, 110);
}

static int again(uint32_t which)                 /* a destructive turn: the first arms (AGAIN), the second acts */
{
    if (ui.arm == which && (uint32_t)(fm1_ms - ui.arm_ms) < 1500u) {
        ui.arm = 0;
        return 1;
    }
    ui.arm = (uint8_t)which;
    ui.arm_ms = fm1_ms;
    return 0;
}

/* KNOB n turned by d */
static void knob(uint32_t n, int32_t d)
{
    sp_sound_t *s = &sp_sound[ui.sel];
    int32_t one = d > 0 ? 1 : -1;
    ui.touch_ms = fm1_ms;
    switch (ui.page) {
    case PG_HOME: {
        uint32_t c = (ui.shift ? 4u : 0u) + n;     /* the faders: channels 1-4, SEL held 5-8 */
        ui.mix[c] = (uint8_t)sp_clamp(ui.mix[c] + d, 0, 127);
        sp_mix[c] = ui.mix[c];
        break;
    }
    case PG_SOUND:
        if (n == 0u) s->tune = (int8_t)sp_clamp(s->tune + one, -24, 12);
        if (n == 1u) s->fine = (int8_t)sp_clamp(s->fine + d, -50, 50);
        if (n == 2u) s->decay = (uint8_t)sp_clamp(s->decay + d, 0, 127);
        if (n == 3u) s->level = (uint8_t)sp_clamp(s->level + d, 0, 127);
        break;
    case PG_TRUNC:
        if (n == 0u) s->start = (uint16_t)sp_clamp(s->start + d * 4, 0, s->end - 10);
        if (n == 1u) s->end = (uint16_t)sp_clamp(s->end + d * 4, s->start + 10, 1000);
        if (n == 2u) s->flags = (uint8_t)(one > 0 ? s->flags | SPF_REVERSE : s->flags & ~SPF_REVERSE);
        if (n == 3u) s->flags = (uint8_t)(one > 0 ? s->flags | SPF_33 : s->flags & ~SPF_33);
        break;
    case PG_OUT:
        if (n == 0u) s->chan = (uint8_t)sp_clamp(s->chan + one, 0, SP_NCH - 1);
        if (n == 1u) s->pan = (int8_t)sp_clamp(s->pan + d, -64, 63);
        if (n == 2u) s->cut = (uint8_t)sp_clamp(s->cut + d, 0, 127);
        if (n == 3u) s->reso = (uint8_t)sp_clamp(s->reso + d, 0, 127);
        break;
    case PG_SFX:
        if (n == 0u) s->drive = (uint8_t)sp_clamp(s->drive + d, 0, 127);
        else s->send[n - 1u] = (uint8_t)sp_clamp(s->send[n - 1u] + d, 0, 127);
        break;
    case PG_CHO:
        if (n == 0u) fxp.crate = (int16_t)sp_clamp(fxp.crate + d, 0, 127);
        if (n == 1u) fxp.cdepth = (int16_t)sp_clamp(fxp.cdepth + d, 0, 127);
        if (n == 2u) fxp.cmix = (int16_t)sp_clamp(fxp.cmix + d, 0, 127);
        break;
    case PG_DLY:
        if (n == 0u) fxp.dtime = (int16_t)sp_clamp(fxp.dtime + one, 0, 5);
        if (n == 1u) fxp.fdbk = (int16_t)sp_clamp(fxp.fdbk + d, 0, 120);
        if (n == 2u) fxp.colr = (int16_t)sp_clamp(fxp.colr + d, 0, 127);
        if (n == 3u) fxp.dmix = (int16_t)sp_clamp(fxp.dmix + d, 0, 127);
        break;
    case PG_REV:
        if (n == 0u) fxp.size = (int16_t)sp_clamp(fxp.size + d, 0, 127);
        if (n == 1u) fxp.damp = (int16_t)sp_clamp(fxp.damp + d, 0, 127);
        if (n == 2u) fxp.pre = (int16_t)sp_clamp(fxp.pre + d, 0, 90);
        break;
    case PG_SEG:
        if (n == 0u && !sq.playing) sq.seg = (uint8_t)sp_clamp(sq.seg + one, 0, SQ_NSEG - 1);
        if (n == 1u) {                              /* BARS: AUTO (0), 1..32 */
            sq_seg_t *g = &sq_seg[sq.seg % SQ_NSEG];
            g->bars = (uint8_t)sp_clamp(g->bars + one, 0, SQ_MAXBARS);
            sq.gen++;
        }
        if (n == 2u) sq.quant = (uint8_t)sp_clamp(sq.quant + one, 0, 6);
        if (n == 3u) sq.swing = (uint8_t)sp_clamp(sq.swing + one, 0, 5);
        break;
    case PG_SEG2:
        if (n == 0u && d > 0 && again(PG_SEG2 + 1u)) { sq_post(RQ_CLEAR, 0, 0); ui_say("SEGMENT CLEARED"); }
        if (n == 1u) ui.copy_to = (uint8_t)sp_clamp(ui.copy_to + one, 0, SQ_NSEG - 1);
        if (n == 2u && d > 0 && again(PG_SEG2 + 2u)) { sq_post(RQ_COPY, 0, ui.copy_to); ui_say("COPIED"); }
        break;
    case PG_SONG:
        if (n == 0u) ui.song_cur = (uint8_t)sp_clamp(ui.song_cur + one, 0, sq.song_n < SQ_NSONG ? sq.song_n : SQ_NSONG - 1);
        if (n == 1u && ui.song_cur < SQ_NSONG && !sq.playing) {
            uint32_t i = ui.song_cur;
            if (i >= sq.song_n) {                     /* past the end: a new step with the segment edited */
                sq_song[i].seg = sq.seg;
                sq_song[i].rep = 1;
                sq.song_n = (uint8_t)(i + 1u);
            } else {
                sq_song[i].seg = (uint8_t)sp_clamp(sq_song[i].seg + one, 0, SQ_NSEG - 1);
            }
        }
        if (n == 2u && ui.song_cur < sq.song_n && !sq.playing) {   /* REPEAT 0: the song ends here */
            int32_t r = sp_clamp(sq_song[ui.song_cur].rep + one, 0, 99);
            if (r == 0) sq.song_n = ui.song_cur;
            else sq_song[ui.song_cur].rep = (uint8_t)r;
        }
        if (n == 3u) sq.song_mode = (uint8_t)(one > 0);
        break;
    case PG_SETUP:
        if (n == 0u) sq.bpm10 = (uint16_t)sp_clamp(sq.bpm10 + d * 5, 400, 2400);
        if (n == 1u) sq.click = (uint8_t)sp_clamp(sq.click + one, 0, 2);
        break;
    default:
        break;
    }
}

/* buttons: the printed labels' matrix ids (as SLOOP's PANEL_DEFAULT); SEL is SLOOP's SCL */
enum { B_OCTDN = 0, B_OCTUP = 1, B_FX = 2, B_SEL = 3, B_ENV = 4, B_LFO = 5, B_EDIT = 6, B_GLO = 7, B_HOME = 8,
       B_SAVE = 9, B_ARP = 10, B_SEQ = 11, B_PLAY = 12, B_REC = 13 };
static void page(uint32_t p) { ui.page = (uint8_t)p; ui.touch_ms = fm1_ms; ui.arm = 0; }

static void tap_tempo(void)                      /* ENV: the tempo of the last taps (two at least, < 2 s apart) */
{
    uint32_t i, n = 0, sum = 0;
    for (i = 3; i; i--) ui.tap_ms[i] = ui.tap_ms[i - 1u];
    ui.tap_ms[0] = fm1_ms;
    for (i = 0; i < 3u && ui.tap_ms[i + 1u] && ui.tap_ms[i] - ui.tap_ms[i + 1u] < 2000u; i++, n++)
        sum += ui.tap_ms[i] - ui.tap_ms[i + 1u];
    if (n && sum)
        sq.bpm10 = (uint16_t)sp_clamp((int32_t)(600000u * n / sum), 400, 2400);
}

/* button b pressed (seq: SEQ was held at the time: OCT pages the step grid's bars) */
static void button(uint32_t b)
{
    switch (b) {
    case B_OCTUP:
        if (ui.steps) {
            const sq_seg_t *g = &sq_seg[sq.seg % SQ_NSEG];
            if (ui.step_bar + 1u < (g->bars ? g->bars : 1u)) ui.step_bar++;
        } else {
            ui.pair = 1;
        }
        break;
    case B_OCTDN:
        if (ui.steps) { if (ui.step_bar) ui.step_bar--; }
        else ui.pair = 0;
        break;
    case B_ARP: ui.multi ^= 1u; break;
    case B_HOME: page(PG_HOME); break;
    case B_EDIT: page(ui.page >= PG_SOUND && ui.page < PG_SFX ? ui.page + 1u : PG_SOUND); break;
    case B_FX: page(ui.page >= PG_CHO && ui.page < PG_REV ? ui.page + 1u : PG_CHO); break;
    case B_SEQ: page(ui.page == PG_SEG ? PG_SEG2 : ui.page == PG_SEG2 ? PG_SONG : PG_SEG); break;
    case B_SAVE: ui.save_req = 1; ui_say("SAVED"); break;
    case B_GLO: page(PG_SETUP); break;
    case B_ENV: tap_tempo(); break;
    case B_PLAY: sq_post(sq.playing ? RQ_STOP : RQ_PLAY, 0, 0); break;
    case B_REC: sq_post(RQ_REC, 0, 0); break;
    default: break;
    }
}

/* what lights on the panel: buttons (bit = matrix id), white and black keys (bit = key 0..26, F3 = 0), the
 * keys' glow (the playhead in the step grid) */
static void ui_leds(uint32_t *btn, uint32_t *keys, uint32_t *glow)
{
    uint32_t b = 0, k = 0, g = 0, i;
    int blink = (sq.beat & 1u) == 0u, fam;
    if (sq.playing) b |= 1u << B_PLAY;
    if (sq.recording || (sq.rec_arm && (fm1_ms / 250u) & 1u)) b |= 1u << B_REC;
    if (ui.multi) b |= 1u << B_ARP;
    if (ui.shift) b |= 1u << B_SEL;
    if (sq.erase) b |= 1u << B_LFO;
    if (sq.playing && blink) b |= 1u << B_ENV;   /* (the beat: TAP's guide) */
    if (sq.song_mode) b |= 1u << B_SAVE;
    fam = ui.page;
    b |= 1u << (fam == PG_HOME ? B_HOME : fam <= PG_SFX ? B_EDIT : fam <= PG_REV ? B_FX : fam <= PG_SONG ? B_SEQ : B_GLO);
    b |= 1u << (ui.pair ? B_OCTUP : B_OCTDN);
    if (ui.steps) {                              /* the selected pad's steps of the bar on the white keys */
        const sq_seg_t *s = &sq_seg[sq.seg % SQ_NSEG];
        uint32_t t0 = ui.step_bar * SQ_BAR, row = 0, len = s->bars ? s->bars * SQ_BAR : SQ_BAR;
        for (i = 0; i < s->n; i++)
            if (s->ev[i].pad == ui.sel && s->ev[i].t >= t0 && s->ev[i].t < t0 + SQ_BAR)
                row |= 1u << ((s->ev[i].t - t0) / 24u);
        for (i = 0; i < 27u; i++) {
            uint32_t w = white_of(i);
            if (w != 0xFFu && (row >> w) & 1u) k |= 1u << i;
            if (w != 0xFFu && sq.playing && sq.countin <= 0 && ((sq.pos >> 16) % len) / 24u == ui.step_bar * 16u + w)
                g |= 1u << i;
        }
        b |= 1u << B_SEQ;
    } else {                                     /* the pads heard just now: their keys flash */
        for (i = 0; i < 27u; i++) {
            uint32_t p = ui.multi ? 0xFFu : key_pad(i);
            if (p < 32u && (int32_t)(fm1_ms - ui.hit_ms[p]) < 90)
                k |= 1u << i;
        }
    }
    *btn = b;
    *keys = k;
    *glow = g;
}

static void ui_init(void)
{
    uint32_t i;
    for (i = 0; i < SP_NCH; i++)
        ui.mix[i] = sp_mix[i];
    ui.force = 1;
}
