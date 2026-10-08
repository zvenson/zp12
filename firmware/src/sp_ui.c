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
static const uint16_t FAM_COL[5] = {RGB(38, 62, 112), RGB(236, 166, 44), RGB(52, 176, 160), RGB(200, 46, 50), RGB(150, 156, 172)};
static const uint16_t FAM_INK[5] = {RGB(255, 255, 255), RGB(30, 24, 10), RGB(8, 30, 28), RGB(255, 255, 255), RGB(20, 24, 34)};

/* a page untouched this long: back to the faders (GLO > SETUP > BACK; 0: never) */
static const uint8_t UI_BACK_S[5] = {6, 12, 30, 60, 0};
static const char *const UI_BACK_NAME[5] = {"6 S", "12 S", "30 S", "60 S", "OFF"};
#define UI_HOLD_MS 1300u                        /* REC held 0.7 s, then this much more: the loop cleared */
#ifndef ZP12_VER
#define ZP12_VER "0.0"                          /* build.py --release X.Y */
#endif
static const char ZP12_VERSION[] = ZP12_VER;

enum { PG_HOME, PG_WAVE, PG_SOUND, PG_TRUNC, PG_OUT, PG_SFX, PG_FILT, PG_CHO, PG_DLY, PG_REV, PG_SEG, PG_SEG2, PG_SONG, PG_SETUP, PG_OUTPUT, PG_N };
static const char *const PG_NAME[PG_N] = {"MIX", "WAVE", "SOUND", "TRUNC", "OUT", "SENDS", "FILTER", "CHORUS", "DELAY", "REVERB",
                                          "LOOP", "TOOLS", "SONG", "SETUP", "OUTPUT"};
/* buttons: the printed labels' matrix ids (as SLOOP's PANEL_DEFAULT); SEL is SLOOP's SCL */
enum { B_OCTDN = 0, B_OCTUP = 1, B_FX = 2, B_SEL = 3, B_ENV = 4, B_LFO = 5, B_EDIT = 6, B_GLO = 7, B_HOME = 8,
       B_SAVE = 9, B_ARP = 10, B_SEQ = 11, B_PLAY = 12, B_REC = 13 };
/* the page families (the button that opens them): their name, colour, pages; on a page the header says where
 * (EDIT > SOUND) and the tabs take the faders' place */
static const char *const FAM_NAME[5] = {"MIX", "EDIT", "FX", "SEQ", "GLO"};
static const uint8_t FAM_FIRST[5] = {PG_HOME, PG_WAVE, PG_FILT, PG_SEG, PG_SETUP}, FAM_N[5] = {1, 5, 4, 3, 2};
static uint32_t fam_of(uint32_t pg) { return pg == PG_HOME ? 0u : pg <= PG_SFX ? 1u : pg <= PG_REV ? 2u : pg <= PG_SONG ? 3u : 4u; }
static const char *const DTIME_NAME[6] = {"1/4", "1/8", "1/16", "1/32", "8T", "16T"};
static const char *const CLICK_NAME[3] = {"OFF", "REC", "ON"};

