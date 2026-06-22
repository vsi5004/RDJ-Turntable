/**
 * screens.c — N-screen ScreenKey layer.
 *
 * M1 scope ("first light"): bring up all panels, fill each a distinct color to prove the
 * shared SPI3 bus + per-screen CS, and poll the per-screen keys (active-low). Real screen
 * content (RPM / play / LVGL menu) lands in later milestones.
 *
 * One shared 32 KB framebuffer, rendered then DMA-pushed to one CS-selected panel at a
 * time, then reused — per the design log (sequential render => one buffer, not four).
 */
#include "screens.h"
#include "st7735.h"
#include "display_config.h"
#include "main.h"
#include "trace.h"

typedef enum { ROLE_PLAY, ROLE_SPEED, ROLE_MENU } screen_role_t;

typedef struct {
    st7735_t      dev;
    GPIO_TypeDef *key_port;
    uint16_t      key_pin;
    screen_role_t role;
    uint8_t       key_prev; /* last sampled pressed state, for edge detect */
} screen_t;

static screen_t screens[NUM_SCREENS] = {
    { { DISP_CS0_GPIO_Port, DISP_CS0_Pin }, KEY0_GPIO_Port, KEY0_Pin, ROLE_PLAY,  0 },
    { { DISP_CS1_GPIO_Port, DISP_CS1_Pin }, KEY1_GPIO_Port, KEY1_Pin, ROLE_SPEED, 0 },
    { { DISP_CS2_GPIO_Port, DISP_CS2_Pin }, KEY2_GPIO_Port, KEY2_Pin, ROLE_MENU,  0 },
};

/* Shared, reused off-screen buffer. In regular SRAM (CCMRAM is not DMA-accessible on F4). */
static uint16_t fb[ST7735_PIXELS];

static const uint16_t first_light[NUM_SCREENS] = { C_RED, C_GREEN, C_BLUE };

static void fill_fb(uint16_t color)
{
    for (int i = 0; i < ST7735_PIXELS; i++) fb[i] = color;
}

void screens_init(void)
{
    st7735_backlight(0);
    st7735_hw_reset();
    for (int i = 0; i < NUM_SCREENS; i++)
        st7735_init(&screens[i].dev);

    for (int i = 0; i < NUM_SCREENS; i++) {
        fill_fb(first_light[i]);
        st7735_draw(&screens[i].dev, fb);
    }

    st7735_backlight(1);
    TRACE("screens_init: %d panels up\n", NUM_SCREENS);
}

void screens_tick(void)
{
    for (int i = 0; i < NUM_SCREENS; i++) {
        screen_t *s = &screens[i];
        uint8_t pressed = (HAL_GPIO_ReadPin(s->key_port, s->key_pin) == GPIO_PIN_RESET);

        if (pressed && !s->key_prev) { /* press edge */
            TRACE("KEY%d pressed\n", i);
            fill_fb(C_WHITE);
            st7735_draw(&s->dev, fb);
        }
        s->key_prev = pressed;
    }
}
