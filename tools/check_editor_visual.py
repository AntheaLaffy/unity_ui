#!/usr/bin/env python3
"""Render real Vulkan frames; catch flipped layouts, missing text and state leaks.

Color/region invariants tolerate glyph antialiasing differences between drivers.
These checks complement, rather than replace, inspection of the saved PNGs.
"""
import argparse
from pathlib import Path
import subprocess

from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[1]


def changed(a, b, box):
    return ImageChops.difference(a.crop(box), b.crop(box)).getbbox() is not None


def orange(image, box=None):
    region = image.crop(box) if box else image
    return sum(count for count, (r, g, b) in region.getcolors(region.width * region.height)
               if r > 225 and 105 < g < 205 and b < 70)


def verify(directory):
    names = ["zh", "en", "compact", "compact-en", "menu", "camera", "console", "2d",
             "game", "rotated", "ime", "search", "large"]
    images = {}
    for name in names:
        path = directory / f"editor-{name}.png"
        with Image.open(path) as source:
            source.load()
            assert source.mode == "RGBA", f"RGBA readback: {name}"
            assert source.getchannel("A").getextrema() == (255, 255), f"Opaque UI: {name}"
            images[name] = source.convert("RGB")
        im = images[name]
        expected = (1024, 680) if name.startswith("compact") else (
            (1920, 1080) if name == "large" else (1440, 900))
        assert im.size == expected, f"Extent: {name}"
        w, h = im.size
        assert im.getpixel((w // 2, 35)) == (48, 48, 48), f"Top menu orientation: {name}"
        assert im.getpixel((150, 340)) == (40, 40, 40), f"Hierarchy boundary: {name}"
        assert im.getpixel((w // 2, h - 3)) == (37, 37, 37), f"Bottom status: {name}"
        if name not in ("search", "camera"):
            assert im.getpixel((12, 244)) == (44, 95, 148), f"Selected hierarchy row: {name}"
        title = im.crop((34, 3, 330, 24))
        assert sum(n for n, (r, g, b) in title.getcolors(title.width * title.height)
                   if r > 100 and g > 100 and b > 100) > 150, (
            f"Actual text coverage: {name}")
        if name not in ("game", "camera"):
            scene = (237, 132, w - 327, h - 269)
            assert orange(im, scene) > 30, f"Projected selection outline: {name}"
            assert orange(im) == orange(im, scene), f"Scene clipping: {name}"
        else:
            assert orange(im) == 0, f"Selection state: {name}"
    zh, en = images["zh"], images["en"]
    assert changed(zh, en, (0, 28, 400, 49)), "Localized menus differ"
    assert changed(zh, en, (1115, 176, 1438, 277)), "Localized Inspector differs"
    assert not changed(zh, en, (310, 250, 990, 590)), "Language preserves scene geometry"
    assert changed(zh, images["menu"], (0, 49, 228, 223)), "Popup drawn on top"
    assert not changed(zh, images["menu"], (400, 82, 1438, 879)), "Popup contained"
    assert changed(zh, images["camera"], (1115, 110, 1438, 600)), "Inspector follows selection"
    assert changed(zh, images["console"], (0, 655, 1113, 878)), "Console tab contents"
    for state in ("2d", "game", "rotated"):
        assert changed(zh, images[state], (310, 250, 990, 590)), f"Scene state: {state}"
    # Y=0 projects to the cube's bottom, not above its center. This detects a
    # texture-UV/local-coordinate flip in the orthographic grid shader.
    r, g, b = images["2d"].getpixel((500, 406))
    assert r > g + 20 and r > b + 20, "2D world axis matches ASM projection"
    for state in ("ime", "search"):
        assert changed(zh, images[state], (48, 109, 228, 128)), f"Chinese input: {state}"
        field = images[state].crop((53, 111, 140, 125))
        assert sum(n for n, (r, g, b) in field.getcolors(field.width * field.height)
                   if r > 110 and g > 110 and b > 110) > 50, (
            f"Input glyph raster coverage: {state}")
    print(f"Editor visual checks passed: {len(images)} Vulkan PNGs, 2 languages, 3 sizes; "
          "panel colors/orientation, text, outline/clipping, popup and state differences.")
    print(f"Inspect PNGs: {directory}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "build/editor-previews")
    parser.add_argument("--skip-render", action="store_true")
    args = parser.parse_args()
    directory = args.output.resolve()
    directory.mkdir(parents=True, exist_ok=True)
    if not args.skip_render:
        subprocess.run([str(ROOT / "build/editor"), "--gallery", str(directory)],
                       cwd=ROOT, check=True)
    verify(directory)


if __name__ == "__main__":
    main()
