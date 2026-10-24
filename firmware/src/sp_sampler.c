/* SPDX-License-Identifier: GPL-3.0-only */
/* Sampling on the machine. The computer (or a phone) plays into the FM-1 over USB (usb.c FELUCCA_UAI, the
 * FM-1 is its speaker); the SAMPLE page (GLO > SAMPLE, or LFO tapped) takes it into flash as an own sample, onto
 * the selected pad. REC: the room is erased first (ERASE %: the flash holds everything while it erases, the
 * sound stutters), then the take waits for the input to pass THRESH, runs TIME seconds or until REC again, and
 * goes into the directory the web editor uses (sp_samples.c), in the largest free run of its rooms (sloopDX's
 * bank room only when the editor was allowed to use it). Mono (L + R), two one-pole low-passes at 0.42 of RATE,
 * resampled from 44.1 kHz, 12 bit, packed as the kit. A page is programmed only just after a USB packet came in,
 * so the next one is never lost (the flash blocks the interrupts ~0.7 ms a page). The state and the settings
 * are in sp_ui.c (smp), the UI draws them. */

static const uint16_t SMP_THR[7] = {0, 129, 260, 519, 1036, 2067, 4125};   /* OFF, -48 .. -18 dB of full scale */

#define SMP_NQ 8u                                /* pages waiting for the flash (2 KB) */
static uint8_t smp_q[SMP_NQ][256] __attribute__((aligned(4)));
static uint32_t smp_qw, smp_qr;                  /* pages filled, pages written */
static uint32_t smp_bytes;                       /* bytes of the take produced */
static uint32_t smp_r, smp_ph, smp_step, smp_pk_last;
static int32_t smp_z1, smp_z2, smp_prev, smp_hold;
static uint8_t smp_has_hold;

/* the rooms a sample may lie in (as sp_samples.c zu_room), [lo, hi) */
static uint32_t smp_rooms(uint32_t r[4][2])
{
    uint32_t n = 0;
    if (zu_dir.magic == ZU_MAGIC && (zu_dir.flags & 1u)) { r[n][0] = 0xA0000u; r[n++][1] = 0xC4000u; }
    r[n][0] = 0xDA000u; r[n++][1] = 0xDC000u;
    r[n][0] = 0xE5000u; r[n++][1] = 0xE9000u;
    r[n][0] = 0xEA000u; r[n++][1] = 0xFC000u;
    return n;
}

static uint32_t smp_slot_bytes(uint32_t i) { return (zu_dir.s[i].n + 1u) / 2u * 3u; }
static int smp_slot_used(uint32_t i) { return zu_dir.magic == ZU_MAGIC && zu_dir.s[i].off && zu_dir.s[i].n; }

/* the largest free run, in whole 4 KiB sectors (an erase must not touch a sample beside it) */
static void smp_find(uint32_t *off, uint32_t *end)
{
    uint32_t r[4][2], nr = smp_rooms(r), k, i, j, best = 0;
    *off = *end = 0;
    for (k = 0; k < nr; k++) {
        for (j = 0; j <= SP_NUSER; j++) {          /* a run starts at the room's start or after a sample */
            uint32_t a = j == SP_NUSER ? r[k][0] : zu_dir.s[j].off + smp_slot_bytes(j), b = r[k][1];
            if (j < SP_NUSER && !smp_slot_used(j)) continue;
            a = (a + 0xFFFu) & ~0xFFFu;
            if (a < r[k][0] || a >= r[k][1]) continue;
            for (i = 0; i < SP_NUSER; i++) {
                uint32_t s0, s1;
                if (!smp_slot_used(i)) continue;
                s0 = zu_dir.s[i].off;
                s1 = s0 + smp_slot_bytes(i);
                if (s0 < a && s1 > a) { b = 0; break; }      /* a lies in a sample */
                if (s0 >= a && s0 < b) b = s0;
            }
            b &= ~0xFFFu;
            if (b > a && b - a > best) { best = b - a; *off = a; *end = b; }
        }
    }
}

static int smp_free_slot(void)
{
    uint32_t i;
    for (i = 0; i < SP_NUSER; i++)
        if (!smp_slot_used(i)) return (int)i;
    return -1;
}

/* the longest take that fits now, in samples at RATE (the TIME knob's end) */
static uint32_t smp_room_samples(void)
{
    uint32_t off, end;
    smp_find(&off, &end);
    return (end - off) / 3u * 2u;
}

static void smp_stop(const char *why)
{
    smp.state = SM_IDLE;
    if (why) ui_say(why);
    ui.force = 1;
}

