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

enum { PG_SOUND, PG_TRUNC, PG_OUT, PG_MIX1, PG_MIX2, PG_N };
static struct {
    uint8_t pair;                               /* 0: banks A + B on the keys, 1: C + D */
    uint8_t sel;                                /* the sound the knobs edit, 0..31 */
    uint8_t page;
    uint8_t multi;                              /* MULTI PITCH */
    uint8_t mix[SP_NCH];
    uint16_t bpm10;                             /* tempo x 10 */
    uint8_t playing, rec;
    uint32_t hit_ms[8];                         /* when each on-screen pad was last hit (its light) */
    uint32_t chan_ms[SP_NCH];
    char msg[24];
    uint32_t msg_until;
    uint32_t sig_lcd, sig_fad, sig_pad, sig_led;   /* what each region shows now */
    uint8_t force;
} ui;

static const char *const PG_NAME[PG_N] = {"SOUND", "TRUNC", "OUT", "MIX 1-4", "MIX 5-8"};

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

static void draw_lcd(void)                      /* the LCD: what is selected and its values */
{
    char l1[32], l2[32], *p;
    const sp_sound_t *s = &sp_sound[ui.sel];
    cv_begin(232, 40, P_FRAME);
    cv_rect(2, 0, 228, 40, RGB(70, 76, 66));    /* the bezel */
    cv_rect(5, 3, 222, 34, P_LCD);
    if (ui.msg[0] && (int32_t)(fm1_ms - ui.msg_until) < 0) {
        cv_text_on(9, 3, &FONT_S, ui.msg, P_LCDINK, P_LCD);
    } else {
        pad_label(l1, ui.sel);
        p = cat(l1 + 2, " ");
        p = cat(p, sound_name(ui.sel));
        while (p < l1 + 15) *p++ = ' ';
        put_int(p, ui.bpm10 / 10u, 3, 0);
        p = cat(p + 3, ".");
        put_int(p, ui.bpm10 % 10u, 1, 0);
        cv_text_on(9, 3, &FONT_S, l1, P_LCDINK, P_LCD);
    }
    p = l2;
    switch (ui.page) {
    case PG_SOUND:
        p = cat(p, "T"); put_int(p, s->tune, 2, 1); p += 3;
        p = cat(p, " F"); put_int(p, s->fine, 2, 1); p += 3;
        p = cat(p, " D"); put_int(p, s->decay, 3, 0); p += 3;
        p = cat(p, " L"); put_int(p, s->level, 3, 0);
        break;
    case PG_TRUNC:
        p = cat(p, "S"); put_int(p, s->start, 4, 0); p += 4;
        p = cat(p, " E"); put_int(p, s->end, 4, 0); p += 4;
        p = cat(p, (s->flags & SPF_REVERSE) ? " REV" : " FWD");
        p = cat(p, (s->flags & SPF_33) ? " 33" : " 45");
        break;
    case PG_OUT:
        p = cat(p, "CH"); put_int(p, s->chan + 1, 1, 0); p += 1;
        p = cat(p, " P"); put_int(p, s->pan, 2, 1); p += 3;
        if (s->chan < 2u) {
            p = cat(p, " C"); put_int(p, s->cut, 3, 0); p += 3;
            p = cat(p, " Q"); put_int(p, s->reso, 3, 0);
        } else {
            p = cat(p, s->chan < 6u ? " FIXED LP" : " NO FILTER");
        }
        break;
    default: {
        uint32_t i, b = ui.page == PG_MIX1 ? 0u : 4u;
        for (i = 0; i < 4u; i++) {
            p = cat(p, i ? " " : "");
            put_int(p, ui.mix[b + i], 3, 0);
            p += 3;
        }
        p = cat(p, ui.page == PG_MIX1 ? " CH1-4" : " CH5-8");
        break;
    }
    }
    if (ui.multi)
        cat(l2 + 20 > p ? p : l2 + 20, " MULTI");
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
        if (i == sp_sound[ui.sel].chan)
            cv_rect(cx - 9, 70 + 12, 19, 1, P_FRAME);
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

static void draw_leds(void)                     /* banks A-D (the pair on the keys), MULTI, RUN, REC */
{
    uint32_t i;
    cv_begin(232, 22, P_NAVY);
    for (i = 0; i < 4u; i++) {
        char b[2] = {(char)('A' + i), 0};
        led(4 + (int32_t)i * 26, 8, i / 2u == ui.pair);
        cv_text_on(12 + (int32_t)i * 26, 3, &FONT_S, b, P_FRAME, P_NAVY);
    }
    led(110, 8, ui.multi);
    cv_text_on(118, 3, &FONT_S, "M", P_FRAME, P_NAVY);
    led(146, 8, ui.playing);
    cv_text_on(154, 3, &FONT_S, "RUN", P_FRAME, P_NAVY);
    led(190, 8, ui.rec);
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
    s = sig_of(&sp_sound[ui.sel], sizeof(sp_sound_t), 2166136261u ^ ui.sel * 7u ^ ui.page * 131u ^ ui.bpm10 * 7919u ^ ui.multi);
    s = sig_of(ui.mix, sizeof ui.mix, s);
    if (ui.msg[0] && (int32_t)(fm1_ms - ui.msg_until) < 0)
        s = sig_of(ui.msg, sizeof ui.msg, s ^ 0x55u);
    if (s != ui.sig_lcd || ui.force) { ui.sig_lcd = s; draw_lcd(); }
    for (i = 0; i < SP_NCH; i++)
        on |= (uint32_t)((int32_t)(fm1_ms - ui.chan_ms[i]) < 90 && sp_ch[i].on) << i;
    s = sig_of(ui.mix, sizeof ui.mix, on * 2654435761u ^ sp_sound[ui.sel].chan);
    if (s != ui.sig_fad || ui.force) { ui.sig_fad = s; draw_faders(); }
    for (i = 0; i < 8u; i++)
        lit |= (uint32_t)((int32_t)(fm1_ms - ui.hit_ms[i]) < 120) << i;
    s = lit * 31u + ui.sel * 7919u + 1u;
    if (s != ui.sig_pad || ui.force) { ui.sig_pad = s; draw_pads(); }
    s = ui.pair + ui.multi * 2u + ui.playing * 4u + ui.rec * 8u + 1u;
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
#define SP_HIT(k, vel, semis) sp_trigger_at(k, vel, semis)
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

/* key k (0 = F3 .. 26 = G5) down */
static void key_down(uint32_t k)
{
    static const uint8_t WHITE_OF[27] = {0, 0xFF, 1, 0xFF, 2, 0xFF, 3, 4, 0xFF, 5, 0xFF, 6, 7, 0xFF, 8, 0xFF, 9,
                                         10, 0xFF, 11, 0xFF, 12, 0xFF, 13, 14, 0xFF, 15};   /* F3 .. G5 */
    uint32_t w;
    if (k >= 27u)
        return;
    if (ui.multi) {                              /* MULTI PITCH: the sound, F4 (key 12) as written */
        pad_hit_at(ui.sel, 110, (int32_t)k - 12);
        return;
    }
    w = WHITE_OF[k];
    if (w == 0xFFu)
        return;
    pad_hit(ui.pair * 16u + w, 110);             /* keys 1-8: bank A (C), 9-16: B (D) */
}

/* KNOB n turned by d on the page */
static void knob(uint32_t n, int32_t d)
{
    sp_sound_t *s = &sp_sound[ui.sel];
    switch (ui.page) {
    case PG_SOUND:
        if (n == 0u) s->tune = (int8_t)sp_clamp(s->tune + d, -24, 12);
        if (n == 1u) s->fine = (int8_t)sp_clamp(s->fine + d, -50, 50);
        if (n == 2u) s->decay = (uint8_t)sp_clamp(s->decay + d, 0, 127);
        if (n == 3u) s->level = (uint8_t)sp_clamp(s->level + d, 0, 127);
        break;
    case PG_TRUNC:
        if (n == 0u) s->start = (uint16_t)sp_clamp(s->start + d * 4, 0, s->end - 10);
        if (n == 1u) s->end = (uint16_t)sp_clamp(s->end + d * 4, s->start + 10, 1000);
        if (n == 2u && d) s->flags = (uint8_t)(d > 0 ? s->flags | SPF_REVERSE : s->flags & ~SPF_REVERSE);
        if (n == 3u && d) s->flags = (uint8_t)(d > 0 ? s->flags | SPF_33 : s->flags & ~SPF_33);
        break;
    case PG_OUT:
        if (n == 0u && d) s->chan = (uint8_t)sp_clamp(s->chan + (d > 0 ? 1 : -1), 0, SP_NCH - 1);
        if (n == 1u) s->pan = (int8_t)sp_clamp(s->pan + d, -64, 63);
        if (n == 2u) s->cut = (uint8_t)sp_clamp(s->cut + d, 0, 127);
        if (n == 3u) s->reso = (uint8_t)sp_clamp(s->reso + d, 0, 127);
        break;
    default: {
        uint32_t c = (ui.page == PG_MIX1 ? 0u : 4u) + n;
        ui.mix[c] = (uint8_t)sp_clamp(ui.mix[c] + d, 0, 127);
        sp_mix[c] = ui.mix[c];
        break;
    }
    }
}

/* buttons: B_* are the printed labels (panel ids as SLOOP's PANEL_DEFAULT) */
enum { B_OCTDN = 0, B_OCTUP = 1, B_FX = 2, B_SCL = 3, B_ENV = 4, B_LFO = 5, B_EDIT = 6, B_GLO = 7, B_HOME = 8,
       B_SAVE = 9, B_ARP = 10, B_SEQ = 11, B_PLAY = 12, B_REC = 13 };
static void button(uint32_t b)
{
    switch (b) {
    case B_OCTUP: ui.pair = 1; ui_say("BANKS C + D"); break;
    case B_OCTDN: ui.pair = 0; ui_say("BANKS A + B"); break;
    case B_ARP: ui.multi ^= 1u; ui_say(ui.multi ? "MULTI PITCH ON" : "MULTI PITCH OFF"); break;
    case B_EDIT: ui.page = ui.page < PG_OUT ? (uint8_t)(ui.page + 1u) % (PG_OUT + 1u) : PG_SOUND; ui_say(PG_NAME[ui.page]); break;
    case B_GLO: ui.page = ui.page == PG_MIX1 ? PG_MIX2 : PG_MIX1; ui_say(PG_NAME[ui.page]); break;
    case B_HOME: ui.page = PG_SOUND; break;
    default: break;
    }
}

static void ui_init(void)
{
    uint32_t i;
    for (i = 0; i < SP_NCH; i++)
        ui.mix[i] = sp_mix[i] = 100;
    ui.bpm10 = 900;
    ui.force = 1;
}
