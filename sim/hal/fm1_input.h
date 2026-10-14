/* SPDX-License-Identifier: GPL-3.0-only */
/* zp12 simulator: the key / button / encoder matrix and the LEDs. sim.c sets the debounced state (fm1_in.notes,
 * .buttons), the edges and the encoder steps; the app's LED columns (fm1_led, fm1_led_dim) are read back through
 * FM1_KEYMAP, the device's matrix (firmware/hal/fm1_input.h). */
#pragma once
#include <stdint.h>
#include "fm1_time.h"
#define FM1_NCOL 11u
#define FM1_NKEY 41u              /* ids: 0..13 buttons, 14..40 note keys */
#define FM1_NENC 7u
static const int8_t FM1_KEYMAP[6][FM1_NCOL] = {
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},
    {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},
    {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},
    { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
};
enum { FM1_BTN_OCT_DOWN = 0, FM1_BTN_OCT_UP = 1 };
static volatile struct {
    uint32_t notes, buttons, pressed, released, notes_pressed, frames;
    int16_t enc_steps[FM1_NENC];
} fm1_in;
static uint8_t fm1_led[FM1_NCOL], fm1_led_dim[FM1_NCOL], fm1_led_bg[FM1_NCOL];
static volatile uint16_t fm1_led_bg_ns;
static void fm1_input_init(void) {}
static void fm1_input_scan(void) { fm1_in.frames++; }
static void fm1_input_tick(void) {}
static int32_t fm1_enc_take(uint32_t e)
{
    int32_t s = fm1_in.enc_steps[e % FM1_NENC];
    fm1_in.enc_steps[e % FM1_NENC] = 0;
    return s;
}
static uint32_t fm1_input_edges(uint32_t *released)
{
    uint32_t p = fm1_in.pressed;
    if (released)
        *released = fm1_in.released;
    fm1_in.pressed = fm1_in.released = 0;
    return p;
}
static uint32_t fm1_input_note_edges(void)
{
    uint32_t p = fm1_in.notes_pressed;
    fm1_in.notes_pressed = 0;
    return p;
}
static void fm1_led_key(uint32_t id, int on)
{
    uint32_t p, r;
    for (p = 0; p < FM1_NCOL; p++)
        for (r = 1; r < 5u; r++)
            if (FM1_KEYMAP[r][p] == (int8_t)id) {
                if (on) fm1_led[p] |= (uint8_t)(1u << r);
                else fm1_led[p] &= (uint8_t)~(1u << r);
            }
}
