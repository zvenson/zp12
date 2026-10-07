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
    fm1_ms = 1000;
    ui_draw(); ppm("zp12-home");
    key_down(0, 0); key_down(4, 0); fm1_ms += 20; knob(2, -30); ui_draw(); ppm("zp12-hit");
    button(B_SEQ); fm1_ms += 2000; ui_draw(); ppm("zp12-seg");
    sq.playing = 1; sq.recording = 1; sq.pos = (SQ_BAR + 2u * SQ_PPQ) << 16; sq_seg[0].bars = 2; fm1_ms += 10; ui_draw(); ppm("zp12-rec");
    sq.playing = 0; sq.recording = 0; button(B_EDIT); button(B_EDIT); button(B_EDIT); button(B_EDIT); knob(3, 60); fm1_ms += 2000; ui_draw(); ppm("zp12-sfx");
    button(B_FX); button(B_FX); fm1_ms += 2000; ui_draw(); ppm("zp12-delay");
    printf("zp12 ui: screens written to %s\n", dir);
    return 0;
}
