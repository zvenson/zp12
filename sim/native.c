/* SPDX-License-Identifier: GPL-3.0-only */
/* The simulator natively (sim/sim.c on the host, ucontext for the main loop): a script presses keys over time,
 * out come the sound (WAV) and the screen (PPM at "shot"). tests/sim_test.mjs runs the same script through the
 * wasm build and compares the two sample for sample, pixel for pixel.
 *   cc -O2 -w -fno-stack-protector -DSIM_NATIVE -Isim/hal -Ifirmware/src -Ifirmware/gen -Ibuild/gen \
 *      -o build/host/zp12-sim sim/native.c
 *   build/host/zp12-sim OUT script.txt     (-> OUT.wav, OUT-<ms>.ppm)
 * Script lines: "T keys MASK" (bit k = key k, 0 = F3), "T btn ID" (a tap), "T btndown ID", "T btnup ID",
 * "T enc ID N", "T midi ST D1 D2", "T master V" (0..1023), "T shot", "T end". (-fno-stack-protector: ui_splash
 * writes past its char b[24], see the report in tests/sim_test.mjs.) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define SIM_NATIVE 1
#include "sim.c"

int main(int argc, char **argv)
{
    static char line[256], what[16], name[512];
    const char *out = argc > 1 ? argv[1] : "build/host/zp12-sim";
    FILE *f = argc > 2 ? fopen(argv[2], "r") : NULL, *w;
    static struct { uint32_t t; char w[16]; long a, b, c; } ev[4096];
    uint32_t n = 0, ei = 0, end = 6000, nf = 0, cap = 0, i;
    int16_t *wav = NULL;
    if (argc > 2 && !f) { perror(argv[2]); return 1; }
    while (f && fgets(line, sizeof line, f) && n < 4096u) {
        if (line[0] == '#' || sscanf(line, "%u %15s %li %li %li", &ev[n].t, what, &ev[n].a, &ev[n].b, &ev[n].c) < 2) continue;
        snprintf(ev[n].w, sizeof ev[n].w, "%s", what);
        if (!strcmp(what, "end")) { end = ev[n].t; continue; }
        n++;
    }
    if (f) fclose(f);
    if (sim_init(44100)) return 1;
    while (sim_now() < end) {
        while (ei < n && ev[ei].t <= sim_now() + 1u) {   /* (as tests/sim_test.mjs: before the millisecond's pass) */
            const char *e = ev[ei].w;
            if (!strcmp(e, "keys")) for (i = 0; i < 27u; i++) sim_key(i, (uint32_t)(ev[ei].a >> i) & 1u);
            else if (!strcmp(e, "btn")) { sim_button((uint32_t)ev[ei].a, 1); sim_button((uint32_t)ev[ei].a, 0); }
            else if (!strcmp(e, "btndown")) sim_button((uint32_t)ev[ei].a, 1);
            else if (!strcmp(e, "btnup")) sim_button((uint32_t)ev[ei].a, 0);
            else if (!strcmp(e, "enc")) sim_enc((uint32_t)ev[ei].a, (int32_t)ev[ei].b);
            else if (!strcmp(e, "midi")) sim_midi((uint32_t)ev[ei].a, (uint32_t)ev[ei].b, (uint32_t)ev[ei].c);
            else if (!strcmp(e, "master")) sim_master((uint32_t)ev[ei].a);
            else if (!strcmp(e, "shot")) {
                snprintf(name, sizeof name, "%s-%u.ppm", out, ev[ei].t);
                if (!(w = fopen(name, "wb"))) { perror(name); return 1; }
                fprintf(w, "P6 240 240 255\n");
                for (i = 0; i < 240u * 240u; i++) {
                    uint16_t c = sim_fb[i];
                    fputc((c >> 11) * 255 / 31, w); fputc((c >> 5 & 63) * 255 / 63, w); fputc((c & 31) * 255 / 31, w);
                }
                fclose(w);
            }
            ei++;
        }
        sim_render(1);
        if (nf == cap) { cap = cap ? cap * 2u : 65536u; wav = realloc(wav, cap * 4u); }
        wav[2u * nf] = (int16_t)(out_l[0] * 32768.0f);
        wav[2u * nf + 1u] = (int16_t)(out_r[0] * 32768.0f);
        nf++;
    }
    snprintf(name, sizeof name, "%s.wav", out);
    if (!(w = fopen(name, "wb"))) { perror(name); return 1; }
    {
        uint32_t bytes = nf * 4u, hd[] = {16, 0x00020001u, 44100u, 44100u * 4u, 0x00100004u}, riff = bytes + 36u;
        fwrite("RIFF", 1, 4, w); fwrite(&riff, 4, 1, w); fwrite("WAVEfmt ", 1, 8, w); fwrite(hd, 4, 5, w);
        fwrite("data", 1, 4, w); fwrite(&bytes, 4, 1, w); fwrite(wav, 4, nf, w);
    }
    fclose(w);
    printf("zp12 sim: %u frames -> %s, %u ms, page %u, playing %u, loop hits %u\n", nf, name, sim_now(), sim_page(),
           sim_playing(), sim_loop_events());
    return 0;
}
