/* SPDX-License-Identifier: GPL-3.0-only */
/* zp12's own samples, put there by the web editor (web/editor.html over sp_link.c): a directory in flash, two
 * copies (0xD8000, 0xD9000) with a generation and a CRC, the newest good one counts; the samples themselves
 * packed as the kit's (12 bit, 2 in 3 bytes) in the free rooms, read through the plain XIP window. Here they
 * are only read: at the start, and when the editor says RELOAD. They play as waves KIT_NWAVE + slot.
 *   directory: magic "ZPSM", gen, flags (bit 0: sloopDX's bank room holds samples), CRC32 of the slots,
 *              24 slots of {flash offset (0: empty), samples, rate Hz, flags (bit 0: stored for 45->33, bit 1: at double speed), -, name[8]} */
#define ZU_DIR0 0xD8000u
#define ZU_MAGIC 0x4D53505Au                     /* "ZPSM" */
typedef struct { uint32_t off, n; uint16_t rate; uint8_t flags, rsv; char name[8]; } zu_slot_t;
typedef struct { uint32_t magic, gen, flags, crc; zu_slot_t s[SP_NUSER]; } zu_dir_t;
static zu_dir_t zu_dir;

static int zu_room(uint32_t off, uint32_t bytes)  /* inside one room the editor may fill */
{
    return FL_IN(off, bytes, 0xA0000u, 0xC4000u) || FL_IN(off, bytes, 0xDA000u, 0xDC000u) ||
           FL_IN(off, bytes, 0xE5000u, 0xE9000u) || FL_IN(off, bytes, 0xEA000u, 0xFC000u);
}

static void zu_load(void)
{
    static zu_dir_t d[2];
    uint32_t i, best = 2;
    for (i = 0; i < SP_NUSER; i++) {             /* none first (a voice on one of them stops) */
        sp_wave[KIT_NWAVE + i].n = 0;
        ui_uname[i][0] = 0;
        ui_uflags[i] = 0;
    }
    for (i = 0; i < SP_NCH; i++)
        if (sp_ch[i].w >= &sp_wave[KIT_NWAVE]) sp_ch[i].on = 0;
    if (!flash_ok)
        return;
    for (i = 0; i < 2u; i++)
        if (!st_read(ZU_DIR0 + i * 0x1000u, &d[i], sizeof d[i]) && d[i].magic == ZU_MAGIC &&
            zs_crc((const uint8_t *)d[i].s, sizeof d[i].s) == d[i].crc && (best == 2u || d[i].gen > d[best].gen))
            best = i;
    if (best == 2u) {
        zu_dir.magic = 0;
        return;
    }
    zu_dir = d[best];
    for (i = 0; i < SP_NUSER; i++) {
        const zu_slot_t *s = &zu_dir.s[i];
        uint32_t k;
        if (!s->off || s->n < 2u || s->n > 400000u || s->rate < 8000u || s->rate > 48000u || !zu_room(s->off, (s->n + 1u) / 2u * 3u))
            continue;
        sp_wave[KIT_NWAVE + i].d = (const uint8_t *)FL_XIP(s->off);
        sp_wave[KIT_NWAVE + i].rate = s->rate;
        ui_uflags[i] = s->flags;
        for (k = 0; k < 8u && s->name[k] >= 32 && s->name[k] < 127; k++) ui_uname[i][k] = s->name[k];
        ui_uname[i][k] = 0;
        if (!k) { ui_uname[i][0] = 'U'; ui_uname[i][1] = (char)('A' + i); ui_uname[i][2] = 0; }
        sp_wave[KIT_NWAVE + i].n = s->n;         /* (last: the sample is there before a voice can take it) */
    }
}
