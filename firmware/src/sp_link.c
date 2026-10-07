/* SPDX-License-Identifier: GPL-3.0-only */
/* zp12's USB link for the web tools (backup now, the sample editor next): a few raw flash commands, the
 * logic in the browser. A message is F0 pack7(7D 'Z' 'P' cmd addr:3 len:2 data:len sum) F7, packed as the
 * updater's (ota.c: ota_pack7), sum = ~(the bytes from cmd on); the reply has the same header and cmd, then
 * rc, addr, len, data, sum.
 *   1 HELLO        -> data: protocol, "zp12 X.Y", 0, the kit's waves
 *   2 READ a n     -> n <= 512 bytes of flash from 0x93000 up
 *   3 ERASE a      -> the 4 KiB sector at a          } only where zp12 keeps things: its store and samples,
 *   4 WRITE a data -> programmed, read back, compared } and sloopDX's DX7 banks + MY KIT (0xA0000..0xC3FFF)
 *   5 HOLD         -> the sequencer stops; no autosave while the link is busy (10 s from the last message)
 *   6 REBOOT       -> after the reply (a restored store is read at the start)
 *   7 RELOAD       -> the samples' directory read again (sp_samples.c); data: how many there are
 *   8 ASSIGN a     -> pad a & 31 plays wave (a >> 8) & 63, heard at once (the web editor's "on a pad")
 *   9 PADS         -> data: the wave of each of the 32 pads (0xFF none), then the kit's names, each ended by 0 */
#define ZL_MAX 512u
#define ZL_PROTO 3u                              /* 2: RELOAD, ASSIGN (the sample editor); 3: PADS */
static uint8_t zl_dec[16 + ZL_MAX], zl_wire[32 + (16 + ZL_MAX) * 8u / 7u];
static uint8_t zl_back[ZL_MAX];
static uint32_t zl_last;                         /* fm1_ms of the last message (0: never) */

static int zl_busy(void) { return zl_last && (uint32_t)(fm1_ms - zl_last) < 10000u; }

static int zl_writable(uint32_t a, uint32_t n)
{
    return FL_IN(a, n, 0xA0000u, 0xDC000u) || FL_IN(a, n, 0xE5000u, 0xE9000u) || FL_IN(a, n, 0xEA000u, 0xFC000u);
}

static void zl_reply(uint32_t cmd, uint32_t rc, uint32_t a, const uint8_t *d, uint32_t n)
{
    uint8_t *m = zl_dec;
    uint32_t i, s = 0, w;
    m[0] = 0x7D; m[1] = 'Z'; m[2] = 'P'; m[3] = (uint8_t)cmd; m[4] = (uint8_t)rc;
    m[5] = (uint8_t)a; m[6] = (uint8_t)(a >> 8); m[7] = (uint8_t)(a >> 16); m[8] = (uint8_t)n; m[9] = (uint8_t)(n >> 8);
    for (i = 0; i < n; i++) m[10 + i] = d[i];
    for (i = 3; i < 10u + n; i++) s += m[i];
    m[10 + n] = (uint8_t)~s;
    zl_wire[0] = 0xF0;
    w = 1u + ota_pack7(m, 11u + n, zl_wire + 1);
    zl_wire[w++] = 0xF7;
    ota_wire_send(zl_wire, w);
}

