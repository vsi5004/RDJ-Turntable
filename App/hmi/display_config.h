/**
 * display_config.h — ScreenKey (128x128 ST7735) geometry, colors, screen count.
 */
#pragma once
#include <stdint.h>

/* Waveshare 0.85" / 1.44" green-tab ST7735 panel: 128x128 with a 2,3 window offset */
#define ST7735_W          128
#define ST7735_H          128
#define ST7735_COL_OFFSET 2
#define ST7735_ROW_OFFSET 3
#define ST7735_PIXELS     (ST7735_W * ST7735_H)

/* Building for 3 now (2 status keys + 1 menu); array-driven so 4 is config-only. */
#define NUM_SCREENS 3

/*
 * Colors are RGB565 **byte-swapped**: the SPI runs 8-bit MSB-first, so we store the
 * high byte first in memory and DMA the framebuffer straight out. rgb565() does the swap.
 */
static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t c = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    return (uint16_t)((c >> 8) | (c << 8));
}

#define C_BLACK 0x0000u
#define C_WHITE 0xFFFFu
#define C_RED   0x00F8u /* 0xF800 swapped */
#define C_GREEN 0xE007u /* 0x07E0 swapped */
#define C_BLUE  0x1F00u /* 0x001F swapped */
