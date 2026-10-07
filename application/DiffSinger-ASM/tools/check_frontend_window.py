#!/usr/bin/env python3
"""Real SDL/Vulkan phoneme inspector, IME, history, clipboard and native synthesis."""
from pathlib import Path
import json
import os
import re
import subprocess
from PIL import Image

APP = Path(__file__).resolve().parents[1]
BIN = APP / "build/diffsinger-asm"
OUT = APP / "build/frontend-window-check"
OUT.mkdir(parents=True, exist_ok=True)
BANK = Path("/home/fuurin/.local/share/OpenUtau/Singers/DongFangZhiZi_Nectar_DiffSinger_CE_26.08.31")
score = json.loads((APP / "examples/first-song.tlp").read_text())
part = score["tracks"][0]["parts"][0]
part["pitch"] = []
part["endOffset"] = 1920
part["notes"] = [dict(pos=120, dur=480, pitch=69, lyric="か", pronunciation="", properties={}),
                  dict(pos=600, dur=240, pitch=72, lyric="+~", pronunciation="", properties={}),
                  dict(pos=840, dur=960, pitch=69, lyric="世界", pronunciation="", properties={})]
fixture = OUT / "japanese.dse"
fixture.write_text(json.dumps(score, ensure_ascii=False))
script = OUT / "editing.input"
script.write_text(f"""2.00 note 0
2.10 wait-frontend
3.80 capture {OUT / 'resolved.png'}
4.00 button timing
4.10 text 80 *
4.20 key Return
4.40 key ctrl+z
4.50 key ctrl+y
4.70 key ctrl+c
4.90 down 502 157
5.00 up 502 157
5.20 key ctrl+v
5.40 note 3
5.60 button timing
5.70 text 120 *
5.80 key Return
6.00 key ctrl+z
6.10 key ctrl+y
6.30 resize 1024 800
6.80 move 900 600
6.90 wheel 0 -3
7.10 capture {OUT / 'timing.png'}
7.30 note 2
7.50 button lyric
7.70 preedit せかい
7.90 capture {OUT / 'ime.png'}
8.10 text せかい
8.30 key Return
8.50 note 0
8.70 capture {OUT / 'original.png'}
9.00 button render
9.10 wait-render
22.00 key Space
22.15 capture {OUT / 'playing.png'}
22.40 key Space
""")
env = dict(os.environ, SDL_VIDEODRIVER="x11", SDL_AUDIODRIVER="dummy", DIFFSINGER_EDITOR_TRACE_INPUT="1")
p = subprocess.run([str(BIN), "--project", str(fixture), "--singer", str(BANK), "--language", "ja",
                    "--script", str(script), "--duration", "24", "--state-out", str(OUT / "edited.dse"),
                    "--report-out", str(OUT / "window.json"), "--cache-out", str(OUT / "voice.wav")],
                   text=True, capture_output=True, env=env, timeout=180)
(OUT / "window.log").write_text(p.stdout + p.stderr)
assert p.returncode == 0, p.stdout + p.stderr
saved = json.loads((OUT / "edited.dse").read_text())
notes = saved["tracks"][0]["parts"][0]["notes"]
assert len(notes) == 4 and notes[3]["pos"] == 3360
assert notes[0]["phonemeTiming"]["ticks"] == [77, 0]
assert notes[3]["phonemeTiming"]["ticks"] == [115, 0], "Clipboard timing alias corrupted the original"
assert notes[2]["lyric"] == "せかい"
assert saved["asmui"]["languageCode"] == "ja" and saved["asmui"]["language"] == 2
report = json.loads((OUT / "window.json").read_text())
assert report["rendered"] and report["manualTimings"] == 2 and report["frontendReady"]
assert report["playTick"] > 10
job = Path(re.search(r"DiffSinger job: (.+)", p.stdout)[1])
durations = [int(x) for x in (job / "durations.txt").read_text().split()]
assert durations[1] == 7
assert durations[-2] == round(3475 * 60 * 44100 / (120 * 480 * 512)) - round(3360 * 60 * 44100 / (120 * 480 * 512))
reopen = subprocess.run([str(BIN), "--project", str(OUT / "edited.dse"), "--check-lyrics"],
                        capture_output=True, text=True, timeout=90)
assert reopen.returncode == 0, reopen.stdout + reopen.stderr
reopened = json.loads(reopen.stdout)
assert reopened["languageId"] == 2 and reopened["errors"] == 0
assert [x["frames"] for x in reopened["parts"][0]["phones"]] == durations
for filename in ["resolved.png", "timing.png", "ime.png", "original.png", "playing.png"]:
    with Image.open(OUT / filename) as image:
        assert image.width >= 1024 and image.height >= 800
        assert len(image.getcolors(image.width * image.height)) > 100
