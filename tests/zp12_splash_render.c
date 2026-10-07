/* SPDX-License-Identifier: GPL-3.0-only */
/* zp12's start (sp_ui.c ui_splash) on the host: a frame every 40 ms as PPMs and its drums as a WAV, to look at
 * and listen to (ffmpeg makes a video of them).
 *   cc -O2 -w -Ifirmware/src -Ifirmware/gen -Ibuild/gen -o build/host/zp12_splash tests/zp12_splash_render.c -lm
 *   build/host/zp12_splash build/host/splash */
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
int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build/host/splash";
    uint32_t i, t, prev = 0, f = 0, k, frames = 0;
    char p[256];
    FILE *w;
    for (i = 0; i < KIT_NWAVE; i++) { sp_wave[i].d = KIT_DATA + KIT_WAVE[i].off; sp_wave[i].n = KIT_WAVE[i].n; sp_wave[i].rate = 26040; }
    memcpy(sp_sound, KIT_PADS, sizeof sp_sound);
    ui_init();
    snprintf(p, sizeof p, "%s/splash.wav", dir);
    w = fopen(p, "wb");
    fwrite("RIFF\0\0\0\0WAVEfmt \x10\0\0\0\x01\0\x02\0\x44\xac\0\0\x10\xb1\x02\0\x04\0\x10\0data\0\0\0\0", 1, 44, w);
    lcd_fill(0, 0, 240, 240, P_NAVY);
    for (t = 0; t <= SPLASH_MS + 400u; t += 40u, f++) {
        FILE *pp;
        for (k = 0; k < 4u; k++)
            if (SPLASH_HIT[k].t > prev && SPLASH_HIT[k].t <= t) sp_trigger(SPLASH_HIT[k].pad, SPLASH_HIT[k].vel);
        prev = t;
        ui_splash(t < SPLASH_MS ? t : SPLASH_MS);
        snprintf(p, sizeof p, "%s/%04u.ppm", dir, f);
        pp = fopen(p, "wb");
        fprintf(pp, "P6\n240 240\n255\n");
        for (i = 0; i < 240u * 240u; i++) {
            uint16_t v = (uint16_t)((fb[i] >> 8) | (fb[i] << 8));
            uint8_t rgb[3] = {(uint8_t)((v >> 11) * 255 / 31), (uint8_t)(((v >> 5) & 63) * 255 / 63), (uint8_t)((v & 31) * 255 / 31)};
            fwrite(rgb, 1, 3, pp);
        }
        fclose(pp);
        for (i = 0; i < 40u * 44100u / 1000u / SP_BLK + (f % 4u == 0u); i++) {   /* 40 ms of sound (55.1 blocks) */
            int32_t o[2 * SP_BLK];
            uint32_t j;
            memset(o, 0, sizeof o);
            sp_render(o);
            for (j = 0; j < 2u * SP_BLK; j++) { int32_t v = o[j] >> 1; int16_t q = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); fwrite(&q, 2, 1, w); }
            frames += SP_BLK;
        }
    }
    fseek(w, 4, SEEK_SET); { uint32_t v = 36u + frames * 4u; fwrite(&v, 4, 1, w); }
    fseek(w, 40, SEEK_SET); { uint32_t v = frames * 4u; fwrite(&v, 4, 1, w); }
    fclose(w);
    printf("splash: %u frames, %.2f s of sound\n", f, frames / 44100.0);
    return 0;
}
