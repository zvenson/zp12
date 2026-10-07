/* SPDX-License-Identifier: GPL-3.0-only */
/* zp12: a 12-bit sampling drum machine for the M-VAVE FM-1. One compilation unit: the HAL (header-only),
 * the platform of SLOOP / Felucca as sloopDX carries it (boot guard, USB-MIDI, the M-UPGRADE updater and the
 * USB rescue, so the web installer always reaches the FM-1), the sound core (sp_core.c), the factory kit
 * (build/gen/zp12_kit.h, tools/gen_kit.py) and the panel (sp_ui.c). */
#include <stdint.h>
#include "fm1_time.h"
#include "fm1_sys.h"
#include "fm1_irq.h"
#include "fm1_guard.h"
#include "fm1_input.h"
#include "fm1_timer.h"
#include "fm1_lcd_hw.h"
#include "fm1_audio.h"
#include "fm1_adc.h"
#include "bootguard.h"

bootguard_t bootguard __attribute__((section(".noinit")));
static volatile uint32_t fm1_ms;

#define RING_PUBLISH() __asm__ volatile("" ::: "memory")   /* (as sloopDX core.h) */
#include "libc.c"
#include "lcd.c"
#define CV_MAX (232u * 80u)                    /* the biggest region: the faders */
#include "gfx.c"

#define FELUCCA_OTA 1
#define FELUCCA_CDC 0
#define FELUCCA_UAC 0
#define FELUCCA_UART 0
#ifndef FELUCCA_ID
#define FELUCCA_ID "FM-1_970"                   /* package identity: 97N = zp12 build N */
#endif
#include "usb.c"

/* ---- flash: read anywhere, erase / program only the update's staging area (saving comes later) */
#include "fm1_flash.h"
static uint8_t flash_ok;
static int st_read(uint32_t off, void *dst, uint32_t n)
{
    uint8_t *d = dst;
    while (n) {
        uint32_t k = n > 256u ? 256u : n, f = irq_save();
        int rc = FL_FAR(fl_read_ram)(off, d, k);
        irq_restore(f);
        if (rc)
            return rc;
        off += k;
        d += k;
        n -= k;
    }
    return 0;
}
static int fl_erase4k_quiet(uint32_t off, uint32_t *took)
{
    uint32_t f = irq_save();
    int rc = FL_FAR(fl_erase4k_ram)(off, took);
    irq_restore(f);
    return rc;
}

static uint8_t recovery_active;
#define OTA_IDENTITY (recovery_active ? "FM-1_000" : FELUCCA_ID)
#include "ota.c"
static void recovery_poll(void);
static uint32_t ota_now_ms(void) { return fm1_ms; }
static void ota_idle(void)
{
    fm1_wdt_feed();
    if (recovery_active) recovery_poll();
}
static int ota_in_area(uint32_t off, uint32_t n) { return FL_IN(off, n, OTA_AREA, OTA_AREA + OTA_AREA_LEN); }
static int ota_erase(uint32_t off)
{
    uint32_t took;
    if (!ota_in_area(off, 0x1000u) || (off & 0xFFFu))
        return -8;
    return fl_erase4k_quiet(off, &took);
}
static int ota_prog(uint32_t off, const void *p, uint32_t n)
{
    if (!ota_in_area(off, n))
        return -8;
    return fl_write(off, p, n);
}
static int ota_fread(uint32_t off, void *p, uint32_t n) { return st_read(off, p, n); }
static void ota_show(uint32_t step, int32_t code)
{
    static const char *const STEP[] = {"", "PACKAGE", "CHECK HEAD", "LOADER", "CONFIRM", "RESTART"};
    if (recovery_active) return;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 92, 240, &FONT_S, "UPDATE", C_WHITE, 1);
    if (step < 9u)
        draw_text_box(0, 124, 240, &FONT_S, STEP[step < 6u ? step : 0], C_WHITE, 1);
    else
        draw_text_box(0, 124, 240, &FONT_S, code == 1 ? "DRY RUN OK" : "FAILED", C_WHITE, 1);
    (void)code;
}
static void ota_commit(const uint8_t *parm)
{
    bootguard_clear(&bootguard);
    usb_detach();
    fm1_delay_ms(30);
    fm1_enter_update(parm);
}
#include "recovery.c"

#define SP_WITH_FX 1
#include "sp_core.c"
#include "sp_fx.c"
#include "zp12_kit.h"
#include "sp_seq.c"
/* pad hits go to the audio ISR as requests (sp_seq.c sq_post): sounded, and recorded when REC */
#define SP_HIT(k, vel, semis) sq_post(RQ_HIT, k, (vel) | (uint32_t)((semis) + 64) << 8)
#include "sp_ui.c"
#include "sp_store.c"

