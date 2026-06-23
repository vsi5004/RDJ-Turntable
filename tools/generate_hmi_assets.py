#!/usr/bin/env python3
"""Generate antialiased ScreenKey masks, glyph atlases, and a visual preview."""

from __future__ import annotations

import argparse
import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
OUTPUT_CPP = ROOT / "App/hmi/generated/hmi_assets.cpp"
PREVIEW_PNG = ROOT / "docs/hmi-minimal-preview.png"
FONT_REGULAR = Path("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf")
FONT_BOLD = Path("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf")
SCALE = 4
ICON_SIZE = 48
RING_SIZE = 58
RING_SEGMENTS = 32
FIRST_CHARACTER = 32
LAST_CHARACTER = 90


def downsample(image: Image.Image, size: tuple[int, int]) -> Image.Image:
    return image.resize(size, Image.Resampling.LANCZOS)


def packed_alpha(image: Image.Image) -> list[int]:
    values = [(value + 8) // 17 for value in image.getdata()]
    if len(values) % 2:
        values.append(0)
    return [(values[index] << 4) | values[index + 1]
            for index in range(0, len(values), 2)]


def cpp_bytes(name: str, values: list[int], columns: int = 16) -> str:
    lines = [f"const uint8_t {name}[] = {{"]
    for start in range(0, len(values), columns):
        chunk = values[start:start + columns]
        lines.append("    " + ", ".join(f"0x{value:02x}" for value in chunk) + ",")
    lines.append("};")
    return "\n".join(lines)


def line(draw: ImageDraw.ImageDraw, points: list[tuple[float, float]], width: float) -> None:
    scaled = [(round(x * SCALE), round(y * SCALE)) for x, y in points]
    draw.line(scaled, fill=255, width=round(width * SCALE), joint="curve")


def icon_canvas() -> tuple[Image.Image, ImageDraw.ImageDraw]:
    image = Image.new("L", (ICON_SIZE * SCALE, ICON_SIZE * SCALE), 0)
    return image, ImageDraw.Draw(image)


def generate_icons() -> dict[str, Image.Image]:
    icons: dict[str, Image.Image] = {}

    image, draw = icon_canvas()
    draw.polygon([(16 * SCALE, 10 * SCALE), (39 * SCALE, 24 * SCALE),
                  (16 * SCALE, 38 * SCALE)], fill=255)
    icons["play"] = downsample(image, (ICON_SIZE, ICON_SIZE))

    image, draw = icon_canvas()
    draw.rounded_rectangle((12 * SCALE, 9 * SCALE, 20 * SCALE, 39 * SCALE),
                           radius=2 * SCALE, fill=255)
    draw.rounded_rectangle((28 * SCALE, 9 * SCALE, 36 * SCALE, 39 * SCALE),
                           radius=2 * SCALE, fill=255)
    icons["pause"] = downsample(image, (ICON_SIZE, ICON_SIZE))

    image, draw = icon_canvas()
    draw.rounded_rectangle((12 * SCALE, 12 * SCALE, 36 * SCALE, 36 * SCALE),
                           radius=3 * SCALE, fill=255)
    icons["stop"] = downsample(image, (ICON_SIZE, ICON_SIZE))

    image, draw = icon_canvas()
    line(draw, [(8, 23), (24, 9), (40, 23)], 4)
    line(draw, [(13, 21), (13, 39), (35, 39), (35, 21)], 4)
    draw.rectangle((21 * SCALE, 29 * SCALE, 27 * SCALE, 39 * SCALE), fill=255)
    icons["home"] = downsample(image, (ICON_SIZE, ICON_SIZE))

    image, draw = icon_canvas()
    center = 24 * SCALE
    outer = 17 * SCALE
    inner = 12 * SCALE
    points = []
    for index in range(24):
        angle = -math.pi / 2 + index * math.pi / 12
        radius = outer if index % 3 == 0 else inner
        points.append((center + math.cos(angle) * radius,
                       center + math.sin(angle) * radius))
    draw.polygon(points, fill=255)
    draw.ellipse((16 * SCALE, 16 * SCALE, 32 * SCALE, 32 * SCALE), fill=0)
    icons["settings"] = downsample(image, (ICON_SIZE, ICON_SIZE))

    image, draw = icon_canvas()
    line(draw, [(31, 10), (17, 24), (31, 38)], 5)
    icons["back"] = downsample(image, (ICON_SIZE, ICON_SIZE))

    image, draw = icon_canvas()
    line(draw, [(17, 10), (31, 24), (17, 38)], 5)
    icons["next"] = downsample(image, (ICON_SIZE, ICON_SIZE))

    image, draw = icon_canvas()
    line(draw, [(9, 25), (19, 35), (39, 13)], 5)
    icons["confirm"] = downsample(image, (ICON_SIZE, ICON_SIZE))

    image, draw = icon_canvas()
    draw.polygon([(24 * SCALE, 6 * SCALE), (43 * SCALE, 40 * SCALE),
                  (5 * SCALE, 40 * SCALE)], fill=255)
    draw.rounded_rectangle((22 * SCALE, 16 * SCALE, 26 * SCALE, 29 * SCALE),
                           radius=SCALE, fill=0)
    draw.ellipse((22 * SCALE, 33 * SCALE, 26 * SCALE, 37 * SCALE), fill=0)
    icons["warning"] = downsample(image, (ICON_SIZE, ICON_SIZE))
    return icons


def generate_font(path: Path, pixel_size: int) -> tuple[list[int], list[tuple[int, int, int]], int]:
    font = ImageFont.truetype(str(path), pixel_size * SCALE)
    ascent, descent = font.getmetrics()
    line_height = math.ceil((ascent + descent) / SCALE)
    alpha_values: list[int] = []
    glyphs: list[tuple[int, int, int]] = []
    offset = 0
    for code in range(FIRST_CHARACTER, LAST_CHARACTER + 1):
        character = chr(code)
        advance_high = max(SCALE, math.ceil(font.getlength(character)))
        width = max(1, math.ceil(advance_high / SCALE))
        canvas = Image.new("L", (width * SCALE, line_height * SCALE), 0)
        draw = ImageDraw.Draw(canvas)
        draw.text((0, ascent), character, font=font, fill=255, anchor="ls")
        glyph = downsample(canvas, (width, line_height))
        quantized = [(value + 8) // 17 for value in glyph.getdata()]
        glyphs.append((offset, width, line_height))
        alpha_values.extend(quantized)
        offset += width * line_height
    if len(alpha_values) % 2:
        alpha_values.append(0)
    packed = [(alpha_values[index] << 4) | alpha_values[index + 1]
              for index in range(0, len(alpha_values), 2)]
    return packed, glyphs, line_height


def generate_ring() -> tuple[Image.Image, list[int]]:
    high = Image.new("L", (RING_SIZE * SCALE, RING_SIZE * SCALE), 0)
    draw = ImageDraw.Draw(high)
    inset = 3 * SCALE
    draw.ellipse((inset, inset, RING_SIZE * SCALE - inset - 1,
                  RING_SIZE * SCALE - inset - 1), outline=255, width=3 * SCALE)
    ring = downsample(high, (RING_SIZE, RING_SIZE))
    center = (RING_SIZE - 1) / 2
    segments: list[int] = []
    for y in range(RING_SIZE):
        for x in range(RING_SIZE):
            angle = math.atan2(x - center, center - y)
            if angle < 0:
                angle += math.tau
            segments.append(min(RING_SEGMENTS - 1,
                                int(angle / math.tau * RING_SEGMENTS)))
    return ring, segments


def generated_cpp(icons: dict[str, Image.Image]) -> str:
    fonts = {
        "small": generate_font(FONT_REGULAR, 10),
        "medium": generate_font(FONT_BOLD, 16),
        "large": generate_font(FONT_BOLD, 26),
    }
    ring, ring_segments = generate_ring()
    sections = [
        "// Generated by tools/generate_hmi_assets.py. Do not edit by hand.",
        '#include "hmi/assets.hpp"',
        "",
        "namespace hmi::assets {",
        "namespace {",
    ]
    icon_order = ["play", "pause", "stop", "home", "settings", "back", "next",
                  "confirm", "warning"]
    for name in icon_order:
        sections.extend([cpp_bytes(f"kIcon{name.title()}Alpha", packed_alpha(icons[name])), ""])

    for name, (alpha, glyphs, _) in fonts.items():
        title = name.title()
        sections.extend([cpp_bytes(f"kFont{title}Alpha", alpha), ""])
        sections.append(f"const Glyph kFont{title}Glyphs[] = {{")
        sections.extend(f"    {{{offset}u, {width}, {height}}},"
                        for offset, width, height in glyphs)
        sections.extend(["};", ""])

    sections.extend([
        cpp_bytes("kHoldRingAlpha", packed_alpha(ring)), "",
        cpp_bytes("kHoldRingSegments", ring_segments), "",
        f"const Mask kIcons[] = {{",
        "    {nullptr, 0, 0},",
    ])
    for name in icon_order:
        sections.append(f"    {{kIcon{name.title()}Alpha, {ICON_SIZE}, {ICON_SIZE}}},")
    sections.extend([
        "};", "",
        f"const Font kFonts[] = {{",
        f"    {{kFontSmallAlpha, kFontSmallGlyphs, {FIRST_CHARACTER}, {LAST_CHARACTER}, {fonts['small'][2]}}},",
        f"    {{kFontMediumAlpha, kFontMediumGlyphs, {FIRST_CHARACTER}, {LAST_CHARACTER}, {fonts['medium'][2]}}},",
        f"    {{kFontLargeAlpha, kFontLargeGlyphs, {FIRST_CHARACTER}, {LAST_CHARACTER}, {fonts['large'][2]}}},",
        "};", "",
        f"const HoldRing kHoldRing = {{{{kHoldRingAlpha, {RING_SIZE}, {RING_SIZE}}},",
        f"                                kHoldRingSegments, {RING_SEGMENTS}}};",
        "", "}  // namespace", "",
        "const Font& font(FontId id) { return kFonts[static_cast<uint8_t>(id)]; }",
        "const Mask& icon(IconId id) { return kIcons[static_cast<uint8_t>(id)]; }",
        "const HoldRing& hold_ring() { return kHoldRing; }",
        "", "}  // namespace hmi::assets", "",
    ])
    return "\n".join(sections)


def composite_mask(destination: Image.Image, mask: Image.Image, xy: tuple[int, int], color: str) -> None:
    layer = Image.new("RGB", mask.size, color)
    destination.paste(layer, xy, mask)


def centered_text(panel: Image.Image, y: int, text: str, font: ImageFont.FreeTypeFont,
                  color: str) -> None:
    draw = ImageDraw.Draw(panel)
    box = draw.textbbox((0, 0), text, font=font)
    width = box[2] - box[0]
    draw.text(((128 - width) / 2, y), text, font=font, fill=color)


def generate_preview(icons: dict[str, Image.Image]) -> Image.Image:
    colors = {"bg": "#080a0e", "white": "#f4f3ee", "muted": "#696e78",
              "red": "#f04444", "amber": "#ffaa14", "cyan": "#1ec8ff"}
    small = ImageFont.truetype(str(FONT_REGULAR), 10)
    medium = ImageFont.truetype(str(FONT_BOLD), 16)
    large = ImageFont.truetype(str(FONT_BOLD), 26)
    ring, _ = generate_ring()
    panels = [Image.new("RGB", (128, 128), colors["bg"]) for _ in range(3)]

    centered_text(panels[0], 7, "TRANSPORT", small, colors["muted"])
    composite_mask(panels[0], ring, (35, 22), colors["red"])
    composite_mask(panels[0], icons["play"], (40, 27), colors["white"])
    centered_text(panels[0], 83, "PLAY", medium, colors["white"])
    centered_text(panels[0], 108, "HOLD STOP", small, colors["red"])

    centered_text(panels[1], 7, "SPEED", small, colors["muted"])
    centered_text(panels[1], 38, "33 RPM", large, colors["amber"])
    centered_text(panels[1], 83, "33.3 ACTUAL", small, colors["white"])
    ImageDraw.Draw(panels[1]).rounded_rectangle((26, 105, 102, 109), radius=2,
                                                fill=colors["amber"])

    centered_text(panels[2], 7, "SYSTEM", small, colors["muted"])
    composite_mask(panels[2], icons["settings"], (40, 25), colors["cyan"])
    centered_text(panels[2], 83, "SETTINGS", medium, colors["white"])
    centered_text(panels[2], 108, "HOME OK", small, colors["cyan"])

    preview = Image.new("RGB", (128 * 3 + 24, 128 + 16), "#202226")
    for index, panel in enumerate(panels):
        preview.paste(panel, (4 + index * 132, 8))
    return preview.resize((preview.width * 3, preview.height * 3), Image.Resampling.NEAREST)


def write_if_changed(path: Path, content: bytes) -> bool:
    if path.exists() and path.read_bytes() == content:
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(content)
    return True


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true",
                        help="fail if generated assets differ from committed files")
    args = parser.parse_args()

    icons = generate_icons()
    cpp = generated_cpp(icons).encode("utf-8")
    preview = generate_preview(icons)
    from io import BytesIO
    buffer = BytesIO()
    preview.save(buffer, format="PNG", optimize=True)

    outputs = [(OUTPUT_CPP, cpp), (PREVIEW_PNG, buffer.getvalue())]
    changed = [path for path, content in outputs
               if not path.exists() or path.read_bytes() != content]
    if args.check:
        if changed:
            for path in changed:
                print(f"out of date: {path.relative_to(ROOT)}")
            return 1
        print("HMI assets are up to date.")
        return 0

    for path, content in outputs:
        if write_if_changed(path, content):
            print(f"wrote {path.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
