/* SPDX-License-Identifier: GPL-3.0-only */
/* zp12's USB link (firmware/src/sp_link.c) on the host, as the FM-1 the web tools talk to: a 1 MiB flash in RAM
 * (sloopDX's banks and zp12's rooms filled with a pattern), SysEx messages as hex lines on stdin, the replies as
 * hex lines on stdout. tests/zp12_link_test.mjs drives it with web/zp12link.js.
 *   cc -O2 -Wall -Wno-unused-function -Ifirmware/src -o build/host/zp12_link_host tests/zp12_link_host.c */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static uint32_t fm1_ms = 1000;
static uint8_t flash[0x100000], flash_ok = 1;
static uint32_t n_erase, n_write;
#define FL_IN(off, n, lo, hi) ((uint32_t)(off) >= (lo) && (uint32_t)(off) <= (hi) && (uint32_t)(n) <= (hi) - (uint32_t)(off))
static int st_read(uint32_t off, void *d, uint32_t n) { if (off + n > sizeof flash) return -1; memcpy(d, flash + off, n); return 0; }
static int fl_erase4k_quiet(uint32_t off, uint32_t *took) { (void)took; memset(flash + off, 0xFF, 4096); n_erase++; return 0; }
static int fl_write(uint32_t off, const uint8_t *s, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) flash[off + i] &= s[i];          /* (NOR: program only clears bits) */
    n_write++;
    return 0;
}
static void fm1_wdt_feed(void) {}
static void usb_retry(uint32_t t) { (void)t; fm1_ms += 100; }
static void fm1_reboot(void) { printf("REBOOT\n"); fflush(stdout); }
static void ui_say(const char *m) { (void)m; }
enum { RQ_STOP = 2 };
static void sq_post(uint32_t op, uint32_t pad, uint32_t arg) { (void)op; (void)pad; (void)arg; }
static const char ZP12_VERSION[] = "1.3";

static uint32_t ota_pack7(const uint8_t *in, uint32_t n, uint8_t *out)   /* (as firmware/src/ota.c) */
{
    uint32_t acc = 0, nb = 0, o = 0;
    while (n--) {
        acc |= (uint32_t)*in++ << nb;
        nb += 8u;
        while (nb >= 7u) { out[o++] = (uint8_t)(acc & 0x7Fu); acc >>= 7; nb -= 7u; }
    }
    if (nb) out[o++] = (uint8_t)(acc & 0x7Fu);
    return o;
}
static uint32_t ota_unpack7(const uint8_t *in, uint32_t n, uint8_t *out, uint32_t max)
{
    uint32_t acc = 0, nb = 0, o = 0;
    while (n--) {
        acc |= (uint32_t)(*in++ & 0x7Fu) << nb;
        nb += 7u;
        if (nb >= 8u) { if (o < max) out[o] = (uint8_t)acc; o++; acc >>= 8; nb -= 8u; }
    }
    return o;
}
static uint8_t frame_buf[640];
static uint32_t frame_len, frame_ready;
static int ota_frame_get(const uint8_t **p, uint32_t *n) { if (!frame_ready) return 0; *p = frame_buf; *n = frame_len; return 1; }
static void ota_frame_done(void) { frame_ready = 0; }
static int ota_wire_send(const uint8_t *p, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) printf("%02x", p[i]);
    printf("\n");
    fflush(stdout);
    return 0;
}
#include "sp_link.c"

int main(void)
{
    char line[4096];
    uint32_t a;
    memset(flash, 0xFF, sizeof flash);
    for (a = 0xA0000; a < 0xA3000; a++) flash[a] = (uint8_t)(a * 7u);       /* sloopDX: two banks' worth */
    for (a = 0xC4000; a < 0xCC000; a++) flash[a] = (uint8_t)(a * 13u + 1u);  /* zp12's store */
    for (a = 0xEA000; a < 0xEB800; a++) flash[a] = (uint8_t)(a >> 3);        /* a sample */
    for (a = 0xDC000; a < 0xDD000; a++) flash[a] = 0x5A;                     /* sloopDX's presets: never touched */
    while (fgets(line, sizeof line, stdin)) {
        uint32_t n = 0, i, v;
        if (!strncmp(line, "DUMP ", 5)) {                                   /* DUMP addr len: the flash itself */
            uint32_t ad = (uint32_t)strtoul(line + 5, 0, 16), ln = (uint32_t)strtoul(strchr(line + 5, ' ') + 1, 0, 16);
            for (i = 0; i < ln; i++) printf("%02x", flash[ad + i]);
            printf("\n");
            fflush(stdout);
            continue;
        }
        if (!strncmp(line, "STATS", 5)) { printf("STATS %u %u\n", n_erase, n_write); fflush(stdout); continue; }
        for (i = 0; line[i] && line[i + 1] && line[i] != '\n'; i += 2) {
            sscanf(line + i, "%2x", &v);
            if (v == 0xF0) n = 0;
            else if (v == 0xF7) { frame_len = n; frame_ready = 1; zl_service(); frame_ready = 0; }
            else if (n < sizeof frame_buf) frame_buf[n++] = (uint8_t)v;
        }
        fm1_ms += 5;
    }
    return 0;
}
