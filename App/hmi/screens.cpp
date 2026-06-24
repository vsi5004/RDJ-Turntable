/**
 * screens.cpp - array-driven ScreenKey renderer using generated antialiased masks.
 *
 * One shared 32 KB RGB565 framebuffer is composed and DMA-pushed to one panel at a time. Static
 * icons and glyph coverage are generated offline at 4x resolution and stored as packed 4-bit alpha;
 * runtime rendering only performs bounded integer blending.
 */
#include "screens.h"

#include "assets.hpp"
#include "display_config.h"
#include "main.h"
#include "st7735.h"
#include "trace.h"

#include <cstring>

namespace screens {
namespace {

struct Screen {
    st7735::Panel panel;
};

Screen screens[NUM_SCREENS] = {
    { { { DISP_CS0_GPIO_Port, DISP_CS0_Pin, false } } },
    { { { DISP_CS1_GPIO_Port, DISP_CS1_Pin, false } } },
    { { { DISP_CS2_GPIO_Port, DISP_CS2_Pin, false } } },
};

/* Regular SRAM: CCMRAM is not DMA-accessible on STM32F4. */
uint16_t fb[ST7735_PIXELS];

constexpr uint16_t kFirstLight[NUM_SCREENS] = { kRed, kGreen, kBlue };
constexpr uint16_t kBackground = rgb565(8, 10, 14);
constexpr uint16_t kMuted = rgb565(105, 110, 120);
constexpr uint16_t kRingInactive = rgb565(42, 45, 52);

uint16_t swap_bytes(uint16_t value)
{
    return static_cast<uint16_t>((value >> 8) | (value << 8));
}

uint16_t blend(uint16_t background, uint16_t foreground, uint8_t alpha)
{
    if (alpha == 0) return background;
    if (alpha >= 15) return foreground;

    const uint16_t bg = swap_bytes(background);
    const uint16_t fg = swap_bytes(foreground);
    const uint16_t inverse = static_cast<uint16_t>(15 - alpha);
    const uint16_t red = static_cast<uint16_t>((((bg >> 11) & 0x1f) * inverse
                                                + ((fg >> 11) & 0x1f) * alpha + 7) / 15);
    const uint16_t green = static_cast<uint16_t>((((bg >> 5) & 0x3f) * inverse
                                                  + ((fg >> 5) & 0x3f) * alpha + 7) / 15);
    const uint16_t blue = static_cast<uint16_t>(((bg & 0x1f) * inverse
                                                 + (fg & 0x1f) * alpha + 7) / 15);
    return swap_bytes(static_cast<uint16_t>((red << 11) | (green << 5) | blue));
}

uint8_t alpha_at(const uint8_t* packed, uint32_t pixel)
{
    const uint8_t byte = packed[pixel / 2];
    return (pixel & 1u) == 0 ? static_cast<uint8_t>(byte >> 4)
                             : static_cast<uint8_t>(byte & 0x0f);
}

void fill_fb(uint16_t color)
{
    for (int index = 0; index < ST7735_PIXELS; ++index) fb[index] = color;
}

void fill_rect(int x, int y, int width, int height, uint16_t color)
{
    for (int py = y; py < y + height; ++py) {
        if (py < 0 || py >= ST7735_H) continue;
        for (int px = x; px < x + width; ++px) {
            if (px >= 0 && px < ST7735_W) fb[py * ST7735_W + px] = color;
        }
    }
}

void draw_line(int x0, int y0, int x1, int y1, uint16_t color)
{
    const int dx = x1 >= x0 ? x1 - x0 : x0 - x1;
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -(y1 >= y0 ? y1 - y0 : y0 - y1);
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    while (true) {
        if (x0 >= 0 && x0 < ST7735_W && y0 >= 0 && y0 < ST7735_H)
            fb[y0 * ST7735_W + x0] = color;
        if (x0 == x1 && y0 == y1) break;
        const int twice_error = 2 * error;
        if (twice_error >= dy) {
            error += dy;
            x0 += sx;
        }
        if (twice_error <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

void draw_mask(int x, int y, const hmi::assets::Mask& mask, uint16_t color)
{
    for (uint32_t row = 0; row < mask.height; ++row) {
        const int py = y + static_cast<int>(row);
        if (py < 0 || py >= ST7735_H) continue;
        for (uint32_t column = 0; column < mask.width; ++column) {
            const int px = x + static_cast<int>(column);
            if (px < 0 || px >= ST7735_W) continue;
            const uint8_t alpha = alpha_at(mask.alpha, row * mask.width + column);
            if (alpha != 0)
                fb[py * ST7735_W + px] = blend(fb[py * ST7735_W + px], color, alpha);
        }
    }
}

const hmi::assets::Glyph& glyph(const hmi::assets::Font& font, char character)
{
    uint8_t code = static_cast<uint8_t>(character);
    if (code < font.first_character || code > font.last_character)
        code = static_cast<uint8_t>('?');
    return font.glyphs[code - font.first_character];
}

int text_width(const hmi::assets::Font& font, const char* text)
{
    int width = 0;
    for (const char* character = text; *character != '\0'; ++character)
        width += glyph(font, *character).width;
    return width;
}

void draw_glyph(int x, int y, const hmi::assets::Font& font,
                const hmi::assets::Glyph& value, uint16_t color)
{
    for (uint32_t row = 0; row < value.height; ++row) {
        const int py = y + static_cast<int>(row);
        if (py < 0 || py >= ST7735_H) continue;
        for (uint32_t column = 0; column < value.width; ++column) {
            const int px = x + static_cast<int>(column);
            if (px < 0 || px >= ST7735_W) continue;
            const uint32_t pixel = value.alpha_offset + row * value.width + column;
            const uint8_t alpha = alpha_at(font.alpha, pixel);
            if (alpha != 0)
                fb[py * ST7735_W + px] = blend(fb[py * ST7735_W + px], color, alpha);
        }
    }
}

void draw_text_centered(int y, const char* text, hmi::FontId font_id, uint16_t color)
{
    const hmi::assets::Font& font = hmi::assets::font(font_id);
    int x = (ST7735_W - text_width(font, text)) / 2;
    for (const char* character = text; *character != '\0'; ++character) {
        const hmi::assets::Glyph& value = glyph(font, *character);
        draw_glyph(x, y, font, value, color);
        x += value.width;
    }
}

hmi::FontId fitting_font(const char* text, hmi::FontId preferred)
{
    if (text_width(hmi::assets::font(preferred), text) <= ST7735_W - 8) return preferred;
    if (preferred == hmi::FontId::Large
        && text_width(hmi::assets::font(hmi::FontId::Medium), text) <= ST7735_W - 8)
        return hmi::FontId::Medium;
    return hmi::FontId::Small;
}

void draw_hold_ring(int x, int y, uint8_t progress, uint16_t accent)
{
    const hmi::assets::HoldRing& ring = hmi::assets::hold_ring();
    for (uint32_t row = 0; row < ring.mask.height; ++row) {
        for (uint32_t column = 0; column < ring.mask.width; ++column) {
            const uint32_t pixel = row * ring.mask.width + column;
            const uint8_t alpha = alpha_at(ring.mask.alpha, pixel);
            if (alpha == 0) continue;
            const int px = x + static_cast<int>(column);
            const int py = y + static_cast<int>(row);
            const uint16_t color = ring.segments[pixel] < progress ? accent : kRingInactive;
            fb[py * ST7735_W + px] = blend(fb[py * ST7735_W + px], color, alpha);
        }
    }
}

void draw_speed_sparkline(const hmi::SpeedSparkline& sparkline, uint16_t accent)
{
    constexpr int kLeft = 10;
    constexpr int kRight = ST7735_W - 11;
    constexpr int kCenterY = 109;
    constexpr int kAmplitude = 10;
    fill_rect(kLeft, kCenterY, kRight - kLeft + 1, 1, kRingInactive);

    if (sparkline.count < 2) {
        fill_rect(26, kCenterY, 76, 2, accent);
        return;
    }

    int previous_x = kLeft;
    int previous_y = kCenterY
        - static_cast<int>(sparkline.samples[0]) * kAmplitude / 127;
    for (uint32_t index = 1; index < sparkline.count; ++index) {
        const int x = kLeft + static_cast<int>(index) * (kRight - kLeft)
            / static_cast<int>(sparkline.count - 1u);
        const int y = kCenterY
            - static_cast<int>(sparkline.samples[index]) * kAmplitude / 127;
        draw_line(previous_x, previous_y, x, y, accent);
        draw_line(previous_x, previous_y + 1, x, y + 1, accent);
        previous_x = x;
        previous_y = y;
    }
}

bool same(const hmi::KeyView& first, const hmi::KeyView& second)
{
    return first.accent == second.accent && first.enabled == second.enabled
        && first.icon_color == second.icon_color && first.icon == second.icon
        && first.hold_progress == second.hold_progress
        && first.hold_available == second.hold_available
        && first.speed_sparkline.count == second.speed_sparkline.count
        && std::memcmp(first.speed_sparkline.samples, second.speed_sparkline.samples,
                       sizeof(first.speed_sparkline.samples)) == 0
        && std::strcmp(first.header, second.header) == 0
        && std::strcmp(first.action, second.action) == 0
        && std::strcmp(first.detail, second.detail) == 0;
}

void render_key(const hmi::KeyView& view)
{
    fill_fb(kBackground);
    const uint16_t accent = view.enabled ? view.accent : kMuted;
    const uint16_t primary = view.enabled ? kWhite : kMuted;

    if (view.icon != hmi::IconId::None) {
        if (view.hold_available) {
            const hmi::assets::HoldRing& ring = hmi::assets::hold_ring();
            draw_hold_ring((ST7735_W - ring.mask.width) / 2, 2,
                           view.hold_progress, accent);
        }
        const hmi::assets::Mask& icon = hmi::assets::icon(view.icon);
        const uint16_t icon_color = view.icon_color != 0
            ? view.icon_color : (view.hold_available ? primary : accent);
        draw_mask((ST7735_W - icon.width) / 2, 8, icon,
                  icon_color);
        const hmi::FontId action_font = fitting_font(view.action, hmi::FontId::Medium);
        draw_text_centered(76, view.action, action_font, primary);
        draw_text_centered(104, view.detail, hmi::FontId::Small,
                           view.enabled ? accent : kMuted);
        return;
    }

    const hmi::FontId action_font = fitting_font(view.action, hmi::FontId::Large);
    draw_text_centered(25, view.action, action_font, accent);
    draw_text_centered(73, view.detail, hmi::FontId::Small, primary);
    /* Speed telemetry remains live while RPM selection is temporarily disabled during transport
     * transitions such as raising the tonearm for pause. Interactivity and observability are
     * intentionally independent. */
    if (view.speed_sparkline.count >= 2)
        draw_speed_sparkline(view.speed_sparkline, view.accent);
    else if (view.enabled)
        draw_speed_sparkline(view.speed_sparkline, accent);
}

}  // namespace

void init()
{
    const st7735::Panel* panels[NUM_SCREENS];
    for (int index = 0; index < NUM_SCREENS; ++index) panels[index] = &screens[index].panel;

    st7735::backlight_init();
    st7735::backlight(0);
    st7735::hw_reset();
    st7735::init_all(panels, NUM_SCREENS);

    for (int index = 0; index < NUM_SCREENS; ++index) {
        fill_fb(kFirstLight[index]);
        st7735::draw(screens[index].panel, fb);
    }
    st7735::backlight(255);
    TRACE("screens_init: %d panels up\n", NUM_SCREENS);
}

void set_brightness(uint8_t duty)
{
    st7735::backlight(duty);
}

void show(const hmi::View& view)
{
    static hmi::View previous;
    static bool first = true;
    for (int index = 0; index < NUM_SCREENS; ++index) {
        if (!first && same(view.keys[index], previous.keys[index])) continue;
        render_key(view.keys[index]);
        st7735::draw(screens[index].panel, fb);
        previous.keys[index] = view.keys[index];
    }
    first = false;
}

void tick()
{
    /* Rendering is snapshot-driven. Physical keys have one owner in app.cpp. */
}

}  // namespace screens
