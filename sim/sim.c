/* SPDX-License-Identifier: GPL-3.0-only */
/* zp12 in a browser: the firmware's own sources (firmware/src/zp12.c and everything it includes, unchanged) on a
 * fake FM-1 (sim/hal: screen, keys, LEDs, flash in RAM, clock, I2S), compiled to WebAssembly by sim/build.sh
 * (zig cc, wasm32-freestanding), or natively for the test (sim/native.c). The pattern follows OberVave's web/sim,
 * Makkuro37's web/sim (M. Geisler) and winampFM's simulator (hericdk), all GPL-3.0.
 *
 * zp12.c's boot (fm1_cstart) runs as on the device, through the fake HAL; its main loop never returns, so it runs
 * as a coroutine: feeding the watchdog (every pass of the loop, fm1_service) or waiting (fm1_delay_ms) parks it
 * until the next millisecond (sim_yield). In wasm that is Binaryen's Asyncify (wasm-opt --asyncify), natively
 * ucontext. Each simulated millisecond: TIMER4 + 24000 ticks, the TIMER5 ISR (fm1_ms), one pass of the main loop.
 * The audio ISR (fm1_alnk0_irq, 128-frame halves) runs whenever sim_render needs the next half.
 *
 * Not simulated: USB (no web tools, no sample upload, no installer; the fake fm1_flash.h cuts usb.c off), the
 * battery, the LED dimming. The factory kit is in the image (build/gen/zp12_kit.h) as on the device. */
#include <stdint.h>
#include <stddef.h>

#ifdef SIM_NATIVE
#define EXPORT
#define memset zp_memset                        /* (libc.c's own, under other names: the host's libc stays) */
#define memcpy zp_memcpy
#define memcmp zp_memcmp
#else
#define EXPORT __attribute__((used, visibility("default")))
#endif

/* fm1_cstart's linker symbols: empty ranges (C data is initialised by the host / the wasm image) */
uint32_t sim_nil[1];
#define _data_start sim_nil
#define _data_end sim_nil
#define _data_load sim_nil
#define _bss_start sim_nil
#define _bss_end sim_nil
#define _pool_start sim_nil
#define _pool_end sim_nil
#define _rt_start sim_nil
#define _rt_end sim_nil
#define _rt_load sim_nil

#include "../firmware/src/zp12.c"

#undef memset
#undef memcpy
#undef memcmp

void isr_timer5(void) { fm1_timer5_irq(); }    /* (on the device: hal/fm1_isr.S wrappers) */
void isr_alnk0(void) { fm1_alnk0_irq(); }

/* ---- the main loop as a coroutine */
static volatile uint32_t booted, halted;
static void sim_boot(void) { fm1_cstart(); }
static void (*volatile sim_boot_p)(void) = sim_boot;   /* (through a pointer: fm1_cstart never returns) */

#ifdef SIM_NATIVE
#include <ucontext.h>
#include <stdlib.h>
static ucontext_t co_main, co_app;
static void sim_yield(void) { swapcontext(&co_app, &co_main); }
static void sim_pass(void)
{
    if (!booted) {
        static char *stack;
        booted = 1;
        stack = malloc(1u << 20);
        getcontext(&co_app);
        co_app.uc_stack.ss_sp = stack;
        co_app.uc_stack.ss_size = 1u << 20;
        co_app.uc_link = &co_main;
        makecontext(&co_app, (void (*)(void))sim_boot, 0);
    }
    swapcontext(&co_main, &co_app);
}
#else
__attribute__((import_module("asyncify"), import_name("start_unwind"))) void asyncify_start_unwind(void *p);
__attribute__((import_module("asyncify"), import_name("stop_unwind"))) void asyncify_stop_unwind(void);
__attribute__((import_module("asyncify"), import_name("start_rewind"))) void asyncify_start_rewind(void *p);
__attribute__((import_module("asyncify"), import_name("stop_rewind"))) void asyncify_stop_rewind(void);
static struct { void *pos, *end; } aco;          /* the parked loop's locals (Asyncify's data) */
static uint8_t aco_stack[32768] __attribute__((aligned(8)));
static volatile uint32_t parked;
/* not inlined: Asyncify instruments the callers of the function that unwinds */
__attribute__((noinline)) static void sim_yield(void)
{
    if (!parked) {
        parked = 1;
        asyncify_start_unwind(&aco);
    } else {
        parked = 0;
        asyncify_stop_rewind();
    }
}
__attribute__((noinline)) static void sim_pass(void)
{
    if (!booted) {
        booted = 1;
        aco.pos = aco_stack;
        aco.end = aco_stack + sizeof aco_stack;
    } else
        asyncify_start_rewind(&aco);
    sim_boot_p();
    asyncify_stop_unwind();
}
#endif

