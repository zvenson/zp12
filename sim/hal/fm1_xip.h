/* SPDX-License-Identifier: GPL-3.0-only */
/* zp12 simulator: the flash is 1 MiB of RAM (sim_flash); the XIP window points into it */
#pragma once
#include <stdint.h>
#include "fm1_cc.h"
static uint8_t sim_flash[0x100000];
#define FM1_XIP(off) ((uintptr_t)(sim_flash + ((uint32_t)(off) & 0xFFFFFu)))
FM1_INLINE const uint8_t *fm1_xip_ptr(uint32_t off) { return (const uint8_t *)FM1_XIP(off); }
