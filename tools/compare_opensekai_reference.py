#!/usr/bin/env python3
"""Build visual comparisons from reference states and actual native captures."""
import argparse
import json
from pathlib import Path
from PIL import Image, ImageDraw, ImageOps

ROOT=Path(__file__).resolve().parents[1]
PROJECT=ROOT/"application/OpenSekai-ASMR"
REFERENCE=PROJECT/"resources/references/0114_01"


def panel(path, native=False):
    with Image.open(path) as image:
        if native:
            scale=min(image.width/1920,image.height/1080)
            w,h=round(1920*scale),round(1080*scale)
            x,y=round((image.width-w)/2),round((image.height-h)/2)
            image=image.crop((x,y,x+w,y+h))
        return ImageOps.pad(image.convert("RGB"),(1280,720),color=(0,0,0))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--captures",type=Path,default=ROOT/"build/opensekai-reference-endings")
    args=parser.parse_args()
    reports=json.loads((args.captures/"report.json").read_text())
    fc=next(r for r in reports if r["kind"]=="full_combo")
    # Equivalent phases are selected by visible state. The clocks, charts and
    # gameplay numbers differ; full-image error is not a fidelity score here.
    pairs=[("fc-ring",fc["animation_frames"][0]),
           ("fc-flash",fc["animation_frames"][1]),
           ("fc-particles",fc["animation_frames"][2]),
           ("result",fc["result_image"])]
    canvas=Image.new("RGB",(2560,760*len(pairs)),(20,20,24))
    draw=ImageDraw.Draw(canvas)
    output=[]
    for i,(state,native) in enumerate(pairs):
        reference=REFERENCE/f"frames/{state}.png"
        y=i*760
        draw.text((16,y+10),f"Reference: {state}",fill="white")
        draw.text((1296,y+10),"Rust / ASM native content (canvas crop)",fill="white")
        canvas.paste(panel(reference),(0,y+40))
        canvas.paste(panel(native,True),(1280,y+40))
        output.append({"state":state,"reference":str(reference),"native":native})
    path=args.captures/"comparison.png"
    canvas.save(path)
    report={"comparison":str(path),"pairs":output,
            "criteria":["composition","glyph bounds and color","glow and ring","particle density and extent","background and cover layering"],
            "open_visual_gaps":["Gameplay hit beam shape and particle distribution calibration", "HUD typography/gradients and layout", "Rounded sprite masks and perspective texture interpolation", "Particle damping and random distribution calibration against recording"],
            "scope":"Visible-state comparisons; native charts, timing and gameplay logic are allowed to differ. This is a review artifact, not an equivalence test."}
    (args.captures/"comparison.json").write_text(json.dumps(report,ensure_ascii=False,indent=2)+"\n")
    print(json.dumps(report,ensure_ascii=False,indent=2))


if __name__=="__main__":
    main()
