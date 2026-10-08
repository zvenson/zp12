/* SPDX-License-Identifier: GPL-3.0-only */
/* zp12's memory: the 32 sounds, the mix, the effects, the sequencer's settings, the song and the 16 segments as
 * one record, in flash 0xC4000..0xD7FFF: two copies of 40 KiB written in turn, each with a generation and a
 * CRC, so a write cut off by power keeps the last good one. A room sloopDX leaves free (its storage.c: SLOOP's
 * old sample slots past MY KIT), so sloopDX's projects, DX7 banks and kit stay as they are.
 * Written when asked (SAVE) or by itself when nothing plays and nothing changed for a few seconds: a flash
 * erase stops the audio for its ~40 ms a sector. */
#define ZS_BASE 0xC4000u
#define ZS_SLOT 0xA000u                          /* 40 KiB a copy */
#define ZS_MAGIC 0x3231505Au                     /* "ZP12" */
#define ZS_VER 6u                                /* 6: SMOOTH; 5: LEVEL in an audio taper; 4: the UI's settings (BACK); 3: the hits'
                                                 * locks; 2: four songs (1: one) */
#define ZS_KIT_ID ((uint16_t)(KIT_BYTES ^ KIT_NWAVE * 4099u ^ 0x0600u))   /* the factory kit the pads were saved with
                                                 * (0.6: changed once, so every older save gets the kit's pads) */

typedef struct { uint32_t magic, ver, gen, len, crc; } zs_head_t;
static uint8_t zs_buf[ZS_SLOT] __attribute__((section(".pool"), aligned(4)));
static uint32_t zs_gen, zs_saved_sig;

static uint32_t zs_crc(const uint8_t *p, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu, k;
    while (n--) {
        c ^= *p++;
        for (k = 0; k < 8u; k++)
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

static uint8_t *zs_put(uint8_t *p, const void *s, uint32_t n) { memcpy(p, s, n); return p + n; }
static const uint8_t *zs_get(const uint8_t *p, void *d, uint32_t n) { memcpy(d, p, n); return p + n; }

/* everything that is saved, as bytes after the head; returns the length. The locks come last, one word for each
 * hit that has one, as many as there is room for (the hits always fit: 16 full segments are 32 KiB) */
static uint32_t zs_pack(uint8_t *b)
{
    uint8_t *p = b, *e = b + ZS_SLOT - sizeof(zs_head_t);
    uint32_t i, j;
    uint16_t st[8] = {sq.bpm10, sq.quant, sq.swing, sq.click, sq.song_sel, sq.song_mode, sq.seg, ZS_KIT_ID};
    p = zs_put(p, sp_sound, sizeof sp_sound);
    p = zs_put(p, sp_mix, sizeof sp_mix);
    p = zs_put(p, &fxp, sizeof fxp);
    p = zs_put(p, st, sizeof st);
    p = zs_put(p, sq_songs, sizeof sq_songs);
    p = zs_put(p, sq_song_len, sizeof sq_song_len);
    {
        uint16_t u[4] = {ui.back, (uint16_t)(sp_smooth + 1u), (uint16_t)(sq.cin_bars + 1u), (uint16_t)(sq.dub_bar + 1u)};   /* (0: an older save) */
        p = zs_put(p, u, sizeof u);
    }
    for (i = 0; i < SQ_NSEG; i++) {
        const sq_seg_t *s = &sq_seg[i];
        p = zs_put(p, s, 4u);                    /* bars, rsv, n */
        p = zs_put(p, s->ev, s->n * sizeof(sq_ev_t));
    }
    for (i = 0; i < SQ_NSEG; i++)
        for (j = 0; j < sq_seg[i].n; j++)
            if (sq_seg[i].ev[j].lk && e - p >= 4)
                p = zs_put(p, &sq_lk[i][j], 4u);
    return (uint32_t)(p - b);
}

static int zs_unpack(const uint8_t *b, uint32_t len, uint32_t ver)
{
    const uint8_t *p = b, *e = b + len;
    uint32_t i, j;
    uint16_t st[8];
    if (len < sizeof sp_sound + sizeof sp_mix + sizeof fxp + sizeof st + sizeof sq_songs[0] + 4u * SQ_NSEG)
        return -1;
    p = zs_get(p, sp_sound, sizeof sp_sound);
    p = zs_get(p, sp_mix, sizeof sp_mix);
    p = zs_get(p, &fxp, sizeof fxp);
    p = zs_get(p, st, sizeof st);
    memset(sq_songs, 0, sizeof sq_songs);
    memset(sq_song_len, 0, sizeof sq_song_len);
    if (ver == 1u) {                                /* one song: song 1 */
        p = zs_get(p, sq_songs[0], sizeof sq_songs[0]);
        sq_song_len[0] = (uint8_t)(st[4] <= SQ_NSONG ? st[4] : 0u);
        st[4] = 0;
    } else {
        p = zs_get(p, sq_songs, sizeof sq_songs);
        p = zs_get(p, sq_song_len, sizeof sq_song_len);
        for (i = 0; i < SQ_SONGS; i++)
            if (sq_song_len[i] > SQ_NSONG) sq_song_len[i] = 0;
    }
    if (ver >= 4u) {
        uint16_t u[4];
        p = zs_get(p, u, sizeof u);
        ui.back = (uint8_t)(u[0] < 5u ? u[0] : 1u);
        sp_smooth = (uint8_t)(u[1] != 1u);       /* (0: an older save: on; 1: off; 2: on) */
        sq.cin_bars = (uint8_t)(u[2] ? (u[2] - 1u) % 3u : 1u);   /* (an older save: a bar, overdub at once) */
        sq.dub_bar = (uint8_t)(u[3] == 2u);
    } else {
        ui.back = 1;                              /* (an older save: 12 s, SMOOTH on) */
        sp_smooth = 1;
        sq.cin_bars = 1;
        sq.dub_bar = 0;
    }
    sq.bpm10 = (uint16_t)sp_clamp(st[0], 400, 2400);
    sq.quant = (uint8_t)(st[1] % 7u);
    sq.swing = (uint8_t)(st[2] % 6u);
    sq.click = (uint8_t)(st[3] % 3u);
    sq.song_sel = (uint8_t)(st[4] & 3u);
    sq.song_mode = (uint8_t)(st[5] & 1u);
    sq.seg = (uint8_t)(st[6] % SQ_NSEG);
    for (i = 0; i < SQ_NSEG; i++) {
        sq_seg_t *s = &sq_seg[i];
        if (e - p < 4)
            return -1;
        p = zs_get(p, s, 4u);
        if (s->n > SQ_MAXEV || s->bars > SQ_MAXBARS || (uint32_t)(e - p) < s->n * sizeof(sq_ev_t)) {
            s->n = 0;
            s->bars = 0;
            return -1;
        }
        p = zs_get(p, s->ev, s->n * sizeof(sq_ev_t));
    }
    for (i = 0; i < SQ_NSEG; i++)                   /* the locks (none before 3; past the end of the room: gone) */
        for (j = 0; j < sq_seg[i].n; j++)
            if (sq_seg[i].ev[j].lk) {
                if (ver >= 3u && e - p >= 4)
                    p = zs_get(p, &sq_lk[i][j], 4u);
                else
                    sq_seg[i].ev[j].lk = 0;
            }
    if (ver < 5u)                                   /* LEVEL was linear: the value that sounds the same squared */
        for (i = 0; i < SP_NSOUND; i++) {
            uint32_t l = sp_sound[i].level * 127u, r = 0;
            while ((r + 1u) * (r + 1u) <= l) r++;
            if (l - r * r > r) r++;                         /* (rounded) */
            sp_sound[i].level = (uint8_t)(r > 127u ? 127u : r);
        }
    if (st[7] != ZS_KIT_ID)                         /* saved with another factory kit: the pads its new sounds */
        for (i = 0; i < SP_NSOUND; i++)
            sp_sound[i] = KIT_PADS[i];
    for (i = 0; i < SP_NSOUND; i++)                 /* (a sound's wave must exist) */
        if (sp_sound[i].wave >= 64u || !sp_wave[sp_sound[i].wave].n)
            sp_sound[i].wave = 0xFFu;
    return 0;
}

/* the newest good copy into the machine; 0 = none (the factory state stays) */
static int zs_load(void)
{
    zs_head_t h[2];
    uint32_t i, best = 2;
    if (!flash_ok)
        return 0;
    for (i = 0; i < 2u; i++) {
        st_read(ZS_BASE + i * ZS_SLOT, &h[i], sizeof h[i]);
        if (h[i].magic != ZS_MAGIC || h[i].ver < 1u || h[i].ver > ZS_VER || h[i].len > ZS_SLOT - sizeof(zs_head_t))
            continue;
        if (best == 2u || h[i].gen > h[best].gen)
            best = i;
    }
    for (i = 0; i < 2u && best < 2u; i++) {          /* the newest; if its CRC fails, the other */
        uint32_t k = i ? best ^ 1u : best;
        if (h[k].magic != ZS_MAGIC || h[k].ver < 1u || h[k].ver > ZS_VER || h[k].len > ZS_SLOT - sizeof(zs_head_t))
            continue;
        st_read(ZS_BASE + k * ZS_SLOT + sizeof(zs_head_t), zs_buf, h[k].len);
        if (zs_crc(zs_buf, h[k].len) == h[k].crc && zs_unpack(zs_buf, h[k].len, h[k].ver) == 0) {
            zs_gen = h[k].gen;
            return 1;
        }
    }
    return 0;
}

/* a fingerprint of what would be saved (the autosave writes when it changed) */
static uint32_t zs_sig(void)
{
    uint32_t n = zs_pack(zs_buf);
    return zs_crc(zs_buf, n);
}

/* write the other copy; 0 = done */
static int zs_save(void)
{
    zs_head_t h;
    uint32_t n, off, a, took;
    if (!flash_ok)
        return -1;
    n = zs_pack(zs_buf + sizeof h);
    h.magic = ZS_MAGIC;
    h.ver = ZS_VER;
    h.gen = zs_gen + 1u;
    h.len = n;
    h.crc = zs_crc(zs_buf + sizeof h, n);
    memcpy(zs_buf, &h, sizeof h);
    off = ZS_BASE + (h.gen & 1u) * ZS_SLOT;
    for (a = 0; a < sizeof h + n; a += 0x1000u) {
        fm1_wdt_feed();
        if (fl_erase4k(off + a, &took))
            return -2;
    }
    if (fl_write(off, zs_buf, sizeof h + n))
        return -3;
    zs_gen = h.gen;
    zs_saved_sig = h.crc;
    return 0;
}
