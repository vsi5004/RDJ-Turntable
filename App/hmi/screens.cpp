/**
 * screens.cpp - N-screen ScreenKey layer.
 *
 * M1 scope ("first light"): bring up all panels, fill each a distinct color to prove the
 * shared SPI3 bus + per-screen CS, and poll the per-screen keys. Real screen content
 * (RPM / play / LVGL menu) lands in later milestones.
 *
 * One shared 32 KB framebuffer, rendered then DMA-pushed to one CS-selected panel at a
 * time, then reused - per the design log (sequential render => one buffer, not four).
 */
#include "screens.h"
#include "st7735.h"
#include "display_config.h"
#include "main.h"
#include "trace.h"
#include "button.hpp"

namespace screens {
namespace {

enum class Role { Play, Speed, Menu };

struct Screen {
    st7735::Panel panel;
    gpio::Button  key;
    Role          role;
};

/* Per-screen: active-low CS + active-low key. */
Screen screens[NUM_SCREENS] = {
    { { { DISP_CS0_GPIO_Port, DISP_CS0_Pin, false } },
      gpio::Button{ gpio::InputPin{ KEY0_GPIO_Port, KEY0_Pin, false } }, Role::Play  },
    { { { DISP_CS1_GPIO_Port, DISP_CS1_Pin, false } },
      gpio::Button{ gpio::InputPin{ KEY1_GPIO_Port, KEY1_Pin, false } }, Role::Speed },
    { { { DISP_CS2_GPIO_Port, DISP_CS2_Pin, false } },
      gpio::Button{ gpio::InputPin{ KEY2_GPIO_Port, KEY2_Pin, false } }, Role::Menu  },
};

/* Shared, reused off-screen buffer. In regular SRAM (CCMRAM is not DMA-accessible on F4). */
uint16_t fb[ST7735_PIXELS];

constexpr uint16_t kFirstLight[NUM_SCREENS] = { kRed, kGreen, kBlue };

void fill_fb(uint16_t color) { for (int i = 0; i < ST7735_PIXELS; ++i) fb[i] = color; }

}  // namespace

void init()
{
    /* Backlight stays off until every panel is initialised and filled, so the
     * uninitialised panel RAM (noise) is never visible. init_all interleaves the panels
     * so the stabilisation delays are paid once, not per panel. */
    const st7735::Panel* panels[NUM_SCREENS];
    for (int i = 0; i < NUM_SCREENS; ++i) panels[i] = &screens[i].panel;

    st7735::backlight(false);
    st7735::hw_reset();
    st7735::init_all(panels, NUM_SCREENS);

    for (int i = 0; i < NUM_SCREENS; ++i) {
        fill_fb(kFirstLight[i]);
        st7735::draw(screens[i].panel, fb);
    }
    st7735::backlight(true);
    TRACE("screens_init: %d panels up\n", NUM_SCREENS);
}

void tick()
{
    for (int i = 0; i < NUM_SCREENS; ++i) {
        if (screens[i].key.fell()) { /* press edge */
            TRACE("KEY%d pressed\n", i);
            fill_fb(kWhite);
            st7735::draw(screens[i].panel, fb);
        }
    }
}

}  // namespace screens