/* ---- the timer: 10 kHz key scan, milliseconds, USB at 2 kHz */
void fm1_timer5_irq(void)
{
    static uint32_t sub, last, acc;
    uint32_t t0 = fm1_ticks();
    fm1_timer5_ack();
    fm1_input_tick();
    acc += t0 - last;
    last = t0;
    while (acc >= 1000u * FM1_TICKS_PER_US) {
        acc -= 1000u * FM1_TICKS_PER_US;
        fm1_ms++;
    }
    if (sub % 5u == 0u)
        usb_poll();
    if (++sub == 10u)
        sub = 0;
}
extern void isr_timer5(void);

/* ---- the sound: ALNK0 I2S, two halves of 128 frames; sp_render in blocks of 32, the MASTER knob, Q15 -> 24 bit */
#define HALF_FRAMES 128u
#define HALF_WORDS (HALF_FRAMES * 2u)
static int32_t abuf[2u * HALF_WORDS] __attribute__((aligned(4)));
static volatile int32_t master_q12 = 2048;

void fm1_alnk0_irq(void)                       /* via isr_alnk0 (hal/fm1_isr.S) */
{
    uint8_t p = fm1_audio_pending();
    fm1_audio_ack_aux(p);
    if (p & FM1_AUDIO_HALF) {
        int32_t *o = &abuf[fm1_audio_free_half() * HALF_WORDS];
        uint32_t i, b;
        for (i = 0; i < HALF_WORDS; i++)
            o[i] = 0;
        for (b = 0; b < HALF_FRAMES; b += SP_BLK) {
            sq_block();                                     /* the sequencer's hits of this block */
            sp_render(o + 2u * b);
        }
        for (i = 0; i < HALF_WORDS; i++) {
            int32_t v = (o[i] * master_q12) >> 12;
            o[i] = sp_clamp(v, -32767, 32767) << 8;
        }
        fm1_audio_ack_half();
    }
}
extern void isr_alnk0(void);

/* the USB side, polled from the main loop: the installer's update request, the UBOOT request */
static void fm1_service(void)
{
    fm1_wdt_feed();
    usb_retry(fm1_ms);
    ota_service();
    if (usb.ota_req) {
        usb.ota_req = 0;
        if (flash_ok)
            ota_session();                      /* returns only if nothing was committed */
        ui.force = 1;
    }
    if (usb.uboot_req) {
        fm1_audio_stop();
        lcd_fill(0, 0, 240, 240, C_BLACK);
        draw_text_box(0, 110, 240, &FONT_S, "UBOOT (USB)", C_WHITE, 1);
        usb_detach();
        fm1_delay_ms(30);
        bootguard.pending = 0;
        fm1_enter_uboot();
    }
    if ((uint32_t)fm1_ms >= 30000u && bootguard.pending)
        bootguard_clear(&bootguard);            /* 30 s up: this firmware boots */
}

static void fm1_fault(const fm1_crash_t *c)
{
    char b[12];
    uint32_t i, t0, v[3] = {c->vec, c->pc, c->rets};
    fm1_audio_stop();
    lcd_fill(0, 0, 240, 240, RGB(160, 0, 0));
    draw_text_box(0, 20, 240, &FONT_S, "ZP12 CRASH", C_WHITE, 1);
    for (i = 0; i < 3u; i++) {
        uint32_t k;
        for (k = 0; k < 8u; k++) b[k] = "0123456789ABCDEF"[(v[i] >> (28u - 4u * k)) & 15u];
        b[8] = 0;
        draw_text_box(10, 60 + i * 20, 220, &FONT_S, b, C_WHITE, 0);
    }
    t0 = fm1_ticks();
    while ((uint32_t)(fm1_ticks() - t0) < 8000u * 1000u * FM1_TICKS_PER_US)
        fm1_wdt_feed();
    fm1_reboot();
}