report.update(status="passed", job=str(job), durations=durations,
              checks=["background dictionary loading", "actual phoneme lane", "timing field", "undo/redo",
                      "clipboard independence", "Japanese IME", "minimum window/inspector scrolling",
                      "native GUI synthesis", "SDL playback clock", "native save/reopen"])

# A multi-syllable word, independent + owners, a held vowel and a full-word copy.
part["notes"] = [dict(pos=120, dur=480, pitch=69, lyric="世界", pronunciation="", properties={}),
                  dict(pos=600, dur=480, pitch=69, lyric="+", pronunciation="", properties={}),
                  dict(pos=1080, dur=480, pitch=69, lyric="+", pronunciation="", properties={}),
                  dict(pos=1560, dur=240, pitch=69, lyric="+~", pronunciation="", properties={})]
syllable = OUT / "syllable.dse"
syllable.write_text(json.dumps(score, ensure_ascii=False))
script.write_text(f"""2.00 note 0
2.10 wait-frontend
3.80 capture {OUT / 'syllable-resolved.png'}
4.00 button timing
4.10 text 60 *
4.20 key Return
4.40 note 1
4.60 button timing
4.70 text 100 *
4.80 key Return
5.00 key ctrl+z
5.10 key ctrl+y
5.30 note 3
5.50 button timing
5.60 text *
5.70 key Return
5.90 capture {OUT / 'syllable-hold.png'}
6.10 note 3
6.20 button timing-reset
6.30 key ctrl+z
6.40 key ctrl+a
6.50 key ctrl+c
6.60 down 376 157
6.70 up 376 157
6.80 key ctrl+v
6.95 key Escape
7.00 note 5
7.20 button timing
7.30 text 120 *
7.40 key Return
7.60 resize 1024 800
8.00 move 900 600
8.10 wheel 0 -3
8.20 capture {OUT / 'syllable-copy.png'}
8.40 note 1
8.60 capture {OUT / 'syllable-original.png'}
8.80 button render
8.90 wait-render
22.00 key Space
22.15 capture {OUT / 'syllable-playing.png'}
22.40 key Space
""")
p = subprocess.run([str(BIN), "--project", str(syllable), "--singer", str(BANK), "--language", "ja",
                    "--script", str(script), "--duration", "24", "--state-out", str(OUT / "syllable-edited.dse"),
                    "--report-out", str(OUT / "syllable-window.json"), "--cache-out", str(OUT / "syllable-voice.wav")],
                   text=True, capture_output=True, env=env, timeout=180)
(OUT / "syllable-window.log").write_text(p.stdout + p.stderr)
assert p.returncode == 0, p.stdout + p.stderr
saved = json.loads((OUT / "syllable-edited.dse").read_text())
notes = saved["tracks"][0]["parts"][0]["notes"]
assert len(notes) == 8 and notes[4]["pos"] == 1920
assert notes[0]["phonemeTiming"]["ticks"] == [58, 0]
assert notes[1]["phonemeTiming"]["ticks"] == [96, 0]
assert notes[2]["phonemeTiming"]["ticks"] == [0], "Held-vowel editing/reset did not target its owner"
assert notes[5]["phonemeTiming"]["ticks"] == [115, 0], "Copied syllable did not retain independent timing"
syllable_report = json.loads((OUT / "syllable-window.json").read_text())
assert syllable_report["rendered"] and syllable_report["manualTimings"] == 6
job = Path(re.search(r"DiffSinger job: (.+)", p.stdout)[1])
durations = [int(x) for x in (job / "durations.txt").read_text().split()]
assert durations[:6] == [11, 5, 38, 8, 35, 64]
reopen = subprocess.run([str(BIN), "--project", str(OUT / "syllable-edited.dse"), "--check-lyrics"],
                        capture_output=True, text=True, timeout=90)
assert reopen.returncode == 0, reopen.stdout + reopen.stderr
assert [x["frames"] for x in json.loads(reopen.stdout)["parts"][0]["phones"]] == durations
for filename in ["syllable-resolved.png", "syllable-hold.png", "syllable-copy.png", "syllable-original.png", "syllable-playing.png"]:
    with Image.open(OUT / filename) as image:
        assert image.width >= 1024 and image.height >= 800
        assert len(image.getcolors(image.width * image.height)) > 100
syllable_report.update(job=str(job), durations=durations,
                       checks=["partial syllable display", "independent + timing", "held-vowel owner redirect/reset",
                               "whole-word clipboard independence", "undo/redo", "native GUI synthesis", "save/reopen"])
report["syllable"] = syllable_report
(OUT / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2))
print("Frontend SDL/Vulkan: real phonemes, edited timing, IME, clipboard, native synthesis and save/reopen OK")
