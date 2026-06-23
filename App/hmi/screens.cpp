/**
 * screens.cpp - N-screen ScreenKey layer.
 *
 * M1 scope ("first light"): bring up all panels, fill each a distinct color to prove the
 * shared SPI3 bus + per-screen CS. Real screen content
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

#include <cstring>

namespace screens {
namespace {

struct Screen {
    st7735::Panel panel;
};

/* Per-screen: active-low CS + active-low key. */
Screen screens[NUM_SCREENS] = {
    { { { DISP_CS0_GPIO_Port, DISP_CS0_Pin, false } } },
    { { { DISP_CS1_GPIO_Port, DISP_CS1_Pin, false } } },
    { { { DISP_CS2_GPIO_Port, DISP_CS2_Pin, false } } },
};

/* Shared, reused off-screen buffer. In regular SRAM (CCMRAM is not DMA-accessible on F4). */
uint16_t fb[ST7735_PIXELS];

constexpr uint16_t kFirstLight[NUM_SCREENS] = { kRed, kGreen, kBlue };
constexpr uint16_t kBackground = rgb565(8, 10, 14);
constexpr uint16_t kMuted = rgb565(105, 110, 120);

constexpr uint8_t kDigits[10][7] = {
    {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14}, {14,17,1,2,4,8,31},
    {30,1,1,14,1,1,30}, {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
    {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8}, {14,17,17,14,17,17,14},
    {14,17,17,15,1,1,14},
};

constexpr uint8_t kLetters[26][7] = {
    {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30}, {14,17,16,16,16,17,14},
    {30,17,17,17,17,17,30}, {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
    {14,17,16,23,17,17,15}, {17,17,17,31,17,17,17}, {14,4,4,4,4,4,14},
    {7,2,2,2,18,18,12}, {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17}, {14,17,17,17,17,17,14},
    {30,17,17,30,16,16,16}, {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4}, {17,17,17,17,17,17,14},
    {17,17,17,17,17,10,4}, {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31},
};

void fill_fb(uint16_t color) { for (int i = 0; i < ST7735_PIXELS; ++i) fb[i] = color; }

void fill_rect(int x, int y, int w, int h, uint16_t color)
{
    for (int py = y; py < y + h; ++py) {
        if (py < 0 || py >= ST7735_H) continue;
        for (int px = x; px < x + w; ++px) {
            if (px >= 0 && px < ST7735_W) fb[py * ST7735_W + px] = color;
        }
    }
}

uint8_t glyph_row(char c, int row)
{
    if (c >= '0' && c <= '9') return kDigits[c - '0'][row];
    if (c >= 'A' && c <= 'Z') return kLetters[c - 'A'][row];
    if (c == '-') return row == 3 ? 31 : 0;
    if (c == '/') return static_cast<uint8_t>(1u << (row < 5 ? 4 - row : 0));
    return 0;
}

void draw_char(int x, int y, char c, int scale, uint16_t color)
{
    for (int row = 0; row < 7; ++row) {
        const uint8_t bits = glyph_row(c, row);
        for (int col = 0; col < 5; ++col) {
            if (bits & (1u << (4 - col)))
                fill_rect(x + col * scale, y + row * scale, scale, scale, color);
        }
    }
}

void draw_text_centered(int y, const char* text, int scale, uint16_t color)
{
    const int length = static_cast<int>(std::strlen(text));
    if (length == 0) return;
    const int width = length * 6 * scale - scale;
    int x = (ST7735_W - width) / 2;
    for (int i = 0; i < length; ++i) {
        draw_char(x, y, text[i], scale, color);
        x += 6 * scale;
    }
}

bool same(const hmi::KeyView& a, const hmi::KeyView& b)
{
    return a.accent == b.accent && a.enabled == b.enabled
        && std::strcmp(a.header, b.header) == 0
        && std::strcmp(a.action, b.action) == 0
        && std::strcmp(a.detail, b.detail) == 0;
}

void render_key(const hmi::KeyView& view)
{
    fill_fb(kBackground);
    const uint16_t accent = view.enabled ? view.accent : kMuted;
    fill_rect(0, 0, ST7735_W, 3, accent);
    fill_rect(0, ST7735_H - 3, ST7735_W, 3, accent);
    fill_rect(0, 0, 3, ST7735_H, accent);
    fill_rect(ST7735_W - 3, 0, 3, ST7735_H, accent);
    draw_text_centered(13, view.header, 1, kMuted);
    const int action_scale = std::strlen(view.action) <= 7 ? 3 : 2;
    draw_text_centered(46, view.action, action_scale, accent);
    draw_text_centered(105, view.detail, 1, view.enabled ? kWhite : kMuted);
}

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

void show(const hmi::View& view)
{
    static hmi::View previous;
    static bool first = true;
    for (int i = 0; i < NUM_SCREENS; ++i) {
        if (!first && same(view.keys[i], previous.keys[i])) continue;
        render_key(view.keys[i]);
        st7735::draw(screens[i].panel, fb);
        previous.keys[i] = view.keys[i];
    }
    first = false;
}

void tick()
{
    /* Rendering is driven by application snapshots. Physical keys have one owner in app.cpp. */
}

}  // namespace screens
