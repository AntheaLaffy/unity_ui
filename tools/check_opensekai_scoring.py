#!/usr/bin/env python3
"""Capture changing score popups on two real official charts in native windows."""
import argparse
import json
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
from check_opensekai_gameplay_reference import PROJECT, ROOT, capture_case
from compare_opensekai_reference import panel


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefix", default="opensekai-dynamic-score")
    args = parser.parse_args()
    if Path(args.prefix).name != args.prefix:
        parser.error("prefix must be a plain name")
    output = ROOT / "build" / args.prefix
    output.mkdir(exist_ok=True)
    if list(output.glob("*/frame-*.png")):
        parser.error("Use a fresh prefix to preserve the previous captures")
    reports = []
    for chart, frames, times in [
        ("0001_01-master", 6345600, [9.085, 11.885, 13.685]),
        ("0114_01-master", 5382910, [1.848, 3.373, 5.623]),
    ]:
        manifest = json.loads((PROJECT / "content/library" / chart / "manifest.json").read_text())
        states = capture_case(output, chart, manifest, 6., [(t, "capture") for t in times], True,
                              chart=chart, audio_frames=frames)
        reports.append({"chart": chart, "title": manifest["title"], "states": states})
    canvas = Image.new("RGB", (2560, 3*300), (20, 20, 24))
    draw = ImageDraw.Draw(canvas)
    font = ImageFont.truetype(str(PROJECT / "Assets/Resources/font/FOT-RodinNTLGPro-DB.otf"), 20)
    for column, report in enumerate(reports):
        for i, state in enumerate(report["states"]):
            x, y = column*1280, i*300
            draw.text((x+16, y+10), f"{report['title']} / music {state['actual_music_seconds']:.3f}s", fill="white", font=font)
            canvas.paste(panel(state["native"], True).crop((0,0,1280,260)), (x,y+40))
    image = output / "score-popups.png"
    canvas.save(image)
    report = {"image": str(image), "charts": reports,
              "scope": "Real hardware Vulkan windows and Auto HUD popups. C ABI verifies the digit values separately; this image supports visual inspection, not automated pixel equality."}
    (output / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2)+"\n")
    print(json.dumps(report, ensure_ascii=False), flush=True)


if __name__ == "__main__":
    main()