/* REC on the SAMPLE page: start (the room erased first), or end the take */
static void smp_press(void)
{
    uint32_t want, rate = SMP_RATE[smp.rate % 3u];
    int slot;
    if (smp.state == SM_REC) { smp.want = smp.n + smp_has_hold; return; }   /* (it ends with what is in hand) */
    if (smp.state != SM_IDLE) { smp_stop("SAMPLE OFF"); return; }
    if (!flash_ok) { ui_say("NO FLASH"); return; }
    if (!uai.alt) { ui_say("NO USB AUDIO"); return; }
    slot = smp_free_slot();
    if (slot < 0) { ui_say("24 SAMPLES: FULL"); return; }
    smp_find(&smp.off, &smp.end);
    want = (uint32_t)smp.len10 * rate / 10u;
    if (want > (smp.end - smp.off) / 3u * 2u) want = (smp.end - smp.off) / 3u * 2u;
    if (want < rate / 10u) { ui_say("NO ROOM"); return; }
    smp.slot = (uint8_t)slot;
    smp.want = want;
    smp.n = 0;
    smp.erased = smp.off;
    smp.state = SM_ERASE;
    ui.force = 1;
}

static void smp_put12(int32_t v)                  /* one 12-bit sample into the take, packed 2 in 3 bytes */
{
    if (!smp_has_hold) { smp_hold = v; smp_has_hold = 1; return; }
    {
        uint32_t a = (uint32_t)smp_hold & 0xFFFu, b = (uint32_t)v & 0xFFFu, k;
        uint8_t t[3] = {(uint8_t)a, (uint8_t)(a >> 8 | (b << 4 & 0xF0u)), (uint8_t)(b >> 4)};
        for (k = 0; k < 3u; k++) {
            smp_q[smp_qw % SMP_NQ][smp_bytes % 256u] = t[k];
            if (++smp_bytes % 256u == 0u) smp_qw++;
        }
    }
    smp_has_hold = 0;
}

static void smp_frame(int32_t x)                  /* one input frame (mono, 16 bit) of the take */
{
    int32_t a = SMP_LP[smp.rate % 3u];
    smp_z1 += ((x - smp_z1) * a) >> 15;
    smp_z2 += ((smp_z1 - smp_z2) * a) >> 15;
    while (smp_ph < 65536u && smp.n + smp_has_hold < smp.want) {   /* the outputs between the last frame and this one */
        int32_t y = smp_prev + (int32_t)(((int64_t)(smp_z2 - smp_prev) * (int32_t)smp_ph) >> 16);
        y = (y + 8) >> 4;
        smp_put12(y < -2048 ? -2048 : y > 2047 ? 2047 : y);
        if (!smp_has_hold) smp.n += 2u;
        smp_ph += smp_step;
    }
    smp_ph -= 65536u;
    smp_prev = smp_z2;
}

static void smp_end_take(void)                    /* the take's last pair and page, then DONE */
{
    if (smp_has_hold) { smp_put12(0); smp.n += 1u; }   /* (an odd take: its last pair padded) */
    if (smp_bytes % 256u) {                       /* the last page: the rest left erased */
        uint32_t k;
        for (k = smp_bytes % 256u; k < 256u; k++) smp_q[smp_qw % SMP_NQ][k] = 0xFF;
        smp_qw++;
    }
    smp.state = SM_DONE;
}

static int smp_dir_write(uint32_t n)              /* the take into the directory (a new generation, the older copy) */
{
    static zu_dir_t d;
    uint32_t a, took;
    if (zu_dir.magic == ZU_MAGIC) d = zu_dir;
    else { memset(&d, 0, sizeof d); d.magic = ZU_MAGIC; }
    d.gen++;
    d.s[smp.slot].off = smp.off;
    d.s[smp.slot].n = n;
    d.s[smp.slot].rate = SMP_RATE[smp.rate % 3u];
    d.s[smp.slot].flags = 0;
    d.s[smp.slot].rsv = 0;
    memset(d.s[smp.slot].name, 0, 8);
    d.s[smp.slot].name[0] = 'S'; d.s[smp.slot].name[1] = 'M'; d.s[smp.slot].name[2] = 'P';
    d.s[smp.slot].name[3] = (char)('0' + (smp.slot + 1u) / 10u); d.s[smp.slot].name[4] = (char)('0' + (smp.slot + 1u) % 10u);
    d.crc = zs_crc((const uint8_t *)d.s, sizeof d.s);
    a = ZU_DIR0 + (d.gen & 1u) * 0x1000u;
    if (fl_erase4k_quiet(a, &took)) return -1;
    return fl_write(a, (const uint8_t *)&d, sizeof d);
}

