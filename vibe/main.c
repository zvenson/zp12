/* SPDX-License-Identifier: GPL-3.0-only */
/* ZP12 for the VIBE – native. A 12-bit sampling drum machine in the spirit of the 80s samplers (their names are
 * trademarks of their owners, no affiliation): zp12's sound (sp_core.c / sp_fx.c / sp_seq.c, the FM-1's own files)
 * with the VIBE's inputs, its screen and its controls. GPL-3.0; built on SLOOP (isod89) and Felucca (Hügelton
 * Instruments), as zp12 is.
 *
 * One flow: events, the sound and the picture take turns (no locks). The sound runs in zp12's blocks of 32 frames
 * (SP_BLK) and hands them out as the host asks; writing to the card goes a piece a frame. */
#include "zv_core.h"
#include "zv_rec.h"
#include "zv_ui.h"

static int32_t obuf[2 * SP_BLK];
static int opos = SP_BLK;
static uint32_t gen_saved, gen_seen, last_write;

/* the first start: loop 1 holds a beat, so PLAY plays (pads A1 kick, A2 snare, A3 hat of the kit) */
static void demo_beat(void)
{
    static const uint8_t K[2][16] = { { 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6, 0, 0, 0, 0, 0 }, { 7, 0, 0, 0, 0, 0, 0, 5, 0, 0, 6, 0, 0, 0, 0, 0 } };
    static const uint8_t SN[2][16] = { { 0, 0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 7, 0, 0, 0 }, { 0, 0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 7, 0, 0, 2 } };
    sq_seg_t *s = &sq_seg[0];
    s->bars = 2;
    for (uint32_t b = 0; b < 2; b++)
        for (uint32_t i = 0; i < 16; i++) {
            uint32_t t = b * SQ_BAR + i * 24u;
            if (K[b][i]) sq_insert(s, t, 0, K[b][i] - 1u, 0, 0, 0);
            if (SN[b][i]) sq_insert(s, t, 1, SN[b][i] - 1u, 0, 0, 0);
            if (!(i & 1u)) sq_insert(s, t, 2, i & 2u ? 3u : 5u, 0, 0, 0);
        }
}

void vibe_on_init(float sample_rate)
{
    sp_fs = (uint32_t)(sample_rate + 0.5f);
    if (sp_fs < 8000u) sp_fs = 48000u;
    sq_ticks_k = (uint32_t)(3355443200ULL / sp_fs);       /* 96 x 32 x 65536 x 10000 / (600 x rate) */
    for (int i = 0; i < KIT_NWAVE; i++) {
        sp_wave[i].d = KIT_DATA + KIT_WAVE[i].off; sp_wave[i].n = KIT_WAVE[i].n; sp_wave[i].rate = 26040; sp_wave[i].fmt = SPW_12;
    }
    for (int w = 0; w < SP_NWAVE; w++) w_next[w] = 0xFFFF;
    for (int k = 0; k < SP_NSOUND; k++) sp_sound[k] = kit_pad(k);
    sq_init();
    if (!store_load()) demo_beat();
    for (int w = KIT_NWAVE; w < SP_NWAVE; w++)           /* the next own name: after the highest SMPnn */
        if (own[w].used && own[w].name[0] == 'S' && own[w].name[1] == 'M' && own[w].name[2] == 'P') {
            int n = (own[w].name[3] - '0') * 10 + (own[w].name[4] - '0');
            if (n > smp_count && n < 100) smp_count = n;
        }
    gen_saved = gen_seen = sq.gen;
    vibe_areas(PG_N);
    vibe_extras_own(1);                                  /* (encoders 1-4 pressed: the row of the pad DATA chose) */
    vibe_log("ZP12 3.0 started");
}

void vibe_on_event(const vibe_event *e)
{
    switch (e->type) {
    case VIBE_EV_PAD: pad_event(e->id, e->down, e->value); break;
    case VIBE_EV_KEY:
        if (e->down) {
            int semis = e->id - 12 + key_oct * 12;
            semis = semis < -32 ? -32 : semis > 31 ? 31 : semis;
            uint32_t vel = (uint32_t)(1.0f + e->value * 126.0f);
            SP_HIT(sel, vel, semis);
            flash_t[sel] = now_ms();
        }
        break;
    case VIBE_EV_MIDI: {
        uint8_t st = e->midi[0] & 0xF0u, d1 = e->midi[1], d2 = e->midi[2];
        if (st == 0x90 && d2 && d1 >= 36 && d1 < 36 + SP_NSOUND) { SP_HIT(d1 - 36, d2, 0); flash_t[d1 - 36] = now_ms(); }
        else if (st == 0xC0 && d1 < SQ_NSEG) sq_post(RQ_LOOP, 0, d1);
        break;
    }
    case VIBE_EV_BUTTON: btn_val = e->value; button(e->id, e->down); break;
    case VIBE_EV_ENCODER: encoder(e->id, e->value); break;
    case VIBE_EV_AREA: area_event(e->value); break;
    case VIBE_EV_TOUCH: touch(e); break;
    case VIBE_EV_PEDAL: if (e->id == VIBE_PEDAL_SWITCH && e->down) sq_post(sq.playing ? RQ_STOP : RQ_PLAY, 0, 0); break;
    case VIBE_EV_MEDIA: if (br_on) br_wait = 1; break;
    case VIBE_EV_SAVE: store_all(); gen_saved = sq.gen; break;
    }
}

void vibe_on_audio(const float *in, float *l, float *r, int n)
{
    rec_audio(in, n);
    int mon = in_mon && page == PG_SAMPLE;
    for (int i = 0; i < n; i++) {
        if (opos >= SP_BLK) {
            for (int j = 0; j < 2 * SP_BLK; j++) obuf[j] = 0;
            fx_bpm10 = sq.bpm10;
            sq_block();
            sp_render(obuf);
            opos = 0;
        }
        float a = (float)sp_out(obuf[2 * opos]) * (1.0f / 32768.0f), b = (float)sp_out(obuf[2 * opos + 1]) * (1.0f / 32768.0f);
        opos++;
        if (mon) { a += mon_buf[i]; b += mon_buf[i]; }
        l[i] = a > 1.0f ? 1.0f : a < -1.0f ? -1.0f : a;   /* (the monitor at +36 dB may go over) */
        r[i] = b > 1.0f ? 1.0f : b < -1.0f ? -1.0f : b;
    }
}

void vibe_on_draw(uint16_t *fb)
{
    uint32_t now = now_ms();
    rec_finish_step();
    load_step();
    if (sq.gen != gen_seen) { gen_seen = sq.gen; st_changed = now; }
    if (sq.turn && now - turn_t > 400u) sq.turn = 0;
    { uint32_t p = sq.played; if (p) { sq.played = 0; for (int k = 0; k < SP_NSOUND; k++) if (p >> k & 1u) flash_t[k] = now; } }
    /* the card: a piece of a new sample a frame (while playing: four a second), the rest when stopped and quiet */
    if (rs == RS_OFF) {
        if (store_pending()) { if (!sq.playing || now - last_write > 250u) { store_step(); last_write = now; } }
        else if ((st_dirty || sq.gen != gen_saved) && !sq.playing && now - st_changed > 2000u) { store_state(); st_dirty = 0; gen_saved = sq.gen; }
    }
    ui_draw(fb);
}
