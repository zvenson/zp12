/* SPDX-License-Identifier: GPL-3.0-only */
/* zp12: the screen and the controls. One screen, laid out as an 80s sampling drum machine's panel:
 * a light frame, a two-line LCD, the PERFORMANCE section in navy with the eight channel faders and the
 * eight pads, the bank and transport LEDs. Drawn in regions (LCD, faders, pads, LEDs), each only when
 * what it shows changed. No hardware here: the host test draws the same screens (tests/zp12_ui_test.c).
 *
 * Keys: the 16 white keys are two banks of 8 pads (A + B; OCT+ C + D, OCT- back). The last pad hit is
 * the sound the knobs edit. ARP: MULTI PITCH (the sound over all 27 keys). EDIT steps SOUND / TRUNC /
 * OUT, GLO the MIX pages, HOME back. SELECT: tempo. ALGORITHM: the sound. */

/* the panel's colours */
#define P_FRAME RGB(206, 208, 201)
#define P_NAVY RGB(38, 62, 112)
#define P_NAVY2 RGB(30, 50, 92)
#define P_INK RGB(30, 48, 96)                   /* the frame's printing */
#define P_RED RGB(200, 46, 50)
#define P_LCD RGB(164, 172, 140)
#define P_LCDINK RGB(36, 44, 30)
#define P_LED RGB(255, 40, 32)
#define P_LEDOFF RGB(70, 20, 22)
#define P_CAP RGB(214, 216, 220)
#define P_SLOT RGB(10, 12, 18)
#define P_PAD RGB(18, 18, 20)
#define P_RULE RGB(120, 140, 180)

enum { PG_HOME, PG_SOUND, PG_TRUNC, PG_OUT, PG_SFX, PG_CHO, PG_DLY, PG_REV, PG_SEG, PG_SONG, PG_SETUP, PG_N };
static struct {
    uint8_t pair;                               /* 0: banks A + B on the keys, 1: C + D */
    uint8_t sel;                                /* the sound the knobs edit, 0..31 */
    uint8_t page;
    uint8_t multi;                              /* MULTI PITCH */
    uint8_t shift;                              /* SEL held: the faders 5-8 */
    uint8_t song_cur;                           /* SONG page: the step under the knobs */
    uint8_t mix[SP_NCH];
    uint32_t hit_ms[8];                         /* when each on-screen pad was last hit (its light) */
    uint32_t chan_ms[SP_NCH];
    uint32_t tap_ms[4];                         /* TAP: the last taps */
    char msg[24];
    uint32_t msg_until;
    uint32_t sig_lcd, sig_fad, sig_pad, sig_led;   /* what each region shows now */
    uint8_t force;
} ui;

static const char *const PG_NAME[PG_N] = {"HOME", "SOUND", "TRUNC", "OUT", "FX SENDS", "CHORUS", "DELAY", "REVERB",
                                          "SEGMENT", "SONG", "SETUP"};
static const char *const DTIME_NAME[6] = {"1/4", "1/8", "1/16", "1/32", "8T", "16T"};
static const char *const CLICK_NAME[3] = {"OFF", "REC", "ON"};

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

static const char *sound_name(uint32_t k)
{
    const sp_sound_t *s = &sp_sound[k];
    return s->wave < KIT_NWAVE ? KIT_WAVE[s->wave].name : s->wave == 0xFFu ? "-----" : "USER";
}
static void pad_label(char *b, uint32_t k) { b[0] = (char)('A' + k / 8u); b[1] = (char)('1' + k % 8u); b[2] = 0; }

/* ---- the regions */
static void draw_frame(void)                    /* once: the light frame, the header, the navy section */
{
    cv_begin(240, 22, P_FRAME);
    cv_text_on(6, 3, &FONT_S, "zp12", P_INK, P_FRAME);
    cv_text_on(7, 3, &FONT_S, "zp12", P_INK, P_FRAME);   /* (bold: twice, a pixel apart) */
    cv_text_on(48, 3, &FONT_S, "sampling drums", P_INK, P_FRAME);
    cv_rect(0, 20, 240, 2, P_FRAME);
    cv_blit(0, 0);
    lcd_fill(0, 22, 4, 218, P_FRAME);
    lcd_fill(236, 22, 4, 218, P_FRAME);
    lcd_fill(4, 22, 232, 4, P_FRAME);
    lcd_fill(4, 66, 232, 4, P_FRAME);
    cv_begin(232, 16, P_NAVY);                  /* the section's title band with its red rule */
    cv_rect(0, 0, 232, 15, P_FRAME);
    cv_text_on(4, 0, &FONT_S, "Performance", P_INK, P_FRAME);
    cv_rect(96, 12, 136, 1, P_RED);
    cv_rect(96, 14, 136, 1, P_INK);
    cv_blit(4, 70);
}