static void splash(void)                       /* the wordmark a moment (tools/gen_logo.py later) */
{
    lcd_fill(0, 0, 240, 240, P_NAVY);
    lcd_fill(0, 96, 240, 2, P_RED);
    lcd_fill(0, 150, 240, 2, P_RED);
    cv_begin(240, 40, P_NAVY);
    cv_text_on(120 - text_w(&FONT_L, "zp12") / 2, 4, &FONT_L, "zp12", P_FRAME, P_NAVY);
    cv_blit(0, 104);
    cv_begin(240, 16, P_NAVY);
    cv_text_on(120 - text_w(&FONT_S, "12-bit sampling drums") / 2, 0, &FONT_S, "12-bit sampling drums", P_RULE, P_NAVY);
    cv_blit(0, 160);
    cv_begin(240, 16, P_NAVY);
    cv_text_on(120 - text_w(&FONT_S, "based on SLOOP + Felucca") / 2, 0, &FONT_S, "based on SLOOP + Felucca", P_RULE, P_NAVY);
    cv_blit(0, 214);
    lcd_sync();
}

static void zp12_main(void)
{
    uint32_t i, prev_notes = 0, t_frame = 0, seq_down = 0, seq_used = 0, t_sig = 0, t_change = 0, last_sig = 0, save_later = 0;
    int32_t knob_avg = 512 * 16;
    for (i = 0; i < KIT_NWAVE; i++) {
        sp_wave[i].d = KIT_DATA + KIT_WAVE[i].off;
        sp_wave[i].n = KIT_WAVE[i].n;
        sp_wave[i].rate = 26040;
    }
    for (i = 0; i < SP_NSOUND; i++)
        sp_sound[i] = KIT_PADS[i];
    sq_init();
    if (zs_load())                                      /* what was left: sounds, mix, effects, segments, song */
        zs_saved_sig = zs_sig();
    ui_init();
    splash();
    while (fm1_ms < 1200u)
        fm1_service();
    ui.force = 1;
    for (;;) {
        uint32_t n = fm1_in.notes, down = n & ~prev_notes, rel, b;
        fm1_service();
        prev_notes = n;
        {   /* SEL held: the faders 5-8; LFO held: ERASE (the pads held lose their hits as the playhead passes) */
            uint32_t bt = fm1_in.buttons, er = 0, k;
            ui.shift = (uint8_t)((bt >> B_SEL) & 1u);
            if ((bt >> B_LFO) & 1u)
                for (k = 0; k < 27u; k++)
                    if ((n >> k) & 1u) {
                        uint32_t pd = ui.multi ? ui.sel : key_pad(k);
                        if (pd < 32u) er |= 1u << pd;
                    }
            sq.erase = er;
            for (i = 0; down; i++, down >>= 1)          /* the keys: pads (ui decides which) */
                if (down & 1u)
                    key_down(i, (bt >> B_LFO) & 1u);
        }
        b = fm1_input_edges(&rel);
        if (b & (1u << B_SEQ)) {                        /* SEQ down: the step grid while held */
            seq_down = fm1_ms;
            seq_used = 0;
            ui.steps = 1;
            ui.force = 1;
            b &= ~(1u << B_SEQ);
        }
        if (ui.steps && (b & ((1u << B_OCTDN) | (1u << B_OCTUP)) || down))
            seq_used = 1;
        for (i = 0; b; i++, b >>= 1)
            if (b & 1u)
                button(i);
        if (rel & (1u << B_SEQ) && ui.steps) {          /* SEQ up: a short tap opens the SEQ pages */
            ui.steps = 0;
            ui.force = 1;
            if ((uint32_t)(fm1_ms - seq_down) < 350u && !seq_used)
                button(B_SEQ);
        }
        {   /* the LEDs: built aside, then swapped in (the scan never shows half of them) */
            uint32_t lb, lk, lg, id, c, r;
            uint8_t nl[FM1_NCOL], nd[FM1_NCOL];
            ui_leds(&lb, &lk, &lg);
            memset(nl, 0, sizeof nl);
            memset(nd, 0, sizeof nd);
            for (c = 0; c < FM1_NCOL; c++)
                for (r = 1; r < 5u; r++) {
                    int32_t kid = FM1_KEYMAP[r][c];
                    if (kid < 0) continue;
                    id = (uint32_t)kid;
                    if (id < 14u ? (lb >> id) & 1u : (lk >> (id - 14u)) & 1u) nl[c] |= (uint8_t)(1u << r);
                    if (id >= 14u && (lg >> (id - 14u)) & 1u) nd[c] |= (uint8_t)(1u << r);
                }
            memcpy(fm1_led, nl, sizeof nl);
            memcpy(fm1_led_dim, nd, sizeof nd);
        }
        {   /* KNOB 1-4 (encoders 2..5), SELECT (0): tempo, ALGORITHM (1): the sound */
            int32_t d;
            for (i = 0; i < 4u; i++)
                if ((d = fm1_enc_take(2u + i)) != 0)
                    knob(i, d);
            if ((d = fm1_enc_take(0)) != 0)
                sq.bpm10 = (uint16_t)sp_clamp((int32_t)sq.bpm10 + d * 5, 400, 2400);
            if ((d = fm1_enc_take(1)) != 0)
                ui.sel = (uint8_t)((ui.sel + SP_NSOUND + (d > 0 ? 1 : SP_NSOUND - 1)) % SP_NSOUND);
            (void)fm1_enc_take(6);
        }
        while (mi_r != mi_w) {                          /* USB MIDI in: notes 36..67 play the 32 pads */
            uint32_t pk = midi_in_q[mi_r % MQ], st = (pk >> 8) & 0xF0u, nt = (pk >> 16) & 0x7Fu, vel = pk >> 24 & 0x7Fu;
            mi_r++;
            if (st == 0x90u && vel && nt >= 36u && nt < 68u)
                pad_hit(nt - 36u, vel);
        }
        {
            int32_t a = fm1_adc_read(FM1_ADC_MASTER);   /* the MASTER knob */
            if (a >= 0) {
                uint32_t k10;
                knob_avg += (a * 16 - knob_avg) / 8;
                k10 = (uint32_t)(knob_avg / 16);
                master_q12 = (int32_t)((k10 * k10) >> 8);
            }
        }
        fx_bpm10 = sq.bpm10;                            /* the delay follows the tempo */
        if ((uint32_t)(fm1_ms - t_frame) >= 16u) {
            t_frame = fm1_ms;
            ui_draw();
        }
        {   /* saving: SAVE, or by itself when stopped, silent and nothing changed for 3 s (an erase stops the audio) */
            uint32_t k, quiet = !sq.playing;
            for (k = 0; k < SP_NCH; k++) quiet &= !sp_ch[k].on;
            if (fm1_ms - t_sig >= 500u) {
                uint32_t sg = zs_sig();
                t_sig = fm1_ms;
                if (sg != last_sig) { last_sig = sg; t_change = fm1_ms; }
            }
            if (ui.save_req && !quiet && sq.playing) {
                ui.save_req = 0;
                save_later = 1;
                ui_say("SAVES AT STOP");
            }
            if ((ui.save_req || save_later || (last_sig != zs_saved_sig && fm1_ms - t_change > 3000u)) && quiet) {
                ui.save_req = 0;
                save_later = 0;
                if (zs_save())
                    ui_say("SAVE ERROR");
                last_sig = zs_saved_sig;
            }
        }
    }
}

