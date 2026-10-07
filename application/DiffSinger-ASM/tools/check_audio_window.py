#!/usr/bin/env python3
"""Actual OpenUtau backing tracks, SDL playback and editable clips on Vulkan."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
from PIL import Image

APP = Path(__file__).resolve().parents[1]
BIN = APP / "build/diffsinger-asm"
OUT = APP / "build/audio-window-check"
OUT.mkdir(parents=True, exist_ok=True)
source = Path("/home/fuurin/Music/萤琢/fly.ustx")
original = source.read_bytes()
env = dict(os.environ, SDL_VIDEODRIVER="x11", SDL_AUDIODRIVER="dummy", DIFFSINGER_EDITOR_TRACE_INPUT="1")
script = OUT / "audio.input"
script.write_text(f"""2.00 move 90 200
2.10 wheel 0 -3
2.25 wave 2
2.40 capture {OUT / 'opened.png'}
2.50 key Space
4.00 key Space
4.20 capture {OUT / 'paused.png'}
4.40 button audio-pos
4.50 text 4
4.60 key Return
4.70 key ctrl+z
4.80 key ctrl+y
4.90 button audio-fade-in
5.00 text 0.5
5.10 key Return
5.20 button track-gain
5.30 text -6
5.40 key Return
5.50 button track-pan
5.60 text 25
5.70 key Return
5.80 resize 1024 800
6.20 capture {OUT / 'edited.png'}
6.40 button audio
9.00 key Space
9.20 capture {OUT / 'playing.png'}
""")
p = subprocess.run([str(BIN), "--project", str(source), "--script", str(script), "--duration", "9.6",
                    "--state-out", str(OUT / "edited.dse"), "--report-out", str(OUT / "window.json"),
                    "--cache-out", str(OUT / "backing.wav")], text=True, capture_output=True, env=env, timeout=90)
(OUT / "window.log").write_text(p.stdout + p.stderr)
assert p.returncode == 0, p.stdout + p.stderr
report = json.loads((OUT / "window.json").read_text())
assert report["waveParts"] == 3 and report["audioOnly"] and report["audioPeaksReady"] and report["rendered"]
assert report["activeTrack"] == 4 and report["activeWave"] == 3
assert report["playTick"] > 1000, report
saved = json.loads((OUT / "edited.dse").read_text())
audio = next(part for part in saved["tracks"][4]["parts"] if part["type"] == "audio")
assert audio["pos"] == 1920 and audio["fadeIn"] == 240
assert saved["tracks"][4]["gain"] == -6 and saved["tracks"][4]["pan"] == .25
assert saved["openutau"]["ustx"].encode() == original and source.read_bytes() == original
for name in ["opened.png", "paused.png", "edited.png", "playing.png"]:
    with Image.open(OUT / name) as image:
        assert image.width >= 1024 and image.height >= 800
        assert len(image.getcolors(image.width * image.height)) > 100
report.update(status="passed", sourceSha256=hashlib.sha256(original).hexdigest(), sourceUnchanged=True,
              checks=["real USTX/audio window", "waveforms", "SDL queued-byte playback clock", "clip fields", "undo/redo",
                      "gain/pan", "minimum-size window", "reprepared backing PCM", "native save with original snapshot"])
(OUT / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2))
print("Audio SDL/Vulkan: real song backing, clock, clip edits, mixer, resize, undo/redo and save OK")