static struct {
    uint8_t pair;                               /* 0: banks A + B on the keys, 1: C + D */
    uint8_t sel;                                /* the sound the knobs edit, 0..31 */
    uint8_t page;
    uint8_t multi;                              /* MULTI PITCH */
    uint8_t shift;                              /* SEL pressed (lit): the knobs are the faders 5-8 */
    uint8_t steps;                              /* SEQ held: the step grid on the keys */
    uint8_t step_bar;                           /* the bar the step grid shows */
    uint8_t song_cur;                           /* SONG page: the step under the knobs */
    uint8_t copy_to;                            /* SEG TOOLS: the target */
    uint8_t copy_pad;                           /* WAVE: the pad the sound goes to */
    uint8_t arm;                                /* a destructive knob turned once (it wants AGAIN): its page + 1 */
    uint8_t back;                               /* UI_BACK_S index (saved) */
    uint8_t turn_pad;                           /* the pad whose TUNE / DECAY / CUT was turned last ... */
    uint32_t turn_ms;                           /* ... and when (recording: its hits take them) */
    uint32_t held;                              /* the buttons held (bit = matrix id) */
    uint8_t rec_on, rec_prev, holding;          /* REC: pressed, the state before, held into a CLEAR */
    uint8_t save_used, prev_page;               /* SAVE held: a loop saved; the page before EDIT */
    uint32_t rec_t0, hold_t0;
    uint8_t mix[SP_NCH];
    uint32_t arm_ms, touch_ms;
    uint32_t hit_ms[32];                        /* when each pad was last heard (its key and pad light) */
    uint32_t chan_ms[SP_NCH];
    uint32_t tap_ms[4];                         /* TAP: the last taps */
    char msg[24];
    uint32_t msg_until;
    uint32_t sig_lcd, sig_mid, sig_pad, sig_led, sig_head;   /* what each region shows now */
    uint8_t force, save_req, factory_req;
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

/* the samples: the kit's, then the own ones from the web editor (sp_samples.c fills their names and flags) */
#define SP_NUSER 24
static char ui_uname[SP_NUSER][9];
static uint8_t ui_uflags[SP_NUSER];              /* bit 0: stored for 45->33, bit 1: at double speed (an octave down) */
static const char *wave_name(uint32_t w)
{
    return w < KIT_NWAVE ? KIT_WAVE[w].name : w < KIT_NWAVE + SP_NUSER && sp_wave[w].n ? ui_uname[w - KIT_NWAVE] : "-----";
}
static const char *sound_name(uint32_t k)
{
    return wave_name(sp_sound[k].wave);
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
    case PG_WAVE:
        COL(0, "WAVE", cat(val[0], wave_name(s->wave)));
        COL(1, "COPY>", pad_label(val[1], ui.copy_pad));
        COL(2, "COPY", cat(val[2], ui.arm == PG_WAVE + 1u ? "AGAIN" : "-->"));
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
    case PG_FILT:                                /* the DJ filter: LP 64 .. OFF .. HP 63 */
        COL(0, "FILTER", djf.v ? (void)(cat(val[0], djf.v < 0 ? "LP" : "HP"), num(val[0] + 2, djf.v < 0 ? -djf.v : djf.v, 2, 0))
                                : (void)cat(val[0], "OFF"));
        COL(1, "RESO", num(val[1], djf.res, 3, 0));
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
        COL(0, "LOOP", num(val[0], sq.seg + 1, 2, 0));
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
        if (ui.song_cur < SQ_SONG_N) {
            COL(1, "SEG", num(val[1], sq_song[ui.song_cur].seg + 1, 2, 0));
            COL(2, "REPEAT", num(val[2], sq_song[ui.song_cur].rep, 2, 0));
        } else {
            COL(1, "SEG", cat(val[1], "END"));
        }
        COL(3, "SONG", sq.song_mode ? (void)num(val[3], sq.song_sel + 1, 1, 0) : (void)cat(val[3], "OFF"));
        break;
    case PG_SETUP:
        COL(0, "TEMPO", num(val[0], (int32_t)(sq.bpm10 / 10u), 3, 0)); COL(1, "CLICK", cat(val[1], CLICK_NAME[sq.click % 3u]));
        COL(2, "BACK", cat(val[2], UI_BACK_NAME[ui.back % 5u]));
        COL(3, "RESET", cat(val[3], ui.arm == PG_SETUP + 1u ? "AGAIN" : "-->"));
        break;
    case PG_OUTPUT:
        COL(0, "SMOOTH", cat(val[0], sp_smooth ? "ON" : "OFF"));
        break;
    default:
        break;
    }
#undef COL
}

/* ---- the regions */
static uint32_t on_page(void) { return ui.page != PG_HOME; }   /* the knobs are a page's, not the faders */

static void draw_head(void)                     /* the header: the name; on a page where you are (EDIT > SOUND) */
{
    cv_begin(240, 20, P_FRAME);
    cv_text_on(6, 2, &FONT_S, "zp12", P_INK, P_FRAME);
    cv_text_on(7, 2, &FONT_S, "zp12", P_INK, P_FRAME);   /* (bold: twice, a pixel apart) */
    if (on_page()) {
        uint32_t fm = fam_of(ui.page);
        char b[24], *p = cat(b, FAM_NAME[fm]);
        if (FAM_N[fm] > 1u) { p = cat(p, " > "); cat(p, PG_NAME[ui.page]); }
        cv_rect(44, 1, 194, 18, FAM_COL[fm]);
        cv_text_on(50, 2, &FONT_S, b, FAM_INK[fm], FAM_COL[fm]);
        cv_text_on(51, 2, &FONT_S, b, FAM_INK[fm], FAM_COL[fm]);
    } else {
        cv_text_on(48, 2, &FONT_S, "sampling drums", P_INK, P_FRAME);
        cv_rect(170, 9, 66, 1, P_RED);
        cv_rect(170, 11, 66, 1, P_INK);
    }
    cv_blit(0, 0);
}

/* a page open: in the faders' place the family in its colour, what it edits, and its pages as tabs (this one lit) */
static void draw_tabs(void)
{
    uint32_t fm = fam_of(ui.page), i, n = FAM_N[fm], tw = 224u / n;
    char b[24], *p = b;
    b[0] = 0;
    cv_begin(232, 62, P_NAVY);
    cv_text_on(8, 2, &FONT_L, FAM_NAME[fm], FAM_COL[fm], P_NAVY);
    if (fm == 1u) {                              /* EDIT: the pad and its sample */
        pad_label(p, ui.sel);
        p[2] = ' ';
        cat(p + 3, sound_name(ui.sel));
    } else if (fm == 4u) {                       /* GLO: the version */
        cat(cat(p, "zp12 "), ZP12_VERSION);
    } else if (fm == 3u) {
        p = cat(p, "LOOP ");
        num(p, sq.seg + 1, sq.seg + 1u >= 10u ? 2u : 1u, 0);
    }
    cv_text_on(224 - text_w(&FONT_S, b), 14, &FONT_S, b, C_WHITE, P_NAVY);
    for (i = 0; i < n; i++) {
        uint32_t pg = FAM_FIRST[fm] + i;
        int32_t x = 4 + (int32_t)(i * tw);
        int cur = pg == ui.page;
        const char *nm = PG_NAME[pg];
        cv_rect(x, 38, (int32_t)tw - 2, 20, cur ? FAM_COL[fm] : RGB(52, 74, 124));
        cv_text_on(x + ((int32_t)tw - 2 - text_w(&FONT_S, nm)) / 2, 40, &FONT_S, nm, cur ? FAM_INK[fm] : P_RULE,
                   cur ? FAM_COL[fm] : RGB(52, 74, 124));
    }
    cv_blit(4, 100);
}