static char *num(char *p, int32_t v, uint32_t digits, int sign) { put_int(p, v, digits, sign); return p + digits + (sign != 0); }

static void draw_lcd(void)                      /* the LCD: where the sequencer is, what is selected, its values */
{
    char l1[32], l2[40], *p = l1;
    const sp_sound_t *s = &sp_sound[ui.sel];
    uint32_t bpm = sq.bpm10;
    cv_begin(232, 40, P_FRAME);
    cv_rect(2, 0, 228, 40, RGB(70, 76, 66));    /* the bezel */
    cv_rect(5, 3, 222, 34, P_LCD);
    if (ui.msg[0] && (int32_t)(fm1_ms - ui.msg_until) < 0) {
        cat(l1, ui.msg);
    } else if (sq.playing) {                    /* SEG01 2.3  REC  092.0 */
        uint32_t t = sq.countin > 0 ? 0u : sq.pos >> 16;
        p = cat(p, sq.song_mode && sq.song_n ? "S" : "SEG");
        if (sq.song_mode && sq.song_n) { p = num(p, sq.song_i + 1, 2, 0); p = cat(p, ":"); }
        p = num(p, sq.seg + 1, 2, 0);
        p = cat(p, " ");
        p = num(p, (int32_t)(t / SQ_BAR + 1u), 1, 0);
        p = cat(p, ".");
        p = num(p, (int32_t)(t % SQ_BAR / SQ_PPQ + 1u), 1, 0);
        p = cat(p, sq.countin > 0 ? " CNT" : sq.recording ? " REC" : "    ");
        while (p < l1 + 15) *p++ = ' ';
        p = num(p, (int32_t)(bpm / 10u), 3, 0); p = cat(p, "."); num(p, (int32_t)(bpm % 10u), 1, 0);
    } else {
        pad_label(p, ui.sel);
        p = cat(p + 2, " ");
        p = cat(p, sound_name(ui.sel));
        while (p < l1 + 15) *p++ = ' ';
        p = num(p, (int32_t)(bpm / 10u), 3, 0); p = cat(p, "."); num(p, (int32_t)(bpm % 10u), 1, 0);
    }
    cv_text_on(9, 3, &FONT_S, l1, P_LCDINK, P_LCD);
    p = l2;
    *p = 0;
    switch (ui.page) {
    case PG_HOME: {
        uint32_t i, b0 = ui.shift ? 4u : 0u;
        p = cat(p, ui.shift ? "5-8" : "1-4");
        for (i = 0; i < 4u; i++) { p = cat(p, " "); p = num(p, ui.mix[b0 + i], 3, 0); }
        break;
    }
    case PG_SOUND:
        p = cat(p, "T"); p = num(p, s->tune, 2, 1); p = cat(p, " F"); p = num(p, s->fine, 2, 1);
        p = cat(p, " D"); p = num(p, s->decay, 3, 0); p = cat(p, " L"); num(p, s->level, 3, 0);
        break;
    case PG_TRUNC:
        p = cat(p, "S"); p = num(p, s->start, 4, 0); p = cat(p, " E"); p = num(p, s->end, 4, 0);
        p = cat(p, (s->flags & SPF_REVERSE) ? " REV" : " FWD"); cat(p, (s->flags & SPF_33) ? " 33" : " 45");
        break;
    case PG_OUT:
        p = cat(p, "CH"); p = num(p, s->chan + 1, 1, 0); p = cat(p, " P"); p = num(p, s->pan, 2, 1);
        if (s->chan < 2u) { p = cat(p, " C"); p = num(p, s->cut, 3, 0); p = cat(p, " Q"); num(p, s->reso, 3, 0); }
        else cat(p, s->chan < 6u ? " FIXED LP" : " NO FILTER");
        break;
    case PG_SFX:
        p = cat(p, "DRV"); p = num(p, s->drive, 3, 0); p = cat(p, " CH"); p = num(p, s->send[0], 3, 0);
        p = cat(p, " DL"); p = num(p, s->send[1], 3, 0); p = cat(p, " RV"); num(p, s->send[2], 3, 0);
        break;
    case PG_CHO:
        p = cat(p, "RATE"); p = num(p, fxp.crate, 3, 0); p = cat(p, " DEP"); p = num(p, fxp.cdepth, 3, 0);
        p = cat(p, " MIX"); num(p, fxp.cmix, 3, 0);
        break;
    case PG_DLY:
        p = cat(p, DTIME_NAME[fxp.dtime % 6]); p = cat(p, " FB"); p = num(p, fxp.fdbk, 3, 0);
        p = cat(p, " CO"); p = num(p, fxp.colr, 3, 0); p = cat(p, " MX"); num(p, fxp.dmix, 3, 0);
        break;
    case PG_REV:
        p = cat(p, "SIZE"); p = num(p, fxp.size, 3, 0); p = cat(p, " DAMP"); p = num(p, fxp.damp, 3, 0);
        p = cat(p, " PRE"); p = num(p, fxp.pre, 2, 0); cat(p, "ms");
        break;
    case PG_SEG: {
        sq_seg_t *g = &sq_seg[sq.seg % SQ_NSEG];
        p = cat(p, "SEG"); p = num(p, sq.seg + 1, 2, 0); p = cat(p, " "); p = num(p, g->bars, 1, 0);
        p = cat(p, "BAR "); p = cat(p, SQ_GRID_NAME[sq.quant % 7u]); p = cat(p, " SW"); num(p, SQ_SWING[sq.swing % 6u], 2, 0);
        break;
    }
    case PG_SONG:
        p = cat(p, sq.song_mode ? "SONG " : "seg  ");
        p = num(p, ui.song_cur + 1, 2, 0); p = cat(p, "/"); p = num(p, sq.song_n, 2, 0);
        if (ui.song_cur < sq.song_n) {
            p = cat(p, " SEG"); p = num(p, sq_song[ui.song_cur].seg + 1, 2, 0);
            p = cat(p, " x"); num(p, sq_song[ui.song_cur].rep, 2, 0);
        } else {
            cat(p, " END");
        }
        break;
    case PG_SETUP:
        p = cat(p, "TEMPO "); p = num(p, (int32_t)(bpm / 10u), 3, 0); p = cat(p, "."); p = num(p, (int32_t)(bpm % 10u), 1, 0);
        p = cat(p, " CLICK "); cat(p, CLICK_NAME[sq.click % 3u]);
        break;
    default:
        break;
    }
    if (ui.multi && ui.page != PG_HOME)
        cat(l2, "");
    cv_text_on(9, 20, &FONT_S, l2, P_LCDINK, P_LCD);
    cv_blit(4, 26);
}

