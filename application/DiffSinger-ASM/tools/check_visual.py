#!/usr/bin/env python3
"""Native Vulkan stress captures: dense notes and long-lived UTF-8 input pages."""
from pathlib import Path
import json
import subprocess
from PIL import Image

APP = Path(__file__).resolve().parents[1]
BIN = APP / "build/diffsinger-asm"
OUT = APP / "build/visual-check"
OUT.mkdir(parents=True, exist_ok=True)
project = json.loads((APP / "examples/first-song.tlp").read_text())
part = project["tracks"][0]["parts"][0]
part["notes"] = [{"pos": 0, "dur": 1920, "pitch": 60,
                  "lyric": "a" * 63, "pronunciation": "zh/a"} for _ in range(2048)]
part["pitch"] = []
(OUT / "dense.tlp").write_text(json.dumps(project))
dense = subprocess.run([str(BIN), "--project", str(OUT / "dense.tlp"), "--headless",
                        "--output", str(OUT / "dense.png")], text=True, capture_output=True, timeout=90)
assert dense.returncode == 0, dense.stdout + dense.stderr
with Image.open(OUT / "dense.png") as image:
    assert image.size == (1440, 940) and len(image.getcolors(image.width * image.height)) > 100

used = set("".join((APP / "src" / name).read_text() for name in ["main.c", "ui.c", "app.c", "engine.c"]))
glyphs = [chr(code) for code in range(0x4E00, 0x5000) if chr(code) not in used][:72]
lines = ["0.05 note 0"]
stamp = .1
for glyph in glyphs:
    for command in ["button lyric", "text " + glyph, "key Return"]:
        lines.append(f"{stamp:.2f} {command}")
        stamp += .04
(OUT / "font-pages.input").write_text("\n".join(lines) + "\n")
pages = subprocess.run([str(BIN), "--headless", "--script", str(OUT / "font-pages.input"),
                        "--duration", str(stamp + .2), "--output", str(OUT / "font-pages.png"),
                        "--state-out", str(OUT / "font-pages.tlp")],
                       text=True, capture_output=True, timeout=180)
(OUT / "font-pages.log").write_text(pages.stdout + pages.stderr)
assert pages.returncode == 0, pages.stdout + pages.stderr
assert "recycled 64 input pages" in pages.stderr, "Font-bank rollover was not reached"
assert "missing input glyphs" not in pages.stderr, pages.stderr
state = json.loads((OUT / "font-pages.tlp").read_text())
assert state["tracks"][0]["parts"][0]["notes"][0]["lyric"] == glyphs[-1]
with Image.open(OUT / "font-pages.png") as image:
    assert image.size == (1440, 940)
(OUT / "report.json").write_text(json.dumps({"status": "passed", "denseNotes": 2048,
    "uniqueInputGlyphs": len(glyphs), "fontPagesRecycled": True}, indent=2))
print("Vulkan stress: 2048 dense notes and recycled UTF-8 font pages OK")
