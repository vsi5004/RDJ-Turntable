/**
 * st7735.h — minimal ST7735 driver over the shared SPI3 bus.
 *
 * The bus (SPI3 SCK/MOSI), DC, RST and backlight are shared across all panels;
 * each panel is selected by its own CS. A panel is described by st7735_t (its CS).
 */
#pragma once
#include "main.h" /* HAL types + CubeMX GPIO label macros (DISP_*, KEYx) */
#include <stdint.h>

typedef struct {
    GPIO_TypeDef *cs_port;
    uint16_t      cs_pin;
} st7735_t;

void st7735_hw_reset(void);                  /* pulse the shared RST (resets all panels) */
void st7735_backlight(uint8_t on);           /* shared backlight on/off */
void st7735_init(const st7735_t *d);         /* run init sequence on one CS-selected panel */
void st7735_fill(const st7735_t *d, uint16_t color);     /* solid fill (blocking) */
void st7735_draw(const st7735_t *d, const uint16_t *fb); /* push 128x128 framebuffer via DMA */