static void sim_halt(uint32_t why)              /* a reboot / UBOOT / update request: the FM-1 stops here */
{
    halted = why;
    for (;;)
        sim_yield();
}

/* ---- the panel: debounced state and edges as fm1_input_tick leaves them; a key or button let go before a
 * pass of the main loop saw it is let go after that pass (a tap within one audio quantum still counts) */
static uint32_t key_new, key_rel_late, btn_rel_late;
EXPORT void sim_key(uint32_t k, uint32_t down)  /* the 27 keys: 0 = F3 .. 26 = G5 */
{
    uint32_t bit = 1u << k;
    if (k >= 27u) return;
    if (down) {
        fm1_in.notes |= bit;
        fm1_in.notes_pressed |= bit;
        key_new |= bit;
        key_rel_late &= ~bit;
    } else if (key_new & bit)
        key_rel_late |= bit;
    else
        fm1_in.notes &= ~bit;
}
EXPORT void sim_button(uint32_t b, uint32_t down)   /* 0 OCT- 1 OCT+ 2 FX 3 SEL 4 ENV 5 LFO 6 EDIT 7 GLO 8 HOME
                                                       9 SAVE 10 ARP 11 SEQ 12 PLAY 13 REC */
{
    uint32_t bit = 1u << b;
    if (b >= 14u) return;
    if (down) {
        fm1_in.buttons |= bit;
        fm1_in.pressed |= bit;
        btn_rel_late &= ~bit;
    } else if (fm1_in.pressed & bit)
        btn_rel_late |= bit;
    else if (fm1_in.buttons & bit) {
        fm1_in.buttons &= ~bit;
        fm1_in.released |= bit;
    }
}
EXPORT void sim_enc(uint32_t e, int32_t steps)  /* 0 SELECT 1 ALGORITHM 2-5 KNOB 1-4 6 PRESETS; + = clockwise */
{
    if (e < FM1_NENC) fm1_in.enc_steps[e] = (int16_t)(fm1_in.enc_steps[e] + steps);
}
EXPORT void sim_midi(uint32_t st, uint32_t d1, uint32_t d2)   /* into the USB-MIDI in queue, as usb.c puts it */
{
    if (st < 0x80u || st >= 0xF0u || mi_w - mi_r >= MQ) return;
    midi_in_q[mi_w % MQ] = (st >> 4) | st << 8 | (d1 & 0x7Fu) << 16 | (d2 & 0x7Fu) << 24;
    mi_w++;
}
EXPORT void sim_master(uint32_t k10)            /* the MASTER pot, 0..1023 */
{
    sim_master10 = (int32_t)(k10 > 1023u ? 1023u : k10);
}

/* ---- the flash: erased at first, then the page may put the saved store back (before sim_init) */
#define SIM_STORE_LO 0xC4000u                   /* sp_store.c's two copies, sp_samples.c's directory */
#define SIM_STORE_HI 0xDA000u
static uint32_t flash_ready;
static void flash_prep(void)
{
    uint32_t i;
    if (flash_ready) return;
    flash_ready = 1;
    for (i = 0; i < sizeof sim_flash; i++) sim_flash[i] = 0xFF;
}
EXPORT uint8_t *sim_store(void) { flash_prep(); return sim_flash + SIM_STORE_LO; }
EXPORT uint32_t sim_store_size(void) { return SIM_STORE_HI - SIM_STORE_LO; }
EXPORT uint32_t sim_store_writes(void) { return sim_flash_writes; }   /* changes when the app wrote the flash */

