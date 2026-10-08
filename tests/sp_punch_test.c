/* sp_punch.c on the host: ROLL repeats the slice since the grid, REVERSE plays the last beat backwards, TAPE STOP
 * comes to a halt, every change is click-free; idle it passes the mix untouched.
 *   cc -O2 -Wall -Wno-unused-function -Ifirmware/src -o build/host/sp_punch_test tests/sp_punch_test.c && build/host/sp_punch_test */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
static inline int32_t sp_clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
#define SQ_PPQ 96u
static struct { uint16_t bpm10; uint32_t pos; uint8_t playing; int32_t countin; } sq = {1200, 0, 1, 0};
#include "sp_punch.c"

#define H 128u
static int fails;
static void check(int ok, const char *what) { printf("%s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) fails++; }

static int32_t sig(uint32_t t) { return (int32_t)((t * 8u) % 32000u) - 16000; }   /* a slow saw (4000 samples): rising, its time in its value */
static uint32_t T;                                                  /* samples so far */
static int32_t out[1 << 20];
static void run(uint32_t halves)                                    /* the ISR's halves; the playhead moves along */
{
    int32_t o[2 * H];
    uint32_t h, i;
    for (h = 0; h < halves; h++) {
        for (i = 0; i < H; i++) o[2 * i] = o[2 * i + 1] = sig(T + i);
        sq.pos += (uint32_t)((uint64_t)H * sq.bpm10 * 96u * 65536u / 26460000u);
        sp_punch(o, H);
        for (i = 0; i < H; i++) out[T + i] = o[2 * i];
        T += H;
    }
}
static int32_t maxjump(uint32_t a, uint32_t b)
{
    int32_t m = 0;
    for (uint32_t t = a + 1; t < b; t++) { int32_t d = abs(out[t] - out[t - 1]); if (d > m && d < 12000) m = d;   /* (the saw's own reset, even halved by the 22 kHz ring, is not a click) */ }
    return m;
}

int main(void)
{
    uint32_t beat = 26460000u / sq.bpm10, t0, i, bad;              /* 120 BPM: 22050 */
    run(400);
    for (i = bad = 0; i < T; i++) bad += out[i] != sig(i);
    check(bad == 0, "idle: the mix passes untouched");

    pn.req = PN_ROLL16; t0 = T; run(200); pn.req = -1; run(20);
    {   /* after the first slice, out repeats with the period of a 1/16 */
        uint32_t len = beat / 4, a = t0 + 2 * len + 200, ok = 1;
        for (i = a; i < a + len && i + len < t0 + 200 * H - 100; i++) ok &= abs(out[i] - out[i + len]) <= 4;
        check(ok, "ROLL 1/16 repeats a 1/16 slice");
        check(maxjump(t0, t0 + 200 * H) < 2600, "ROLL: no clicks at the slice's seams");
    }
    pn.req = PN_REV; t0 = T; run(100); pn.req = -1; run(20);
    {   /* the saw rises: reversed, it falls */
        uint32_t a = t0 + 200, falls = 0, rises = 0;
        for (i = a; i < a + 2000; i++) { int32_t d = out[i + 1] - out[i]; if (d < 0 && d > -1000) falls++; else if (d > 0 && d < 1000) rises++; }
        check(falls > 10 * rises, "REVERSE plays backwards");
    }
    pn.req = PN_STOP; t0 = T; run(400);
    {
        int32_t m = 0;
        for (i = t0 + beat + 2000; i < T; i++) if (abs(out[i]) > m) m = abs(out[i]);
        check(m == 0, "TAPE STOP: silent after a beat");
    }
    pn.req = -1; t0 = T; run(40);
    for (i = t0 + 80, bad = 0; i < T; i++) bad += out[i] != sig(i);
    check(bad == 0, "released: the mix is back, untouched");
    printf("%s\n", fails ? "FAILED" : "all passed");
    return fails != 0;
}