/* main loop: a frame for us is taken, any other is left to ota_service */
static void zl_service(void)
{
    const uint8_t *p;
    uint32_t n, d, cmd, a, len, i, s = 0, rc = 0;
    uint8_t h[3];
    if (!ota_frame_get(&p, &n))
        return;
    if (n < 4u || ota_unpack7(p, 4, h, 3) < 3u || h[0] != 0x7D || h[1] != 'Z' || h[2] != 'P')
        return;
    d = ota_unpack7(p, n, zl_dec, sizeof zl_dec);
    ota_frame_done();
    if (d < 10u || d > sizeof zl_dec)
        return;
    for (i = 3; i + 1u < d; i++) s += zl_dec[i];
    cmd = zl_dec[3];
    a = zl_dec[4] | (uint32_t)zl_dec[5] << 8 | (uint32_t)zl_dec[6] << 16;
    len = zl_dec[7] | (uint32_t)zl_dec[8] << 8;
    if ((uint8_t)~s != zl_dec[d - 1u] || len > ZL_MAX || (cmd == 4u && len + 10u != d))
        return;                                  /* damaged: no reply, the host asks again */
    zl_last = fm1_ms ? fm1_ms : 1u;
    if (!flash_ok && cmd >= 2u && cmd <= 4u) {
        zl_reply(cmd, 9, a, 0, 0);
        return;
    }
    switch (cmd) {
    case 1: {
        static const uint8_t HI[] = {ZL_PROTO, 'z', 'p', '1', '2', ' '};
        uint8_t b[16];
        for (i = 0; i < sizeof HI; i++) b[i] = HI[i];
        for (n = 0; ZP12_VERSION[n] && i + 2u < sizeof b; n++) b[i++] = (uint8_t)ZP12_VERSION[n];
        b[i++] = 0;
        b[i++] = KIT_NWAVE;                      /* (the own samples are waves KIT_NWAVE + slot) */
        zl_reply(1, 0, 0, b, i);
        return;
    }
    case 2:
        if (a < 0x93000u || !FL_IN(a, len, 0x93000u, 0x100000u) || st_read(a, zl_back, len))
            rc = 1, len = 0;
        zl_reply(2, rc, a, zl_back, len);
        return;
    case 3: {
        uint32_t took;
        rc = (a & 0xFFFu) || !zl_writable(a, 0x1000u) ? 2u : fl_erase4k_quiet(a, &took) ? 3u : 0u;
        break;
    }
    case 4:
        if (!zl_writable(a, len))
            rc = 2;
        else if (fl_write(a, zl_dec + 9, len) || st_read(a, zl_back, len))
            rc = 3;
        else
            for (i = 0; i < len && !rc; i++)
                rc = zl_back[i] != zl_dec[9 + i] ? 4u : 0u;
        break;
    case 5:
        sq_post(RQ_STOP, 0, 0);
        ui_say("USB BACKUP");
        break;
    case 6:
        zl_reply(6, 0, 0, 0, 0);
        for (i = fm1_ms; (uint32_t)(fm1_ms - i) < 80u;) {
            fm1_wdt_feed();                      /* (the reply on its way) */
            usb_retry(fm1_ms);
        }
        fm1_reboot();
        return;
    case 7: {
        uint8_t b[1] = {0};
        zu_load();
        for (i = 0; i < SP_NUSER; i++) b[0] += sp_wave[KIT_NWAVE + i].n != 0u;
        ui_say(b[0] ? "SAMPLES LOADED" : "NO SAMPLES");
        zl_reply(7, 0, a, b, 1);
        return;
    }
    case 8:
        if (((a >> 8) & 63u) >= KIT_NWAVE + SP_NUSER || !sp_wave[(a >> 8) & 63u].n)
            rc = 2;
        else
            wave_set(a & 31u, (a >> 8) & 63u);
        break;
    case 9: {
        uint32_t k;
        for (i = 0; i < SP_NSOUND; i++) zl_back[i] = sp_sound[i].wave;
        for (k = 0; k < KIT_NWAVE && i + 10u < ZL_MAX; k++) {
            for (n = 0; KIT_WAVE[k].name[n]; n++) zl_back[i++] = (uint8_t)KIT_WAVE[k].name[n];
            zl_back[i++] = 0;
        }
        zl_reply(9, 0, a, zl_back, i);
        return;
    }
    default:
        rc = 7;
        break;
    }
    zl_reply(cmd, rc, a, 0, 0);
}
