#pragma once

#include <cstdint>

namespace hmi {

enum class IconId : uint8_t {
    None,
    Play,
    Pause,
    Stop,
    Home,
    Settings,
    Back,
    Next,
    Confirm,
    Warning,
};

enum class FontId : uint8_t { Small, Medium, Large };

namespace assets {

struct Glyph {
    uint32_t alpha_offset;
    uint8_t width;
    uint8_t height;
};

struct Font {
    const uint8_t* alpha;
    const Glyph* glyphs;
    uint8_t first_character;
    uint8_t last_character;
    uint8_t line_height;
};

struct Mask {
    const uint8_t* alpha;
    uint8_t width;
    uint8_t height;
};

struct HoldRing {
    Mask mask;
    const uint8_t* segments;
    uint8_t segment_count;
};

const Font& font(FontId id);
const Mask& icon(IconId id);
const HoldRing& hold_ring();

}  // namespace assets
}  // namespace hmi
