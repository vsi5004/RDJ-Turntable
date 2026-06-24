/**
 * st7735.h - minimal ST7735 driver over the shared SPI3 bus.
 *
 * The bus (SPI3 SCK/MOSI), DC, RST and backlight are shared across all panels;
 * each panel is selected by its own (active-low) CS, held by Panel.
 */
#pragma once
#include "main.h" /* HAL types + CubeMX GPIO label macros */
#include "gpio.hpp"
#include <cstdint>

namespace st7735 {

struct Panel {
    gpio::OutputPin cs; /* active-low chip select */
};

void hw_reset();                               /* pulse shared RST (resets all panels) */
void backlight_init();                         /* start shared TIM4_CH1 PWM at zero duty */
void backlight(uint8_t duty);                  /* shared backlight, 0=off and 255=full */
void init_all(const Panel** panels, int count);/* init all panels, sharing stabilisation delays */
void fill(const Panel& p, uint16_t color);     /* solid fill (blocking) */
void draw(const Panel& p, const uint16_t* fb); /* push 128x128 framebuffer via DMA */

}  // namespace st7735
