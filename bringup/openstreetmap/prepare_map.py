#!/usr/bin/env python3
"""Convert a manually exported OpenStreetMap PNG to STM32 RGB888 display data.

Usage (from bringup/openstreetmap):
    python -m pip install pillow
    python tools/prepare_map.py map.png

Produces:
    osm_map_preview.png             (PC-side visual check)
    src/osm_map_rgb888.h            (embedded image bytes for Zephyr)

This does NOT download OSM tiles and does NOT create a live map client.
"""

import argparse
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont, ImageOps

WIDTH = 480
HEIGHT = 272
ROOT = Path(__file__).resolve().parents[1]


def font_for_preview(size=12):
    candidates = [
        Path("C:/Windows/Fonts/arial.ttf"),
        Path("C:/Windows/Fonts/segoeui.ttf"),
        Path("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"),
    ]
    for candidate in candidates:
        if candidate.is_file():
            return ImageFont.truetype(str(candidate), size=size)
    return ImageFont.load_default()


def main():
    parser = argparse.ArgumentParser(description="Prepare static OSM image for STM32 LCD")
    parser.add_argument("image", type=Path, help="PNG/JPEG exported with OSM's Share > Image")
    args = parser.parse_args()

    if not args.image.is_file():
        parser.error(f"Map image not found: {args.image}")

    with Image.open(args.image) as source:
        image = ImageOps.fit(
            source.convert("RGB"),
            (WIDTH, HEIGHT),
            method=Image.Resampling.LANCZOS,
        )

    draw = ImageDraw.Draw(image)
    font = font_for_preview()
    draw.rectangle((0, 0, 186, 22), fill=(17, 36, 61))
    draw.text((8, 4), "OSM: OFFLINE DISPLAY TEST", fill="white", font=font)
    draw.rectangle((0, HEIGHT - 22, WIDTH - 1, HEIGHT - 1), fill=(247, 249, 251))
    draw.text((8, HEIGHT - 19), "(c) OpenStreetMap contributors", fill=(20, 35, 54), font=font)

    preview_file = ROOT / "osm_map_preview.png"
    image.save(preview_file)

    # In Zephyr 4.5, PIXEL_FORMAT_RGB_888's framebuffer byte order is B,G,R.
    # Use this order for the STM32 LTDC driver (not PIL's R,G,B order).
    pixels = image.tobytes("raw", "BGR")
    assert len(pixels) == WIDTH * HEIGHT * 3
    output = ROOT / "src" / "osm_map_rgb888.h"
    output.parent.mkdir(parents=True, exist_ok=True)

    with output.open("w", encoding="utf-8", newline="\n") as dst:
        dst.write("/* Generated static map image - do not edit manually. */\n")
        dst.write("#ifndef RIDECAST_OSM_MAP_RGB888_H_\n")
        dst.write("#define RIDECAST_OSM_MAP_RGB888_H_\n")
        dst.write("#include <stdint.h>\n")
        dst.write(f"#define OSM_MAP_WIDTH {WIDTH}\n")
        dst.write(f"#define OSM_MAP_HEIGHT {HEIGHT}\n")
        dst.write(f"static const uint8_t osm_map_rgb888[{len(pixels)}] = {{\n")
        for pos in range(0, len(pixels), 24):
            dst.write("    " + ", ".join(f"0x{value:02x}" for value in pixels[pos:pos+24]) + ",\n")
        dst.write("};\n#endif /* RIDECAST_OSM_MAP_RGB888_H_ */\n")

    print(f"Preview: {preview_file}")
    print(f"Header:  {output}")
    print(f"Pixels:  {WIDTH}x{HEIGHT}, RGB888 (BGR bytes), {len(pixels):,} bytes")
    print("Use one pristine West build to include the updated header.")


if __name__ == "__main__":
    main()
