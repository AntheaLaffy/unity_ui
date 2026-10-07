#!/usr/bin/env python3
"""SDL window acceptance with real Vulkan frames and committed document state."""
from pathlib import Path
import json
import os
import subprocess
import time
from PIL import Image

APP = Path(__file__).resolve().parents[1]
BIN = APP / "build/diffsinger-asm"
OUT = APP / "build/window-check"
OUT.mkdir(parents=True, exist_ok=True)
env = dict(os.environ, SDL_VIDEODRIVER="x11", SDL_AUDIODRIVER="dummy", DIFFSINGER_EDITOR_TRACE_INPUT="1")
project = OUT / "editing.tlp"
project.write_text((APP / "examples/first-song.tlp").read_text())
script = OUT / "editing.input"
script.write_text(f"""0.10 note 0
0.20 key Up
0.30 key ctrl+z
0.40 key ctrl+y
0.50 down-note 0
0.65 move-delta 960 1
0.80 up 0 0
0.90 button lyric
1.00 preedit 虚拟歌姬
1.05 capture {OUT / 'ime.png'}
1.15 text 虚拟歌姬
1.25 key Return
1.35 key ctrl+s
2.00 button pitch
2.10 down-note 0
2.20 move-delta 120 0.25
2.30 up 0 0
2.40 key ctrl+z
2.50 key D
2.60 point 120 60
2.70 key Delete
2.80 key ctrl+z
2.90 key ctrl+y
3.00 key ctrl+s
3.10 button track
3.20 key Home
3.30 key ctrl+z
3.40 key ctrl+s
3.55 resize 1280 840
3.80 note 0
4.00 capture {OUT / 'resized.png'}
""")
p = subprocess.run([str(BIN), "--project", str(project), "--script", str(script),
                    "--duration", "4.5", "--output", str(OUT / "editing.png"),
                    "--state-out", str(OUT / "edited.tlp"), "--report-out", str(OUT / "editing.json")],
                   text=True, capture_output=True, env=env, timeout=60)
(OUT / "editing.log").write_text(p.stdout + p.stderr)
assert p.returncode == 0, p.stdout + p.stderr
score = json.loads((OUT / "edited.tlp").read_text())
saved = json.loads(project.read_text())
assert saved["tracks"] == score["tracks"] and saved["asmui"] == score["asmui"], "Ctrl+S lost the project path or document state"
editing_report = json.loads((OUT / "editing.json").read_text())
assert not editing_report["modified"] and editing_report["path"] == str(project), editing_report
notes = score["tracks"][0]["parts"][0]["notes"]
assert len(notes) == 12, notes
assert notes[0]["pitch"] == 62 and notes[0]["pos"] == 960, notes[0]
assert notes[0]["lyric"] == "虚拟歌姬", notes[0]
assert len(score["tracks"]) == 1
assert score["asmui"]["speaker"] == "Normal" and score["asmui"]["singer"]
with Image.open(OUT / "resized.png") as image:
    assert image.size[0] >= 1024 and image.size[1] >= 800
    with Image.open(OUT / "ime.png") as first:
        assert image.size != first.size, "Resize did not change the framebuffer"
    assert len(image.getcolors(1280 * 840)) > 100

# Reopen the saved project through the same document reader and compare notes.
reopen = subprocess.run([str(BIN), "--project", str(OUT / "edited.tlp"), "--headless",
                         "--output", str(OUT / "reopened.png"), "--state-out", str(OUT / "reopened.tlp")],
                        text=True, capture_output=True, env=env, timeout=60)
assert reopen.returncode == 0, reopen.stdout + reopen.stderr
reopened = json.loads((OUT / "reopened.tlp").read_text())
assert reopened["tracks"] == score["tracks"] and reopened["asmui"] == score["asmui"]

# Use a short real voice render so cancellation, playback and cache export can
# be exercised without depending on project duration or a physical audio device.
voice = json.loads((APP / "examples/first-song.tlp").read_text())
part = voice["tracks"][0]["parts"][0]
part["notes"] = [{"pos": 0, "dur": 960, "pitch": 69, "lyric": "啊", "pronunciation": "zh/a"}]
part["pitch"] = []
(OUT / "voice.tlp").write_text(json.dumps(voice, ensure_ascii=False))
script.write_text(f"""0.1 render
0.2 render
0.3 wait-idle
1.5 capture {OUT / 'cancelled.png'}
1.6 render
1.7 wait-render
8.0 key Space
8.35 key Space
8.5 capture {OUT / 'playing.png'}
""")
p = subprocess.run([str(BIN), "--project", str(OUT / "voice.tlp"), "--script", str(script),
                    "--duration", "9", "--output", str(OUT / "voice.png"),
                    "--cache-out", str(OUT / "gui-voice.wav"), "--report-out", str(OUT / "voice.json")],
                   text=True, capture_output=True, env=env, timeout=90)
(OUT / "voice.log").write_text(p.stdout + p.stderr)
assert p.returncode == 0, p.stdout + p.stderr
report = json.loads((OUT / "voice.json").read_text())
assert report["rendered"] and report["samples"] > 0 and report["playTick"] > 10, report
(OUT / "report.json").write_text(json.dumps({"status": "passed", "editing": ["move", "transpose", "IME", "lyrics", "undo", "redo", "delete", "save", "reopen", "track undo", "resize"],
    "rendering": ["cancel", "native synthesis", "SDL queued-audio clock", "cache export"], "voice": report}, ensure_ascii=False, indent=2))
print("SDL/Vulkan window: editing, IME, persistence, resize, cancel, native render and audio clock OK")
