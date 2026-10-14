/* SPDX-License-Identifier: GPL-3.0-only */
/* zp12 simulator: the MASTER pot (the page's knob) and a full battery */
#pragma once
#include <stdint.h>
enum { FM1_ADC_BATT = 3, FM1_ADC_MASTER = 4 };
static volatile int32_t sim_master10 = 1023;   /* 0..1023 */
static void fm1_adc_init(void) {}
static int32_t fm1_adc_read(uint32_t ch) { return ch == FM1_ADC_MASTER ? sim_master10 : 700; }
