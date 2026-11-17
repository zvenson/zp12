/* SPDX-License-Identifier: GPL-3.0-only */
/* The 1 MiB SPI NOR parts the FM-1 has shipped with, by JEDEC id (manufacturer, memory type, capacity 0x14 = 8 Mbit):
 *   0x856014  Puya P25Q80H     the first units
 *   0xB36014  UCUN UC25HQ80    later units: a second source with the same capacity and the standard command set
 *                              fm1_flash.h uses (06 WREN, 05 / 35 RDSR, 20 4 KiB erase, 02 page program, 9F id).
 * After SLOOP #241 (isod89/sloop-fm1). The update loader (ldr_flash_known), the app (main) and the USB rescue all
 * ask here, so the three always agree. Its own header so the simulator, which stubs the driver, checks the same list. */
#pragma once
#include <stdint.h>

static inline __attribute__((always_inline)) int fl_jedec_known(uint32_t id)
{
    return id == 0x856014u || id == 0xB36014u;
}
