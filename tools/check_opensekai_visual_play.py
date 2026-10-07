#!/usr/bin/env python3
"""Exercise selection speed controls and capture real 0114 gameplay states."""
import json
import os
from pathlib import Path
import re
import shutil
import tempfile

from PIL import Image, ImageDraw
from check_opensekai_song import isolated_run

ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / "application/OpenSekai-ASMR"


def main():
    source = PROJECT / "content/library/0114_01-master"
    manifest = json.loads((source / "manifest.json").read_text())
    output = ROOT / "build/opensekai-visual-play"
    output.mkdir(exist_ok=True)
    if list(output.glob("frame-*.png")):
        raise ValueError("Move the previous visual capture aside before another run")
    with tempfile.TemporaryDirectory(prefix="ojsk-visual-play-", dir=ROOT / "build") as temporary:
        data = Path(temporary)
        entry = data / source.name
        entry.mkdir()
        for name in ["manifest.json", manifest["audioFileName"], manifest["scoreFileName"], manifest["jacketFileName"]]:
            shutil.copyfile(source / name, entry / name)
        events = [(t, f"click 1750 360") for t in (.3, .4, .5, .6, .7)]
        events += [(1., "1073741893 1"), (1.02, "1073741893 0"), (1.5, "click 1210 418")]
        for t in (6.3, 9., 12.):
            events += [(t, "1073741893 1"), (t+.02, "1073741893 0")]
        script = data / "input.txt"
        script.write_text("".join(f"{t} {command}\n" for t, command in sorted(events)))
        command = [str(ROOT / "build/editor"), "--project", str(PROJECT),
                   "--data-dir", str(data), "--app-chart", manifest["id"],
                   "--app-screen", "manager", "--app-only", "--duration", "14",
                   "--size", "1280", "720", "--app-input-script", str(script),
                   "--app-captures", str(output), "--output", str(output / "live.png")]
        env = os.environ.copy()
        env.update(SDL_AUDIO_DRIVER="dummy", ASMUI_PROFILE_AUDIO="1")
        run = isolated_run(command, env, 90)
        log = run.stdout + run.stderr
        (output / "run.log").write_text(log)
        assert run.returncode == 0, log
        assert len(re.findall("Application pointer:", log)) == 6
        assert "Application audio: 5382910 stereo frames" in log
        assert "Application sound bank: 18 clips" in log
        assert not re.search(r"Validation Error|VUID-|sound error:|missing=[1-9]|dropped=[1-9]", log)
        settings = json.loads((data / "native-play-settings.json").read_text())
        assert abs(settings["noteSpeed"] - 6.5) < 1e-6
        frames = sorted(output.glob("frame-*.png"))
        assert len(frames) == 4, frames
        sheet = Image.new("RGB", (2560, 2 * 760), (24, 24, 28))
        labels = ["Selection: SDL clicks selected speed 6.5", "First gameplay capture",
                  "Gameplay: notes on lane plane and hit feedback", "Gameplay: moving particles and held notes"]
        draw = ImageDraw.Draw(sheet)
        for i, (path, label) in enumerate(zip(frames, labels)):
            x, y = (i % 2) * 1280, (i // 2) * 760
            draw.text((x + 12, y + 10), label, fill="white")
            with Image.open(path) as image:
                sheet.paste(image.convert("RGB").resize((1280, 720)), (x, y + 40))
        sheet.save(output / "overview.png")
        report = {"chart": manifest["id"], "selected_speed": settings["noteSpeed"],
                  "speed_control": "Five real SDL mouse clicks, saved to native-play-settings.json",
                  "frames": [str(path) for path in frames], "overview": str(output / "overview.png"),
                  "scope": "Actual native SDL/Vulkan gameplay; visual inspection of plane projection and native hit animations. Formal reference video selection preview is excluded from audio calibration."}
        (output / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
        print(json.dumps(report, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
