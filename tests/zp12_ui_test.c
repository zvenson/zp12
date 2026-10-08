/* SPDX-License-Identifier: GPL-3.0-only */
/* zp12's screen on the host: the real UI code (firmware/src/sp_ui.c) on a framebuffer; writes PPMs to look at.
 *   cc -O2 -w -Ifirmware/src -Ifirmware/gen -Ibuild/gen -o build/host/zp12_ui_test tests/zp12_ui_test.c -lm
 *   build/host/zp12_ui_test build/host */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
static uint32_t fm1_ms;
static uint16_t fb[240 * 240];
static void lcd_sync(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{
    uint32_t i, j;
    for (j = 0; j < h; j++) for (i = 0; i < w; i++) if (x + i < 240u && y + j < 240u) fb[(y + j) * 240u + x + i] = p[j * w + i];
}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    uint16_t s = (uint16_t)((c >> 8) | (c << 8));
    for (j = 0; j < h; j++) for (i = 0; i < w; i++) if (x + i < 240u && y + j < 240u) fb[(y + j) * 240u + x + i] = s;
}
#define __attribute__(x)
#include "gfx.c"
#define SP_WITH_FX 1
#include "sp_core.c"
#include "sp_fx.c"
#include "zp12_kit.h"
#include "sp_seq.c"
#include "sp_ui.c"
static const char *dir;
static void ppm(const char *name)
{
    char p[256];
    FILE *f;
    uint32_t i;
    snprintf(p, sizeof p, "%s/%s.ppm", dir, name);
    f = fopen(p, "wb");
    fprintf(f, "P6\n240 240\n255\n");
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t v = (uint16_t)((fb[i] >> 8) | (fb[i] << 8));
        uint8_t rgb[3] = {(uint8_t)((v >> 11) * 255 / 31), (uint8_t)(((v >> 5) & 63) * 255 / 63), (uint8_t)((v & 31) * 255 / 31)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}
int main(int argc, char **argv)
{
    uint32_t i;
    dir = argc > 1 ? argv[1] : "build/host";
    for (i = 0; i < KIT_NWAVE; i++) { sp_wave[i].d = KIT_DATA + KIT_WAVE[i].off; sp_wave[i].n = KIT_WAVE[i].n; sp_wave[i].rate = 26040; }
    memcpy(sp_sound, KIT_PADS, sizeof sp_sound);
    ui_init();
    ui.back = 1;                                            /* (as zp12.c: pages back after 12 s) */
    for (i = 0; i < 27u; i++)                               /* the keys F3..G5: black where a piano has them */
        if ((white_of(i) == 0xFFu) != ((0x54Au >> ((i + 5u) % 12u)) & 1u)) { printf("zp12 ui: key %u black / white wrong FAIL\n", i); return 1; }
    fm1_ms = 1000;
    ui_draw(); ppm("zp12-home");
    key_down(0, 0); key_down(4, 0); fm1_ms += 20; knob(2, -30); ui_draw(); ppm("zp12-hit");
    button(B_SEQ); fm1_ms += 2000; ui_draw(); ppm("zp12-seg");
    sq.playing = 1; sq.recording = 1; sq.pos = (SQ_BAR + 2u * SQ_PPQ) << 16; sq_seg[0].bars = 2; fm1_ms += 10; ui_draw(); ppm("zp12-rec");
    sq.playing = 0; sq.recording = 0; button(B_EDIT); button(B_EDIT); button(B_EDIT); button(B_EDIT); knob(3, 60); fm1_ms += 2000; ui_draw(); ppm("zp12-sfx");
    button(B_FX); knob(0, -9); fm1_ms += 2000; ui_draw(); ppm("zp12-filter"); djf.v = 0; button(B_FX); button(B_FX); fm1_ms += 2000; ui_draw(); ppm("zp12-delay");
    fm1_ms += 13000; ui_draw(); ppm("zp12-back");           /* untouched 12 s: the faders again */
    if (ui.page != PG_HOME) { printf("zp12 ui: a page did not go back after 12 s FAIL\n"); return 1; }
    sq_insert(&sq_seg[0], 0, 0, 7, 0, 0, 0); sq_insert(&sq_seg[0], 96, 1, 7, 0, 0, sp_lock_of(&sp_sound[1])); sq_insert(&sq_seg[0], 48, 4, 5, 0, 0, 0);
    sq_insert(&sq_seg[0], 144, 4, 5, 0, 0, 0); sq_insert(&sq_seg[0], 240, 0, 7, 0, 0, 0); sq_seg[0].bars = 2;
    ui.sel = 0; ui.steps = 1; ui.force = 1; sq.playing = 1; sq.pos = (5u * 24u) << 16; fm1_ms += 10; ui_draw(); ppm("zp12-steps");
    ui.steps = 0; ui.force = 1; sq.playing = 0;            /* the sample: D1, WAVE */
    ui.pair = 1; key_down(21, 0); ui.sel = 24; page(PG_WAVE); fm1_ms += 2000; ui_draw(); ppm("zp12-wave");
    key_down(14, 0); ui.sel = 24; page(PG_HOME); fm1_ms += 20; ui_draw(); ppm("zp12-chord");
    sq.playing = 1; sq.recording = 0; sq.seg = 2; sq.pos = (SQ_BAR + 3u * SQ_PPQ) << 16; page(PG_SEG); fm1_ms += 2000; ui_draw(); ppm("zp12-loop");
    sq.playing = 0; ui.sel = 1; page(PG_SFX); fm1_ms += 2000; ui_draw(); ppm("zp12-sends");
    ui.sel = 0; page(PG_SETUP); fm1_ms += 2000; ui_draw(); ppm("zp12-setup");
    page(PG_HOME); ui.pair = 0; key_down(2, 0); fm1_ms += 2000; ui_preset(1); fm1_ms += 20; ui_draw(); ppm("zp12-preset");
    if (sp_sound[1].wave != (KIT_PADS[1].wave + 1u) % KIT_NWAVE) { printf("zp12 ui: PRESETS did not change the pad's sample FAIL\n"); return 1; }
    fm1_ms += 2000; sq.seg = 0;                            /* REC held: undone at 0.7 s, the bar, the loop cleared */
    ui_holds(1u << B_REC, 1u << B_REC, 0); fm1_ms += 800; ui_holds(1u << B_REC, 0, 0); fm1_ms += 600; ui_holds(1u << B_REC, 0, 0);
    ui_draw(); ppm("zp12-hold");
    fm1_ms += 800; ui_holds(1u << B_REC, 0, 0); sq_block(); ui_draw(); ppm("zp12-cleared");
    if (sq_seg[0].n || sq.rec_arm) { printf("zp12 ui: REC held did not clear the loop / undo the arm FAIL\n"); return 1; }
    ui_holds(0, 0, 1u << B_REC);
    ui_holds(1u << B_EDIT, 1u << B_EDIT, 0); button(B_EDIT); button(B_OCTDN); sq_block(); ui_holds(0, 0, 1u << B_EDIT);
    if (sq_seg[0].n != 5u || ui.page != PG_HOME) { printf("zp12 ui: EDIT + OCT- did not undo FAIL\n"); return 1; }
    fm1_ms += 2000; ui_holds(1u << B_SAVE, 1u << B_SAVE, 0); key_down(3, 0); sq_block(); ui_holds(0, 0, 1u << B_SAVE); ui_draw(); ppm("zp12-saveloop");
    if (sq_seg[1].n != 5u || ui.save_req != 1u) { printf("zp12 ui: SAVE + a black key did not save the loop FAIL\n"); return 1; }
    page(PG_SEG); ui.shift = 0; i = ui.mix[4]; button(B_SEL); knob(0, -3); fm1_ms += 20; ui_draw(); ppm("zp12-sel");
    if (!ui.shift || ui.page != PG_HOME || ui.mix[4] == i || sq.seg != 0u) { printf("zp12 ui: SEL on the LOOP page did not give the faders 5-8 FAIL\n"); return 1; }
    {   /* lit, a page open: the knobs are the page's, the faders stay */
        uint8_t m = ui.mix[4];
        page(PG_SOUND); knob(0, 1);
        if (ui.mix[4] != m || ui.page != PG_SOUND) { printf("zp12 ui: a page with SEL lit FAIL\n"); return 1; }
    }
    { uint32_t l; ui_leds(&l, &i, &i); if (!((l >> B_SEL) & 1u)) { printf("zp12 ui: SEL not lit FAIL\n"); return 1; } }
    button(B_SEL);                                          /* again: 1-4, and the faders */
    if (ui.shift || ui.page != PG_HOME) { printf("zp12 ui: SEL again did not give 1-4 FAIL\n"); return 1; }
    {   /* an own sample (the web editor's): PRESETS finds it after the kit, shows its name, skips the empty places */
        static uint8_t snd[300];
        sp_wave[KIT_NWAVE + 3].d = snd; sp_wave[KIT_NWAVE + 3].n = 200; sp_wave[KIT_NWAVE + 3].rate = 26040;
        strcpy(ui_uname[3], "MY KICK"); ui_uflags[3] = 1;
        page(PG_HOME); ui.sel = 0; sp_sound[0].wave = KIT_NWAVE - 1; sp_sound[0].flags = 0;
        ui_preset(1); fm1_ms += 20; ui_draw(); ppm("zp12-own");
        if (sp_sound[0].wave != KIT_NWAVE + 3 || !(sp_sound[0].flags & SPF_33) || strcmp(sound_name(0), "MY KICK")) { printf("zp12 ui: PRESETS did not find the own sample FAIL\n"); return 1; }
        ui_preset(1);
        if (sp_sound[0].wave != 0u) { printf("zp12 ui: PRESETS did not skip the empty places FAIL\n"); return 1; }
        ui_preset(-1);
        if (sp_sound[0].wave != KIT_NWAVE + 3) { printf("zp12 ui: PRESETS back did not find the own sample FAIL\n"); return 1; }
        sp_wave[KIT_NWAVE + 5].d = snd; sp_wave[KIT_NWAVE + 5].n = 100; sp_wave[KIT_NWAVE + 5].rate = 26040;
        strcpy(ui_uname[5], "BREAK"); ui_uflags[5] = 2; sp_sound[0].tune = -3;
        ui_preset(1);                                      /* stored at double speed: an octave down on the pad ... */
        if (sp_sound[0].wave != KIT_NWAVE + 5 || sp_sound[0].tune != -12) { printf("zp12 ui: a x2 sample did not set TUNE -12 FAIL\n"); return 1; }
        ui_preset(1);                                      /* ... and back to 0 on a normal one */
        if (sp_sound[0].wave != 0u || sp_sound[0].tune != 0) { printf("zp12 ui: leaving a x2 sample did not reset TUNE FAIL\n"); return 1; }
    }
    page(PG_FILT); ui.back = 4; fm1_ms += 120000; ui_draw();     /* BACK OFF: the page stays ... */
    if (ui.page != PG_FILT) { printf("zp12 ui: BACK OFF did not keep the page FAIL\n"); return 1; }
    ui.back = 2; ui.touch_ms = fm1_ms; fm1_ms += 20000; ui_draw();  /* ... 30 s: still there after 20 ... */
    if (ui.page != PG_FILT) { printf("zp12 ui: BACK 30 s went back after 20 s FAIL\n"); return 1; }
    fm1_ms += 11000; ui_draw();                                       /* ... and back after 31 */
    if (ui.page != PG_HOME) { printf("zp12 ui: BACK 30 s did not go back FAIL\n"); return 1; }
    printf("zp12 ui: screens written to %s\n", dir);
    return 0;
}