/* main loop: one step of the take (an erase, or up to ~20 ms of input and pages) */
static void smp_service(void)
{
    uint32_t t0 = fm1_ms;
    if (smp.state == SM_IDLE)
        return;
    if (!uai.alt && smp.state != SM_ERASE) {      /* the host stopped playing */
        if (smp.state == SM_REC && smp.n >= 2u) smp.want = smp.n + smp_has_hold;
        else { smp_stop("NO INPUT"); return; }
    }
    if (smp.state == SM_ERASE) {
        uint32_t need = (smp.off + (smp.want + 1u) / 2u * 3u + 0xFFFu) & ~0xFFFu, took;
        if (smp.erased < need) {
            if (fl_erase4k_quiet(smp.erased, &took)) { smp_stop("ERASE ERROR"); return; }
            smp.erased += 0x1000u;
            return;
        }
        smp_qw = smp_qr = smp_bytes = 0;
        smp_has_hold = 0;
        smp_z1 = smp_z2 = smp_prev = 0;
        smp_ph = 0;
        smp_step = (44100u << 16) / SMP_RATE[smp.rate % 3u];   /* (fits 32 bits: no 64-bit division here) */
        smp_r = uai_w;                            /* (what came in while erasing is gone) */
        smp.state = smp.thr ? SM_WAIT : SM_REC;
        ui.force = 1;
        return;
    }
    for (;;) {
        uint32_t w = uai_w;
        while (smp_r != w && smp.state != SM_DONE && smp_qw - smp_qr < SMP_NQ - 1u) {
            uint32_t f = uai_ring[smp_r % UAI_N];
            int32_t x = ((int32_t)(int16_t)(f & 0xFFFFu) + (int32_t)(int16_t)(f >> 16)) >> 1;
            smp_r++;
            if (smp.state == SM_WAIT) {
                if ((uint32_t)(x < 0 ? -x : x) < SMP_THR[smp.thr % 7u]) continue;
                smp.state = SM_REC;
                ui.force = 1;
            }
            smp_frame(x);
            if (smp.n + smp_has_hold >= smp.want)
                smp_end_take();
        }
        if (smp.state == SM_REC && smp.n + smp_has_hold >= smp.want && smp_qw - smp_qr < SMP_NQ - 1u)
            smp_end_take();                       /* (REC again, or the host stopped: no frame brings the end) */
        if (smp_qr != smp_qw && uai.pkts != smp_pk_last) {   /* a page, just after a packet */
            smp_pk_last = uai.pkts;
            if (fl_write(smp.off + smp_qr * 256u, smp_q[smp_qr % SMP_NQ], 256u)) { smp_stop("WRITE ERROR"); return; }
            smp_qr++;
        } else if (smp_qr != smp_qw && !uai.alt) {           /* (no packets any more: write the rest now) */
            if (fl_write(smp.off + smp_qr * 256u, smp_q[smp_qr % SMP_NQ], 256u)) { smp_stop("WRITE ERROR"); return; }
            smp_qr++;
        }
        if (smp.state == SM_DONE && smp_qr == smp_qw) {
            uint32_t n = smp.want < smp.n ? smp.want : smp.n, k;
            char m[24], *p;
            sp_sound_t *s = &sp_sound[ui.sel];
            if (smp_dir_write(n)) { smp_stop("DIR ERROR"); return; }
            zu_load();
            s->wave = (uint8_t)(KIT_NWAVE + smp.slot);
            s->tune = 0; s->fine = 0; s->start = 0; s->end = 1000;
            s->flags = (uint8_t)(s->flags & ~(SPF_33 | SPF_REVERSE));
            p = cat(m, "");
            pad_label(p, ui.sel);
            while (*p) p++;
            p = cat(p, " ");
            for (k = 0; k < 8u && ui_uname[smp.slot][k]; k++) *p++ = ui_uname[smp.slot][k];
            *p = 0;
            smp.state = SM_IDLE;
            ui.save_req = 1;                      /* (the pad's new sound into the save) */
            ui_say(m);
            ui.force = 1;
            SP_HIT(ui.sel, 110, 0);
            return;
        }
        if ((uint32_t)(fm1_ms - t0) >= 20u)
            return;
        if (smp_r == uai_w && smp_qr == smp_qw)  /* nothing in hand: the main loop goes on */
            return;
    }
}

/* the audio ISR: the input mixed into the output at MON (else nothing heard: the computer plays into the FM-1) */
static uint32_t smp_mr, smp_mlast;
static void smp_monitor(int32_t *o, uint32_t n)
{
    uint32_t i, w = uai_w, g = smp.mon;
    if (!uai.alt || !g) { smp_mr = w; return; }
    if ((uint32_t)(w - smp_mr) > 1024u)           /* far behind (a stall, a new stream): close up, a little in hand */
        smp_mr = w - 192u;
    for (i = 0; i < n; i++) {
        if (smp_mr != w) smp_mlast = uai_ring[smp_mr++ % UAI_N];   /* (the I2S runs a little fast: now and then one twice) */
        o[2u * i] += ((int32_t)(int16_t)(smp_mlast & 0xFFFFu) * (int32_t)g) >> 7;
        o[2u * i + 1u] += ((int32_t)(int16_t)(smp_mlast >> 16) * (int32_t)g) >> 7;
    }
}
