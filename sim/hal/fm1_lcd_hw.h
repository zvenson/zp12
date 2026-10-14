/* SPDX-License-Identifier: GPL-3.0-only */
/* zp12 simulator: the ST7789 behind SPI1. lcd.c's commands and pixel transfers are decoded as the panel does
 * (CASET 0x2A, RASET 0x2B, RAMWR 0x2C, big-endian RGB565) into sim_fb, 240 x 240; a transfer completes at once. */
#pragma once
#include <stdint.h>
#include "fm1_cc.h"
static uint16_t sim_fb[240 * 240];
static struct { uint8_t cmd, hi, odd; uint16_t x0, x1, y0, y1, x, y; } sim_lcd;
static uint32_t fm1_lcd_timeouts;
static inline void fm1_lcd_hw_init(void) {}
static inline void fm1_lcd_baud(uint32_t b) { (void)b; }
static inline void fm1_lcd_wait(void) {}
static inline void fm1_lcd_deselect(void) {}
static void fm1_lcd_send_cmd(uint8_t c)
{
    sim_lcd.cmd = c;
    sim_lcd.odd = 0;
    if (c == 0x2Cu) { sim_lcd.x = sim_lcd.x0; sim_lcd.y = sim_lcd.y0; }
}
static void fm1_lcd_send_data(const void *p, uint32_t n)
{
    const uint8_t *b = p;
    uint32_t i;
    if (sim_lcd.cmd == 0x2Au && n >= 4u) {
        sim_lcd.x0 = (uint16_t)(b[0] << 8 | b[1]); sim_lcd.x1 = (uint16_t)(b[2] << 8 | b[3]);
    } else if (sim_lcd.cmd == 0x2Bu && n >= 4u) {
        sim_lcd.y0 = (uint16_t)(b[0] << 8 | b[1]); sim_lcd.y1 = (uint16_t)(b[2] << 8 | b[3]);
    } else if (sim_lcd.cmd == 0x2Cu) {
        for (i = 0; i < n; i++) {
            if (!sim_lcd.odd) { sim_lcd.hi = b[i]; sim_lcd.odd = 1; continue; }
            sim_lcd.odd = 0;
            if (sim_lcd.x < 240u && sim_lcd.y < 240u)
                sim_fb[sim_lcd.y * 240u + sim_lcd.x] = (uint16_t)(sim_lcd.hi << 8 | b[i]);
            if (sim_lcd.x >= sim_lcd.x1) {
                sim_lcd.x = sim_lcd.x0;
                sim_lcd.y = sim_lcd.y >= sim_lcd.y1 ? sim_lcd.y0 : (uint16_t)(sim_lcd.y + 1u);
            } else
                sim_lcd.x++;
        }
    }
}