static uint32_t fader_y(uint32_t lv) { return 62u - lv * 52u / 127u; }   /* the cap's top, in the region */

static void draw_faders(void)                   /* the eight channel levels; a cap lights while it sounds */
{
    uint32_t i, k;
    cv_begin(232, 80, P_NAVY);
    for (k = 0; k < 9u; k++)                    /* the scale lines behind the faders */
        cv_rect(12, 10 + (int32_t)k * 7, 208, 1, P_RULE);
    for (i = 0; i < SP_NCH; i++) {
        int32_t cx = 14 + (int32_t)i * 29, y = (int32_t)fader_y(ui.mix[i]);
        int on = (int32_t)(fm1_ms - ui.chan_ms[i]) < 90 && sp_ch[i].on;
        char n[2] = {(char)('1' + i), 0};
        cv_rect(cx - 1, 6, 3, 62, P_SLOT);
        cv_rect(cx - 9, y, 19, 7, on ? P_LED : P_CAP);
        cv_rect(cx - 9, y + 3, 19, 1, on ? RGB(120, 10, 10) : RGB(90, 90, 96));
        cv_text_on(cx - 4, 66, &FONT_S, n, P_FRAME, P_NAVY);
        if (ui.page == PG_HOME && (i >= 4u) == (ui.shift != 0))
            cv_rect(cx - 9, 79, 19, 1, P_FRAME);        /* the four the knobs move now */
    }
    cv_blit(4, 86);
}

static void draw_pads(void)                     /* the eight pads of the bank the last hit was in, its labels */
{
    uint32_t i, bank = ui.sel / 8u;
    cv_begin(232, 52, P_NAVY);
    for (i = 0; i < 8u; i++) {
        int32_t cx = 14 + (int32_t)i * 29;
        uint32_t k = bank * 8u + i;
        int lit = (int32_t)(fm1_ms - ui.hit_ms[i]) < 120;
        char l[3];
        cv_rect(cx - 11, 4, 23, 23, RGB(6, 6, 8));
        cv_rect(cx - 10, 5, 21, 21, lit ? P_LED : P_PAD);
        cv_rect(cx - 8, 7, 17, 2, lit ? RGB(255, 140, 120) : RGB(48, 48, 52));   /* the light on its top edge */
        pad_label(l, k);
        cv_text_on(cx - 8, 30, &FONT_S, l, k == ui.sel ? C_WHITE : P_FRAME, P_NAVY);
    }
    cv_blit(4, 166);
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
    cv_blit(4, 218);
}