void fm1_cstart(void)
{
    extern uint32_t _data_start[], _data_end[], _data_load[], _bss_start[], _bss_end[];
    extern uint32_t _pool_start[], _pool_end[], _rt_start[], _rt_end[], _rt_load[];
    uint32_t *s, *d, p3, boot_mode;
    fm1_time_init();
    fm1_reset_reason();
    p3 = fm1_boot.p3_rst;
    fm1_wdt_arm(0x0D);
    if ((p3 & 1u) && !(p3 & (4u | 0x40u)))
        bootguard_clear(&bootguard);
    boot_mode = bootguard_begin(&bootguard);
    if (boot_mode == BOOT_ROM)
        fm1_enter_uboot();
    fm1_irq_init();
    for (d = _bss_start; d < _bss_end; d++) *d = 0;
    for (d = _pool_start; d < _pool_end; d++) *d = 0;
    for (s = _data_load, d = _data_start; d < _data_end; s++, d++) *d = *s;
    for (s = _rt_load, d = _rt_start; d < _rt_end; s++, d++) *d = *s;
    fm1_mailbox_clear();
    fm1_guard_enable(FM1_GUARD_STACK | FM1_GUARD_WRITE | FM1_GUARD_BUS | FM1_GUARD_PC);
    fm1_boot.p3_rst = (uint8_t)p3;
    if (boot_mode == BOOT_RECOVERY || recovery_key())
        recovery_main();                        /* OCT- at power-on: the USB rescue */
    flash_ok = FL_FAR(fl_jedec_ram)() == 0x856014u;
    ota_boot_cleanup();
    lcd_init();
    lcd_fill(0, 0, 240, 240, C_BLACK);
    fm1_input_init();
    fm1_adc_init();
    fm1_audio_init(abuf, HALF_WORDS, isr_alnk0, 3);   /* (abuf is zero: .bss) */
    usb_start();
    fm1_timer5_start(isr_timer5, 4);
    fm1_guard_lock_top();
    fm1_irq_enable_all();
    zp12_main();
    for (;;)
        fm1_service();
}
