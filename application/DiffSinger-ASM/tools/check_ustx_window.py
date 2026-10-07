#!/usr/bin/env python3
"""Native window check on a real imported song, without writing the source."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
from PIL import Image

APP = Path(__file__).resolve().parents[1]
OUT = APP / "build/ustx-window-check"
OUT.mkdir(parents=True, exist_ok=True)
source = Path("/home/fuurin/Music/萤琢/fly.ustx")
original = source.read_bytes()
baseline_path = OUT / "baseline.dse"
baseline_run = subprocess.run([str(APP / "build/diffsinger-asm"), "--project", str(source),
                               "--write-demo", str(baseline_path)], text=True, capture_output=True, timeout=60)
assert baseline_run.returncode == 0, baseline_run.stdout + baseline_run.stderr
script = OUT / "edit.input"
script.write_text(f"""0.1 capture {OUT / 'opened.png'}
0.2 note 0
0.3 key Up
0.4 key ctrl+z
0.5 key ctrl+y
0.6 button lyric
0.7 preedit 编辑测试
0.8 text 编辑测试
0.9 key Return
1.1 resize 1280 840
1.3 capture {OUT / 'edited.png'}
""")
env = dict(os.environ, SDL_VIDEODRIVER="x11", SDL_AUDIODRIVER="dummy")
run = subprocess.run([str(APP / "build/diffsinger-asm"), "--project", str(source), "--script", str(script),
                      "--duration", "2", "--state-out", str(OUT / "edited.dse"),
                      "--report-out", str(OUT / "window.json")], env=env, text=True, capture_output=True, timeout=90)
(OUT / "window.log").write_text(run.stdout + run.stderr)
assert run.returncode == 0, run.stdout + run.stderr
saved = json.loads((OUT / "edited.dse").read_text())
notes = saved["tracks"][0]["parts"][0]["notes"]
assert len(notes) == 496 and notes[0]["pitch"] == 65 and notes[0]["lyric"] == "编辑测试"
baseline = json.loads(baseline_path.read_text())
original_pitch = dict(zip(*[iter(baseline["tracks"][0]["parts"][0]["pitch"][0])] * 2))
changed_pitch = dict(zip(*[iter(saved["tracks"][0]["parts"][0]["pitch"][0])] * 2))
for tick in range(15785, 16495, 5):
    assert abs(changed_pitch[tick] - original_pitch[tick] - 1) < .00002, "Imported pitch did not follow the note transpose"
assert saved["openutau"]["ustx"].encode("utf-8") == original
assert source.read_bytes() == original
report = json.loads((OUT / "window.json").read_text())
assert report["sourceFormat"] == "USTX" and report["notes"] == 836 and report["waveParts"] == 3
for filename in ("opened.png", "edited.png"):
    with Image.open(OUT / filename) as screenshot:
        assert screenshot.width >= 1024 and screenshot.height >= 800
        assert len(screenshot.getcolors(screenshot.width * screenshot.height)) > 100
(OUT / "report.json").write_text(json.dumps({"status": "passed", "sourceSha256": hashlib.sha256(original).hexdigest(),
    "sourceUnchanged": True, "notes": 836, "checks": ["native USTX window", "pitch display", "move", "undo", "redo", "IME", "resize", "native save retaining source"]}, indent=2))
print("USTX SDL/Vulkan: real song, note edits, undo/redo, IME, resize and source preservation OK")