/* ---- the clock: a millisecond = TIMER4 + 24000, the TIMER5 ISR, one pass of the main loop */
static void sim_ms(void)
{
    sim_ticks += 1000u * FM1_TICKS_PER_US;
    if (sim_timer_isr) sim_timer_isr();
    if (!halted) {
        key_new = 0;
        sim_pass();
        if (key_rel_late) { fm1_in.notes &= ~key_rel_late; key_rel_late = 0; }
        if (btn_rel_late) {
            fm1_in.buttons &= ~btn_rel_late;
            fm1_in.released |= btn_rel_late;
            btn_rel_late = 0;
        }
    }
}

#define SIM_MAXN 1024u
static float out_l[SIM_MAXN], out_r[SIM_MAXN];
static uint32_t half_pos = 1u << 30, ms_acc;
static int32_t peak;

EXPORT int sim_init(uint32_t rate)              /* boots the FM-1 (44100 only: the engine's rate) */
{
    if (rate != SP_FS || booted) return booted ? 0 : -1;
    flash_prep();
    sim_pass();
    return 0;
}
EXPORT uint32_t sim_rate(void) { return SP_FS; }
EXPORT float *sim_out_l(void) { return out_l; }
EXPORT float *sim_out_r(void) { return out_r; }
EXPORT void sim_render(uint32_t n)              /* n frames (<= 1024) into sim_out_l / sim_out_r, -1..1 */
{
    uint32_t i;
    if (!booted) return;
    for (i = 0; i < n && i < SIM_MAXN; i++) {
        int32_t l = 0, r = 0;
        if (sim_aon && sim_abuf) {
            if (half_pos >= sim_ahalf_words / 2u) {   /* the I2S wants the next half: the ISR fills it */
                sim_ahalf ^= 1u;
                sim_audio_isr();
                half_pos = 0;
            }
            l = sim_abuf[sim_ahalf * sim_ahalf_words + 2u * half_pos] >> 8;
            r = sim_abuf[sim_ahalf * sim_ahalf_words + 2u * half_pos + 1u] >> 8;
            half_pos++;
            if (l > peak) peak = l; else if (-l > peak) peak = -l;
            if (r > peak) peak = r; else if (-r > peak) peak = -r;
        }
        out_l[i] = (float)l * (1.0f / 32768.0f);
        out_r[i] = (float)r * (1.0f / 32768.0f);
        if ((ms_acc += 1000u) >= SP_FS) {
            ms_acc -= SP_FS;
            sim_ms();
        }
    }
}
EXPORT uint32_t sim_now(void) { return fm1_ms; }
EXPORT uint32_t sim_halted(void) { return halted; }
EXPORT int32_t sim_peak(void) { int32_t p = peak; peak = 0; return p; }   /* loudest sample since the last call (Q15) */

/* ---- what the panel shows */
EXPORT uint16_t *sim_screen(void) { return sim_fb; }   /* 240 x 240 RGB565, row by row */
EXPORT uint32_t sim_fb_hash(void)
{
    uint32_t h = 2166136261u, i;
    for (i = 0; i < 240u * 240u; i++) h = (h ^ sim_fb[i]) * 16777619u;
    return h;
}
static uint32_t led_bits(const uint8_t *cols, uint32_t keys)   /* the LED columns back to ids (FM1_KEYMAP) */
{
    uint32_t c, r, v = 0;
    for (c = 0; c < FM1_NCOL; c++)
        for (r = 1; r < 5u; r++) {
            int32_t id = FM1_KEYMAP[r][c];
            if (id < 0 || !((cols[c] >> r) & 1u)) continue;
            if (keys && id >= 14) v |= 1u << (id - 14);
            if (!keys && id < 14) v |= 1u << id;
        }
    return v;
}
EXPORT uint32_t sim_leds_buttons(void) { return led_bits(fm1_led, 0); }   /* bit b: button b lit */
EXPORT uint32_t sim_leds_keys(void) { return led_bits(fm1_led, 1); }      /* bit k: key k lit */
EXPORT uint32_t sim_glow_keys(void) { return led_bits(fm1_led_dim, 1); }  /* the dim layer */
EXPORT uint32_t sim_page(void) { return ui.page; }
EXPORT uint32_t sim_playing(void) { return sq.playing; }
EXPORT uint32_t sim_recording(void) { return sq.recording | (uint32_t)sq.rec_arm << 1; }
EXPORT uint32_t sim_loop_events(void) { return sq_seg[sq.seg].n; }   /* hits in the current loop */
EXPORT uint32_t sim_bpm10(void) { return sq.bpm10; }
