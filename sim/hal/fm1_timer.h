/* SPDX-License-Identifier: GPL-3.0-only */
/* zp12 simulator: TIMER5. Started, sim.c calls its ISR once per simulated millisecond (the device: 10 kHz;
 * fm1_timer5_irq counts the milliseconds from TIMER4 either way). */
#pragma once
#include <stdint.h>
#include "fm1_cc.h"
static void (*sim_timer_isr)(void);
static void fm1_timer5_start(void (*isr)(void), uint32_t prio) { (void)prio; sim_timer_isr = isr; }
static inline void fm1_timer5_ack(void) {}
