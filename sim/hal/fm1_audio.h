/* SPDX-License-Identifier: GPL-3.0-only */
/* zp12 simulator: the I2S out. The app's buffer of two halves (L, R, 24 bit left-justified in 32); sim.c calls
 * the ALNK0 ISR whenever it needs the next half and takes it from there. */
#pragma once
#include <stdint.h>
#include "fm1_cc.h"
#define FM1_AUDIO_HALF 0x80u
static int32_t *sim_abuf;
static uint32_t sim_ahalf_words, sim_ahalf, sim_aon;
static void (*sim_audio_isr)(void);
static void fm1_audio_init(int32_t *buf, uint32_t half_words, void (*isr)(void), uint32_t prio)
{
    (void)prio;
    sim_abuf = buf;
    sim_ahalf_words = half_words;
    sim_audio_isr = isr;
    sim_aon = 1;
}
static inline uint8_t fm1_audio_pending(void) { return FM1_AUDIO_HALF; }
static inline void fm1_audio_ack_aux(uint8_t p) { (void)p; }
static inline uint32_t fm1_audio_free_half(void) { return sim_ahalf; }
static inline void fm1_audio_ack_half(void) {}
static inline void fm1_audio_stop(void) { sim_aon = 0; }