static uint32_t sig_of(const void *p, uint32_t n, uint32_t h)
{
    const uint8_t *b = p;
    while (n--) h = (h ^ *b++) * 16777619u;
    return h;
}

static void ui_draw(void)
{
    uint32_t i, s, lit = 0, on = 0;
    if (ui.force) {
        draw_frame();
        ui.sig_lcd = ui.sig_fad = ui.sig_pad = ui.sig_led = 0;
    }
    s = sig_of(&sp_sound[ui.sel], sizeof(sp_sound_t), 2166136261u ^ ui.sel * 7u ^ ui.page * 131u ^ sq.bpm10 * 7919u ^ ui.multi);
    s = sig_of(ui.mix, sizeof ui.mix, s ^ ui.shift);
    s = sig_of(&fxp, sizeof fxp, s);
    s = sig_of(sq_song, sizeof sq_song, s ^ ui.song_cur * 31u ^ sq.song_n * 17u ^ sq.song_mode);
    s ^= (sq.seg * 977u) ^ (sq.quant * 31u) ^ (sq.swing * 7u) ^ (sq.click * 3u) ^ sq_seg[sq.seg % SQ_NSEG].bars * 101u;
    if (sq.playing)
        s ^= ((sq.countin > 0 ? 0u : (sq.pos >> 16) / SQ_PPQ) + 1u) * 2654435761u ^ sq.recording * 5u ^ (sq.countin > 0) * 9u ^ sq.song_i * 13u;
    if (ui.msg[0] && (int32_t)(fm1_ms - ui.msg_until) < 0)
        s = sig_of(ui.msg, sizeof ui.msg, s ^ 0x55u);
    if (s != ui.sig_lcd || ui.force) { ui.sig_lcd = s; draw_lcd(); }
    for (i = 0; i < SP_NCH; i++)
        on |= (uint32_t)((int32_t)(fm1_ms - ui.chan_ms[i]) < 90 && sp_ch[i].on) << i;
    s = sig_of(ui.mix, sizeof ui.mix, on * 2654435761u ^ ui.shift * 3u ^ (ui.page == PG_HOME) * 5u);
    if (s != ui.sig_fad || ui.force) { ui.sig_fad = s; draw_faders(); }
    for (i = 0; i < 8u; i++)
        lit |= (uint32_t)((int32_t)(fm1_ms - ui.hit_ms[i]) < 120) << i;
    s = lit * 31u + ui.sel * 7919u + 1u;
    if (s != ui.sig_pad || ui.force) { ui.sig_pad = s; draw_pads(); }
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
    ui.msg_until = fm1_ms + 1200u;
}

/* ---- playing. SP_HIT starts a sound: on the FM-1 a ring to the audio ISR (zp12.c), on the host at once */
#ifndef SP_HIT
#define SP_HIT(k, vel, semis) sq_hit(k, vel, semis)
#endif
static void pad_hit_at(uint32_t k, uint32_t vel, int32_t semis)
{
    k %= SP_NSOUND;
    ui.sel = (uint8_t)k;
    ui.hit_ms[k % 8u] = fm1_ms;
    ui.chan_ms[sp_sound[k].chan % SP_NCH] = fm1_ms;
    SP_HIT(k, vel, semis);
}
static void pad_hit(uint32_t k, uint32_t vel) { pad_hit_at(k, vel, 0); }

/* the pad of key k (0 = F3 .. 26 = G5) in pad mode, or 0xFF (a black key) */
static uint32_t key_pad(uint32_t k)
{
    static const uint8_t WHITE_OF[27] = {0, 0xFF, 1, 0xFF, 2, 0xFF, 3, 4, 0xFF, 5, 0xFF, 6, 7, 0xFF, 8, 0xFF, 9,
                                         10, 0xFF, 11, 0xFF, 12, 0xFF, 13, 14, 0xFF, 15};   /* F3 .. G5 */
    return k < 27u && WHITE_OF[k] != 0xFFu ? ui.pair * 16u + WHITE_OF[k] : 0xFFu;   /* 1-8: A (C), 9-16: B (D) */
}