static void draw_frame(void)                    /* once: the light frame */
{
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
    if (ui.holding) {                           /* REC held: what letting go late does, and how long it still takes */
        uint32_t w = (fm1_ms - ui.hold_t0) * 212u / UI_HOLD_MS;
        p = cat(big, "CLEAR LOOP ");
        num(p, sq.seg + 1, sq.seg + 1u >= 10u ? 2u : 1u, 0);
        cv_rect(10, 34, 212, 3, P_LCDDIM);
        cv_rect(10, 34, (int32_t)(w < 212u ? w : 212u), 3, P_LCDINK);
    } else if (ui.msg[0] && (int32_t)(fm1_ms - ui.msg_until) < 0) {
        cat(big, ui.msg);
    } else if (ui.steps) {                      /* the step grid: the pad, the bar of the keys */
        pad_label(p, ui.sel);
        p = cat(p + 2, " BAR ");
        p = num(p, ui.step_bar + 1, 1, 0);
        p = cat(p, "/");
        num(p, sq_seg[sq.seg % SQ_NSEG].bars ? sq_seg[sq.seg % SQ_NSEG].bars : 1, 1, 0);
    } else if (sq.playing) {                    /* SEG01 2.3 */
        uint32_t t = sq.countin > 0 ? 0u : sq.pos >> 16;
        if (sq.song_mode && SQ_SONG_N) {          /* S2.03 L4: song 2, its step 3, loop 4 */
            p = cat(p, "S"); p = num(p, sq.song_sel + 1, 1, 0); p = cat(p, "."); p = num(p, sq.song_i + 1, 2, 0);
            p = cat(p, " L");
        } else p = cat(p, "LOOP");
        p = num(p, sq.seg + 1, sq.seg + 1u >= 10u ? 2u : 1u, 0);
        p = cat(p, " ");
        p = num(p, (int32_t)(t / SQ_BAR + 1u), t / SQ_BAR + 1u >= 10u ? 2u : 1u, 0);
        p = cat(p, ".");
        num(p, (int32_t)(t % SQ_BAR / SQ_PPQ + 1u), 1, 0);
    } else {
        pad_label(p, ui.sel);
        p = cat(p + 2, " ");
        cat(p, sound_name(ui.sel));
    }
    {   /* top right, small: the tempo (REC / COUNT / ARMED before it), below it the page; what does not fit
         * beside the big line is left out (the big line wins) */
        int32_t bw = 10 + text_w(&FONT_L, big) + 6;
        const char *pg = ui.page == PG_HOME ? "BPM" : PG_NAME[ui.page];
        const char *st = sq.playing && sq.countin > 0 ? "COUNT " : sq.recording ? "REC " : sq.rec_arm ? "ARMED " : "";
        p = cat(small, st);
        num(p, (int32_t)(bpm / 10u), 3, 0);
        if (222 - text_w(&FONT_S, small) < bw)
            num(small, (int32_t)(bpm / 10u), 3, 0);
        cv_text_on(10, 6, &FONT_L, big, P_LCDINK, P_LCD);
        if (222 - text_w(&FONT_S, small) >= bw)
            cv_text_on(222 - text_w(&FONT_S, small), 4, &FONT_S, small, P_LCDDIM, P_LCD);
        if (222 - text_w(&FONT_S, pg) >= bw)
            cv_text_on(222 - text_w(&FONT_S, pg), 20, &FONT_S, pg, P_LCDDIM, P_LCD);
    }
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
        uint32_t k = bank * 8u + r, row = 0, lks = 0;
        int32_t y = 4 + (int32_t)r * 13;
        char l[3];
        for (i = 0; i < g->n; i++)
            if (g->ev[i].pad == k && g->ev[i].t >= t0 && g->ev[i].t < t0 + SQ_BAR) {
                row |= 1u << ((g->ev[i].t - t0) / 24u);
                lks |= (uint32_t)g->ev[i].lk << ((g->ev[i].t - t0) / 24u);
            }
        pad_label(l, k);
        cv_text_on(2, y - 3, &FONT_S, l, k == ui.sel ? C_WHITE : P_RULE, P_NAVY);
        for (i = 0; i < 16u; i++) {
            int32_t x = 24 + (int32_t)i * 12 + (int32_t)(i / 4u) * 2;
            uint16_t c = (row >> i) & 1u ? (k == ui.sel ? P_STEP : P_FRAME) : (k == ui.sel ? RGB(70, 90, 140) : RGB(52, 74, 124));
            cv_rect(x, y, 10, 10, c);
            if ((lks >> i) & 1u)                 /* a hit with its own TUNE / DECAY / CUT: a dot */
                cv_rect(x + 3, y + 3, 4, 4, P_RED);
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
    sq.turn = sq.recording && (uint32_t)(fm1_ms - ui.turn_ms) < 600u ? 1u << ui.turn_pad : 0u;
    if (ui.page != PG_HOME && UI_BACK_S[ui.back % 5u] && (uint32_t)(fm1_ms - ui.touch_ms) > UI_BACK_S[ui.back % 5u] * 1000u && !ui.steps)
        ui.page = PG_HOME;                      /* untouched: the knobs are the faders again */
    if (ui.arm && (uint32_t)(fm1_ms - ui.arm_ms) > 1500u)
        ui.arm = 0;
    if (ui.force) {
        draw_frame();
        ui.sig_lcd = ui.sig_mid = ui.sig_pad = ui.sig_led = ui.sig_head = 0;
    }
    s = on_page() ? ui.page * 7u + 1u : 0x5A5Au;
    if (s != ui.sig_head || ui.force) { ui.sig_head = s; draw_head(); }
    s = sig_of(&sp_sound[ui.sel], sizeof(sp_sound_t), 2166136261u ^ ui.sel * 7u ^ ui.page * 131u ^ sq.bpm10 * 7919u ^ ui.multi);
    s = sig_of(ui.mix, sizeof ui.mix, s ^ ui.shift ^ ui.steps * 3u ^ ui.step_bar * 29u ^ ui.arm * 37u ^ ui.copy_to * 41u);
    s = sig_of(&fxp, sizeof fxp, s ^ (uint32_t)(djf.v + 64) * 6151u ^ (uint32_t)djf.res * 97u ^ ui.back * 2203u ^ sp_smooth * 4409u);
    s = sig_of(sq_songs, sizeof sq_songs, s ^ ui.song_cur * 31u ^ SQ_SONG_N * 17u ^ sq.song_mode ^ sq.song_sel * 7u);
    s ^= (sq.seg * 977u) ^ (sq.quant * 31u) ^ (sq.swing * 7u) ^ (sq.click * 3u) ^ sq_seg[sq.seg % SQ_NSEG].bars * 101u;
    s ^= sq.recording * 5u ^ sq.rec_arm * 11u;
    if (sq.playing)
        s ^= ((sq.countin > 0 ? 0u : (sq.pos >> 16) / SQ_PPQ) + 1u) * 2654435761u ^ (sq.countin > 0) * 9u ^ sq.song_i * 13u;
    if (ui.msg[0] && (int32_t)(fm1_ms - ui.msg_until) < 0)
        s = sig_of(ui.msg, sizeof ui.msg, s ^ 0x55u);
    if (ui.holding)
        s ^= ((fm1_ms - ui.hold_t0) / 40u + 1u) * 0x9E3779B1u;
    if (s != ui.sig_lcd || ui.force) { ui.sig_lcd = s; draw_lcd(); }
    if (ui.steps) {
        const sq_seg_t *g = &sq_seg[sq.seg % SQ_NSEG];
        uint32_t len = g->bars ? g->bars * SQ_BAR : SQ_BAR;
        s = 0x9E37u ^ sq.gen * 2654435761u ^ ui.sel * 131u ^ ui.step_bar * 7u;
        if (sq.playing) s ^= (((sq.pos >> 16) % len) / 24u + 1u) * 40503u;
        if (s != ui.sig_mid || ui.force) { ui.sig_mid = s; ui.sig_pad = 0; draw_grid(); }
    } else {
        if (on_page()) {                         /* a page: its tabs */
            s = (0xAB00u ^ ui.page * 977u ^ ui.sel * 131u ^ sp_sound[ui.sel].wave * 7919u ^ sq.seg * 7u) & ~1u;
            if (s != ui.sig_mid || ui.force) { ui.sig_mid = s; draw_tabs(); }
        } else {                                 /* the faders */
            for (i = 0; i < SP_NCH; i++)
                on |= (uint32_t)((int32_t)(fm1_ms - ui.chan_ms[i]) < 90 && sp_ch[i].on) << i;
            s = sig_of(ui.mix, sizeof ui.mix, on * 2654435761u ^ ui.shift * 3u ^ (ui.page == PG_HOME) * 5u) | 1u;
            if (s != ui.sig_mid || ui.force) { ui.sig_mid = s; draw_faders(); }
        }
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
                                         0xFF, 10, 11, 0xFF, 12, 0xFF, 13, 14, 0xFF, 15};   /* F3 .. G5 */
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
    if ((ui.held >> B_SAVE) & 1u) {              /* SAVE held: a black key saves the loop into that loop */
        if (white_of(k) == 0xFFu) {
            uint32_t b = 0, i;
            char m[24], *p = cat(m, "LOOP ");
            for (i = 0; i < k; i++) b += white_of(i) == 0xFFu;
            if (b != sq.seg)
                sq_post(RQ_COPY, 0, b);
            p = num(p, (int32_t)b + 1, b + 1u >= 10u ? 2u : 1u, 0);
            cat(p, " SAVED");
            ui_say(m);
            ui.save_req = 1;
            ui.save_used = 1;
        }
        return;
    }
    if (ui.steps) {                              /* SEQ held: a white key sets / clears the selected pad's step */
        uint32_t w = white_of(k);
        if (w != 0xFFu) {
            sq_post(RQ_STEP, ui.sel, ui.step_bar * SQ_BAR + w * 24u);
            if (!sq_seg[sq.seg % SQ_NSEG].bars)
                sq_seg[sq.seg % SQ_NSEG].bars = 1;   /* (a step written makes an AUTO segment a bar long) */
        }
        return;
    }
    if (white_of(k) == 0xFFu && !ui.multi && !erase) {   /* a black key: loop (segment) 1-11, at the loop's end */
        uint32_t b = 0, i;
        for (i = 0; i < k; i++) b += white_of(i) == 0xFFu;
        sq_post(RQ_LOOP, 0, b);
        {   /* say it: now, or at the end of the loop playing */
            char m[24], *p = cat(m, "LOOP ");
            p = num(p, (int32_t)b + 1, b + 1u >= 10u ? 2u : 1u, 0);
            cat(p, sq.playing && b != sq.seg ? " NEXT" : "");
            ui_say(m);
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

/* a 0..127 value's step: 3 a detent, more when turned fast (a half turn from 0 to 127) */
static int32_t accel(int32_t d)
{
    int32_t a = d < 0 ? -d : d;
    return d * (a >= 3 ? 8 : a == 2 ? 5 : 3);
}

/* the sample the selected pad plays, one on (the factory's, then the own ones): any sample on as many pads as
 * wanted, each with its own TUNE, DECAY, ... */
static void wave_set(uint32_t pad, uint32_t w)
{
    sp_sound_t *s = &sp_sound[pad % SP_NSOUND];
    char m[24];
    uint32_t was = s->wave >= KIT_NWAVE && s->wave < KIT_NWAVE + SP_NUSER ? ui_uflags[s->wave - KIT_NWAVE] : 0u;
    uint32_t now = w >= KIT_NWAVE ? ui_uflags[w - KIT_NWAVE] : 0u;
    s->wave = (uint8_t)w;
    if (w >= KIT_NWAVE)                          /* an own sample: 45->33 as it was stored */
        s->flags = (uint8_t)((now & 1u) ? s->flags | SPF_33 : s->flags & ~SPF_33);
    if ((now ^ was) & 2u)                        /* stored at double speed: played an octave down (and back) */
        s->tune = (now & 2u) ? -12 : 0;
    s->start = 0;
    s->end = 1000;
    ui.sel = (uint8_t)(pad % SP_NSOUND);
    SP_HIT(ui.sel, 100, 0);                    /* (heard at once) */
    pad_label(m, ui.sel);
    m[2] = ' ';
    cat(m + 3, wave_name(w));
    ui_say(m);
}
static void wave_step(int32_t one)
{
    uint32_t n = KIT_NWAVE + SP_NUSER, w = sp_sound[ui.sel].wave < n ? sp_sound[ui.sel].wave : 0u, i;
    for (i = 0; i < n; i++) {                   /* the next that is there */
        w = (w + (one > 0 ? 1u : n - 1u)) % n;
        if (sp_wave[w].n)
            break;
    }
    wave_set(ui.sel, w);
}

/* PRESETS turned: the sample of the pad played last */
static void ui_preset(int32_t d)
{
    ui.touch_ms = fm1_ms;
    wave_step(d > 0 ? 1 : -1);
}

/* KNOB n turned by d */
static void knob(uint32_t n, int32_t d)
{
    sp_sound_t *s = &sp_sound[ui.sel];
    int32_t one = d > 0 ? 1 : -1, dd = accel(d);
    ui.touch_ms = fm1_ms;
    switch (ui.page) {
    case PG_HOME: {
        uint32_t c = (ui.shift ? 4u : 0u) + n;     /* the faders: channels 1-4, SEL lit 5-8 */
        ui.mix[c] = (uint8_t)sp_clamp(ui.mix[c] + dd, 0, 127);
        sp_mix[c] = ui.mix[c];
        break;
    }
    case PG_WAVE:
        if (n == 0u)
            wave_step(one);
        if (n == 1u) ui.copy_pad = (uint8_t)((ui.copy_pad + SP_NSOUND + (one > 0 ? 1u : SP_NSOUND - 1u)) % SP_NSOUND);
        if (n == 2u && d > 0 && again(PG_WAVE + 1u) && ui.copy_pad != ui.sel) {
            sp_sound[ui.copy_pad] = *s;
            ui_say("SOUND COPIED");
        }
        break;
    case PG_SOUND:
        if (n < 3u) { ui.turn_pad = ui.sel; ui.turn_ms = fm1_ms; }   /* (recording: the hits take TUNE / FINE / DECAY) */
        if (n == 0u) s->tune = (int8_t)sp_clamp(s->tune + one, -24, 12);
        if (n == 1u) s->fine = (int8_t)sp_clamp(s->fine + d, -50, 50);
        if (n == 2u) s->decay = (uint8_t)sp_clamp(s->decay + dd, 0, 127);
        if (n == 3u) s->level = (uint8_t)sp_clamp(s->level + dd, 0, 127);
        break;
    case PG_TRUNC:
        if (n == 0u) s->start = (uint16_t)sp_clamp(s->start + dd * 3, 0, s->end - 10);
        if (n == 1u) s->end = (uint16_t)sp_clamp(s->end + dd * 3, s->start + 10, 1000);
        if (n == 2u) s->flags = (uint8_t)(one > 0 ? s->flags | SPF_REVERSE : s->flags & ~SPF_REVERSE);
        if (n == 3u) s->flags = (uint8_t)(one > 0 ? s->flags | SPF_33 : s->flags & ~SPF_33);
        break;
    case PG_OUT:
        if (n == 0u) s->chan = (uint8_t)sp_clamp(s->chan + one, 0, SP_NCH - 1);
        if (n == 1u) s->pan = (int8_t)sp_clamp(s->pan + dd, -64, 63);
        if (n == 2u) { s->cut = (uint8_t)sp_clamp(s->cut + dd, 0, 127); ui.turn_pad = ui.sel; ui.turn_ms = fm1_ms; }
        if (n == 3u) s->reso = (uint8_t)sp_clamp(s->reso + dd, 0, 127);
        break;
    case PG_SFX:
        if (n == 0u) s->drive = (uint8_t)sp_clamp(s->drive + dd, 0, 127);
        else s->send[n - 1u] = (uint8_t)sp_clamp(s->send[n - 1u] + dd, 0, 127);
        break;
    case PG_FILT:
        if (n == 0u) djf.v = (int8_t)sp_clamp(djf.v + d * 2, -64, 63);   /* (one turn's half: open to shut) */
        if (n == 1u) djf.res = (int8_t)sp_clamp(djf.res + dd, 0, 127);
        break;
    case PG_CHO:
        if (n == 0u) fxp.crate = (int16_t)sp_clamp(fxp.crate + dd, 0, 127);
        if (n == 1u) fxp.cdepth = (int16_t)sp_clamp(fxp.cdepth + dd, 0, 127);
        if (n == 2u) fxp.cmix = (int16_t)sp_clamp(fxp.cmix + dd, 0, 127);
        break;
    case PG_DLY:
        if (n == 0u) fxp.dtime = (int16_t)sp_clamp(fxp.dtime + one, 0, 5);
        if (n == 1u) fxp.fdbk = (int16_t)sp_clamp(fxp.fdbk + dd, 0, 120);
        if (n == 2u) fxp.colr = (int16_t)sp_clamp(fxp.colr + dd, 0, 127);
        if (n == 3u) fxp.dmix = (int16_t)sp_clamp(fxp.dmix + dd, 0, 127);
        break;
    case PG_REV:
        if (n == 0u) fxp.size = (int16_t)sp_clamp(fxp.size + dd, 0, 127);
        if (n == 1u) fxp.damp = (int16_t)sp_clamp(fxp.damp + dd, 0, 127);
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
        if (n == 0u) ui.song_cur = (uint8_t)sp_clamp(ui.song_cur + one, 0, SQ_SONG_N < SQ_NSONG ? SQ_SONG_N : SQ_NSONG - 1);
        if (n == 1u && ui.song_cur < SQ_NSONG && !sq.playing) {
            uint32_t i = ui.song_cur;
            if (i >= SQ_SONG_N) {                     /* past the end: a new step with the segment edited */
                sq_song[i].seg = sq.seg;
                sq_song[i].rep = 1;
                SQ_SONG_N = (uint8_t)(i + 1u);
            } else {
                sq_song[i].seg = (uint8_t)sp_clamp(sq_song[i].seg + one, 0, SQ_NSEG - 1);
            }
        }
        if (n == 2u && ui.song_cur < SQ_SONG_N && !sq.playing) {   /* REPEAT 0: the song ends here */
            int32_t r = sp_clamp(sq_song[ui.song_cur].rep + one, 0, 99);
            if (r == 0) SQ_SONG_N = ui.song_cur;
            else sq_song[ui.song_cur].rep = (uint8_t)r;
        }
        if (n == 3u && !sq.playing) {              /* SONG: OFF (the loops) or 1-4, the one played and edited */
            int32_t v = sp_clamp((sq.song_mode ? sq.song_sel + 1 : 0) + one, 0, (int32_t)SQ_SONGS);
            sq.song_mode = (uint8_t)(v > 0);
            if (v > 0) { sq.song_sel = (uint8_t)(v - 1); ui.song_cur = 0; }
        }
        break;
    case PG_SETUP:
        if (n == 0u) sq.bpm10 = (uint16_t)sp_clamp(sq.bpm10 + dd * 3, 400, 2400);
        if (n == 1u) sq.click = (uint8_t)sp_clamp(sq.click + one, 0, 2);
        if (n == 2u) ui.back = (uint8_t)sp_clamp(ui.back + one, 0, 4);
        if (n == 3u && d > 0 && !sq.playing && again(PG_SETUP + 1u))
            ui.factory_req = 1;                    /* (zp12.c: everything to the factory state, saved) */
        break;
    case PG_OUTPUT:
        if (n == 0u) sp_smooth = one > 0;         /* (right: on, left: off) */
        break;
    default:
        break;
    }
}

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
        if ((ui.held >> B_EDIT) & 1u) {          /* EDIT + OCT-: undo the last CLEAR / ERASE / COPY (again: redo) */
            sq_post(RQ_UNDO, 0, 0);
            page(ui.prev_page);
            ui_say(sq_undo_of < SQ_NSEG ? "UNDO" : "NO UNDO");
        } else if (ui.steps) { if (ui.step_bar) ui.step_bar--; }
        else ui.pair = 0;
        break;
    case B_ARP: ui.multi ^= 1u; break;
    case B_HOME: page(PG_HOME); break;
    case B_SEL:                                  /* the faders 5-8 (lit) or 1-4, until pressed again; from a page: the faders */
        ui.shift ^= 1u;
        page(PG_HOME);
        ui_say(ui.shift ? "FADERS 5-8" : "FADERS 1-4");
        break;
    case B_EDIT: ui.prev_page = ui.page; page(ui.page >= PG_WAVE && ui.page < PG_SFX ? ui.page + 1u : PG_WAVE); break;
    case B_FX: page(ui.page >= PG_FILT && ui.page < PG_REV ? ui.page + 1u : PG_FILT); break;
    case B_SEQ: page(ui.page == PG_SEG ? PG_SEG2 : ui.page == PG_SEG2 ? PG_SONG : PG_SEG); break;
    case B_SAVE: ui.save_req = 1; ui_say("SAVED"); break;
    case B_GLO: page(ui.page == PG_SETUP ? PG_OUTPUT : PG_SETUP); break;
    case B_ENV: tap_tempo(); break;
    case B_PLAY: sq_post(sq.playing ? RQ_STOP : RQ_PLAY, 0, 0); break;
    case B_REC: sq_post(RQ_REC, 0, 0); break;
    default: break;
    }
}

/* REC and SAVE, as on sloopDX. REC acts on the press (no lag); held 0.7 s the press is undone and a bar fills:
 * held to its end, the loop is cleared (EDIT + OCT- brings it back), let go before, nothing. SAVE tapped saves
 * now; held, a black key saves the loop into that loop (key_down). held: the buttons held now; returns pressed
 * without the two. */
static uint32_t ui_holds(uint32_t held, uint32_t pressed, uint32_t released)
{
    ui.held = held;
    if (pressed & (1u << B_REC)) {
        ui.rec_prev = (uint8_t)(sq.recording | sq.rec_arm << 1);
        ui.rec_t0 = fm1_ms;
        ui.rec_on = 1;
        sq_post(RQ_REC, 0, 0);
    }
    if ((held >> B_REC) & 1u) {
        if (ui.rec_on && !ui.holding && (uint32_t)(fm1_ms - ui.rec_t0) >= 700u) {
            sq_post(RQ_RECSET, 0, ui.rec_prev); /* a hold: the press undone */
            ui.holding = 1;
            ui.hold_t0 = fm1_ms;
        }
        if (ui.holding && (uint32_t)(fm1_ms - ui.hold_t0) >= UI_HOLD_MS) {
            char m[24], *p = cat(m, "LOOP ");
            sq_post(RQ_CLEAR, 0, 0);
            p = num(p, sq.seg + 1, sq.seg + 1u >= 10u ? 2u : 1u, 0);
            cat(p, " CLEARED");
            ui_say(m);
            ui.holding = 0;
            ui.rec_on = 0;                       /* (until REC is up: nothing more) */
        }
    } else {
        ui.holding = 0;                          /* let go before the end: nothing */
        ui.rec_on = 0;
    }
    if (pressed & (1u << B_SAVE))
        ui.save_used = 0;
    if (released & (1u << B_SAVE) && !ui.save_used) {
        ui.save_req = 1;
        ui_say("SAVED");
    }
    return pressed & ~((1u << B_REC) | (1u << B_SAVE));
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
        uint32_t nb = 0;
        for (i = 0; i < 27u; i++) {
            uint32_t p = ui.multi ? 0xFFu : key_pad(i);
            if (p < 32u && (int32_t)(fm1_ms - ui.hit_ms[p]) < 90)
                k |= 1u << i;
            if (white_of(i) == 0xFFu && !ui.multi) {   /* the loops: the one playing lit, one waiting blinks */
                if (nb == sq.seg) k |= 1u << i;
                if (nb == sq.next_seg && blink) k |= 1u << i;
                g |= 1u << i;                    /* (the others glow: there is a loop under every black key) */
                nb++;
            }
        }
    }
    *btn = b;
    *keys = k;
    *glow = g;
}

/* ---- the start: a kick is sampled into the LCD (the 12-bit steps drawn as they come in), then the name is
 * typed in big LCD pixels, each letter a drum: ba (tom), dum (tom), tss (kick + crash: "12"). t: ms since the
 * start; zp12.c draws it and plays SPLASH_HIT: the pads A8, A7, A1 + B1 as they are (own sounds there: an own start) */
#define SPLASH_MS 3000u
static const struct { uint16_t t; uint8_t pad, vel; } SPLASH_HIT[4] = {{1350, 7, 84}, {1550, 6, 84}, {1850, 0, 100}, {1850, 8, 70}};

static void cv_big(int32_t x, int32_t y, const char *s, int32_t k, uint16_t c)   /* FONT_S, k x k pixels a dot */
{
    for (; *s; s++) {
        uint32_t gi = glyph(&FONT_S, (uint8_t)*s), gx, gy, w = FONT_S.bw[gi], bpr = (w + 1u) / 2u;
        const uint8_t *gd = FONT_S.data + FONT_S.off[gi];
        for (gy = 0; gy < FONT_S.h; gy++)
            for (gx = 0; gx < w; gx++) {
                uint32_t v = gd[gy * bpr + gx / 2u];
                if (((gx & 1u) ? (v & 15u) : (v >> 4)) >= 8u)
                    cv_rect(x + ((int32_t)gx - FONT_S.pad) * k, y + (int32_t)gy * k, k, k, c);
            }
        x += FONT_S.adv[gi] * k;
    }
}

static int32_t splash_kick(uint32_t i)            /* the sampled kick at x = i: its pitch falling, dying away, held in steps */
{
    int32_t x = (int32_t)(i / 3u * 3u), ph = (85 * x - x * x / 8) & 1023, e = 42 - x * 34 / 232, v;
    v = ph < 512 ? ph - 256 : 767 - ph;          /* (a triangle for the sine: no tables) */
    return v * e / 256;
}

static void ui_splash(uint32_t t)
{
    uint32_t i, lit = 0, n = t < 1300u ? t * 232u / 1300u : 232u, flash = t >= 1850u && t < 1990u;
    char b[24];
    cv_begin(240, 20, P_NAVY);                   /* the top: SAMPLING and its LED, the rate */
    if (t < 1300u) {
        cv_text_on(8, 2, &FONT_S, "SAMPLING", P_FRAME, P_NAVY);
        if ((t / 250u) & 1u) cv_rect(84, 7, 7, 7, P_LED);
        num(b, (int32_t)(n * 26040u / 232u), 5, 0);   /* (the samples taken: one second's worth) */
        cv_text_on(232 - text_w(&FONT_S, b), 2, &FONT_S, b, P_RULE, P_NAVY);
    } else {
        cv_text_on(8, 2, &FONT_S, "12 BIT  26.04 KHZ", P_RULE, P_NAVY);
    }
    cv_blit(0, 0);
    cv_begin(232, 112, flash ? P_LCDINK : P_LCD);   /* the LCD: the wave coming in, then the name over it */
    cv_rect(0, 0, 232, 3, RGB(70, 76, 66));
    for (i = 0; i < n; i++) {                     /* the wave as stairs: each step, and the rise to it */
        int32_t v = splash_kick(i), u = i ? splash_kick(i - 1u) : v, lo = v < u ? v : u, hi = v < u ? u : v;
        cv_rect((int32_t)i, 56 - hi, 2, hi - lo + 2, t < 1300u ? P_LCDINK : RGB(150, 162, 124));
    }
    if (t < 1300u)
        cv_rect((int32_t)n, 8, 2, 96, P_RED);    /* the write head */
    {
        static const char NAME[] = "zp12";
        uint32_t k = t < 1350u ? 0u : t < 1550u ? 1u : t < 1850u ? 2u : 4u;
        char nm[5];
        for (i = 0; i < k; i++) nm[i] = NAME[i];
        nm[k] = 0;
        if (k) {
            int32_t sh = flash ? (int32_t)((t / 30u) & 1u) * 4 - 2 : 0;   /* (the crash shakes it) */
            cv_big(116 - 80 + sh, 16, nm, 5, flash ? P_LCD : P_LCDINK);
        }
    }
    cv_blit(4, 22);
    for (i = 0; i < 4u; i++)                      /* the pads: a chase while sampling, the hits after */
        if (t >= SPLASH_HIT[i].t && t < SPLASH_HIT[i].t + 160u) lit |= 1u << (SPLASH_HIT[i].pad & 7u);
    if (t < 1300u) lit = 1u << (t / 90u % 8u);
    if (flash) lit = 0xFF;
    cv_begin(232, 46, P_NAVY);
    for (i = 0; i < 8u; i++) {
        int32_t cx = 14 + (int32_t)i * 29;
        cv_rect(cx - 11, 2, 23, 23, RGB(6, 6, 8));
        cv_rect(cx - 10, 3, 21, 21, (lit >> i) & 1u ? P_LED : P_PAD);
        cv_rect(cx - 8, 5, 17, 2, (lit >> i) & 1u ? RGB(255, 140, 120) : RGB(48, 48, 52));
    }
    if (t >= 1900u) {
        cat(cat(b, "12-bit sampling drums  "), ZP12_VERSION);
        cv_text_on(116 - text_w(&FONT_S, b) / 2, 28, &FONT_S, b, P_FRAME, P_NAVY);
    }
    cv_blit(4, 136);
    cv_begin(240, 58, P_NAVY);
    if (t >= 2100u)
        cv_text_on(120 - text_w(&FONT_S, "based on SLOOP + Felucca") / 2, 30, &FONT_S, "based on SLOOP + Felucca", P_RULE, P_NAVY);
    cv_blit(0, 182);
    lcd_sync();
}

static void ui_init(void)
{
    uint32_t i;
    for (i = 0; i < SP_NCH; i++)
        ui.mix[i] = sp_mix[i];
    ui.force = 1;
}