/* key k down; erase: LFO held (the pad's hits go: as the playhead passes, or at once when stopped) */
static void key_down(uint32_t k, int erase)
{
    uint32_t pad;
    if (k >= 27u)
        return;
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
            sq.req_wipe = (uint8_t)(pad + 1u);
            ui_say("ERASED");
        }
        return;
    }
    pad_hit(pad, 110);
}

/* KNOB n turned by d on the page */
static void knob(uint32_t n, int32_t d)
{
    sp_sound_t *s = &sp_sound[ui.sel];
    int32_t one = d > 0 ? 1 : -1;
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
        if (n == 1u) sq_seg[sq.seg % SQ_NSEG].bars = (uint8_t)sp_clamp(sq_seg[sq.seg % SQ_NSEG].bars + one, 1, 8);
        if (n == 2u) sq.quant = (uint8_t)sp_clamp(sq.quant + one, 0, 6);
        if (n == 3u) sq.swing = (uint8_t)sp_clamp(sq.swing + one, 0, 5);
        break;
    case PG_SONG:
        if (n == 0u) ui.song_cur = (uint8_t)sp_clamp(ui.song_cur + one, 0, sq.song_n < SQ_NSONG ? sq.song_n : SQ_NSONG - 1);
        if (n == 1u || n == 2u) {
            uint32_t i = ui.song_cur;
            if (i >= SQ_NSONG)
                break;
            if (i >= sq.song_n && !sq.playing) {          /* past the end: a new step (the segment edited, once) */
                sq_song[i].seg = sq.seg;
                sq_song[i].rep = 1;
                sq.song_n = (uint8_t)(i + 1u);
            }
            if (n == 1u) sq_song[i].seg = (uint8_t)sp_clamp(sq_song[i].seg + one, 0, SQ_NSEG - 1);
            else sq_song[i].rep = (uint8_t)sp_clamp(sq_song[i].rep + one, 1, 99);
        }
        if (n == 3u && !sq.playing)                       /* the song's length: steps after it are dropped */
            sq.song_n = (uint8_t)sp_clamp(sq.song_n + one, 0, SQ_NSONG);
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
static void page(uint32_t p) { ui.page = (uint8_t)p; ui_say(PG_NAME[p]); }

static void tap_tempo(void)                      /* ENV: the tempo of the last taps (two at least, < 2 s apart) */
{
    uint32_t i, n = 0, sum = 0;
    for (i = 3; i; i--) ui.tap_ms[i] = ui.tap_ms[i - 1u];
    ui.tap_ms[0] = fm1_ms;
    for (i = 0; i < 3u && ui.tap_ms[i + 1u] && ui.tap_ms[i] - ui.tap_ms[i + 1u] < 2000u; i++, n++)
        sum += ui.tap_ms[i] - ui.tap_ms[i + 1u];
    if (n && sum) {
        sq.bpm10 = (uint16_t)sp_clamp((int32_t)(600000u * n / sum), 400, 2400);
        ui_say("TAP");
    }
}

static void button(uint32_t b)
{
    switch (b) {
    case B_OCTUP: ui.pair = 1; ui_say("BANKS C + D"); break;
    case B_OCTDN: ui.pair = 0; ui_say("BANKS A + B"); break;
    case B_ARP: ui.multi ^= 1u; ui_say(ui.multi ? "MULTI PITCH ON" : "MULTI PITCH OFF"); break;
    case B_HOME: ui.page = PG_HOME; break;
    case B_EDIT: page(ui.page >= PG_SOUND && ui.page < PG_SFX ? ui.page + 1u : PG_SOUND); break;
    case B_FX: page(ui.page >= PG_CHO && ui.page < PG_REV ? ui.page + 1u : PG_CHO); break;
    case B_SEQ: page(PG_SEG); break;
    case B_SAVE:
        if (ui.page == PG_SONG) {                 /* on the SONG page: SAVE again toggles the song mode */
            sq.song_mode ^= 1u;
            ui_say(sq.song_mode ? "SONG MODE" : "SEGMENT MODE");
        } else {
            page(PG_SONG);
        }
        break;
    case B_GLO: page(PG_SETUP); break;
    case B_ENV: tap_tempo(); break;
    case B_PLAY: sq.req_play = sq.playing ? 2u : 1u; break;
    case B_REC:
        if (sq.playing)
            sq.recording ^= 1u;
        else
            sq.rec_arm ^= 1u;
        break;
    default: break;
    }
}

static void ui_init(void)
{
    uint32_t i;
    for (i = 0; i < SP_NCH; i++)
        ui.mix[i] = sp_mix[i] = 100;
    sq_init();
    ui.force = 1;
}
