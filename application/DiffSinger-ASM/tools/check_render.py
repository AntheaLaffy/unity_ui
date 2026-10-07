#!/usr/bin/env python3
"""Real native acoustic/vocoder acceptance; no substitute synthesizer."""
from pathlib import Path
import array
import json
import math
import os
import re
import subprocess
import wave
from audio_quality import check_voice

APP = Path(__file__).resolve().parents[1]
BIN = APP / "build/diffsinger-asm"
OUT = APP / "build/render-check"
OUT.mkdir(parents=True, exist_ok=True)


def invoke(*args, success=True):
    p = subprocess.run([str(BIN), *map(str, args)], text=True, capture_output=True, timeout=120)
    (OUT / "last-output.txt").write_text(p.stdout + p.stderr)
    if success:
        assert p.returncode == 0, p.stdout + p.stderr
    else:
        assert p.returncode != 0, "Invalid input was accepted"
    return p


score = json.loads((APP / "examples/first-song.tlp").read_text())
part = score["tracks"][0]["parts"][0]
part["notes"] = [{"pos": 120, "dur": 600, "pitch": 69, "lyric": "啊", "pronunciation": "", "properties": {}}]
part["pitch"] = [[120, 69, 720, 69]]
part["endOffset"] = 1920
fixture = OUT / "voice.tlp"
fixture.write_text(json.dumps(score, ensure_ascii=False))
p = invoke("--project", fixture, "--render", OUT / "voice.wav")
job = Path(re.search(r"任务目录: (.+)", p.stdout)[1])
tokens = [int(v) for v in (job / "tokens.txt").read_text().split()]
durations = [int(v) for v in (job / "durations.txt").read_text().split()]
f0 = array.array("f", (job / "f0.f32").read_bytes())
assert len(tokens) == len(durations) and sum(durations) == len(f0)
assert f0[0] == 0 and abs(f0[15] - 440) < .01
assert len(f0) == round(720 * 60 * 44100 / (120 * 480 * 512))
with wave.open(str(OUT / "voice.wav")) as f:
    assert (f.getnchannels(), f.getsampwidth(), f.getframerate()) == (2, 2, 44100)
    assert f.getnframes() == len(f0) * 512
    pcm = array.array("h", f.readframes(f.getnframes()))
    assert pcm[::2] == pcm[1::2], "Centered mono voice was mixed incorrectly"
    rms = math.sqrt(sum((v / 32768) ** 2 for v in pcm) / len(pcm))
    quality = check_voice(pcm)
predicted = array.array("f", (job / "predicted-variance.f32").read_bytes())
assert len(predicted) == len(f0) * 3
voicing = predicted[1::3]
assert max(voicing) < -10 and min(voicing) < -80, "Neutral control lost the learned physical energy"
assert (job / "variance-tokens.txt").exists() and (job / "word-dur.txt").exists()

# An expression is an offset from predicted energy. Check the physical tensor
# and the actual downstream audio, so filling a constant absolute level fails.
part["automations"] = {"voicing": {"defaultValue": 0, "points": [0, -6, 1920, -6]}}
quieter_fixture = OUT / "quieter.dse"
quieter_fixture.write_text(json.dumps(score, ensure_ascii=False))
quiet = invoke("--project", quieter_fixture, "--render", OUT / "quieter.wav")
quiet_job = Path(re.search(r"任务目录: (.+)", quiet.stdout)[1])
physical = [float(v) for v in (quiet_job / "voicing.txt").read_text().split()]
assert all(abs(v - max(-96, base - 6)) < .0001 for v, base in zip(physical, voicing))
with wave.open(str(OUT / "quieter.wav")) as f:
    quiet_quality = check_voice(array.array("h", f.readframes(f.getnframes())))
assert .35 < quiet_quality["rms"] / quality["rms"] < .8, (quality, quiet_quality)
part.pop("automations")

# Overlapping notes must fail without replacing an existing output.
original = (OUT / "voice.wav").read_bytes()
part["notes"].append({"pos": 200, "dur": 300, "pitch": 70, "lyric": "a", "pronunciation": "zh/a"})
fixture.write_text(json.dumps(score))
invoke("--project", fixture, "--render", OUT / "voice.wav", success=False)
assert (OUT / "voice.wav").read_bytes() == original
part["notes"].pop()
part["notes"][0]["pronunciation"] = "unknown/phoneme"
fixture.write_text(json.dumps(score))
invoke("--project", fixture, "--render", OUT / "invalid.wav", success=False)
assert not (OUT / "invalid.wav").exists()
part["notes"][0]["pronunciation"] = "zh/a"
score["tracks"][0]["mute"] = True
fixture.write_text(json.dumps(score))
invoke("--project", fixture, "--render", OUT / "muted.wav", success=False)
assert not (OUT / "muted.wav").exists()
score["tracks"][0]["mute"] = False
fixture.write_text(json.dumps(score, ensure_ascii=False, indent=2))
for option, value in [("--steps", "0"), ("--steps", "4bad"), ("--workers", "-1"),
                      ("--duration", "nan"), ("--width", "zero")]:
    invalid = invoke(option, value, "--render", OUT / "invalid.wav", success=False)
    assert invalid.returncode == 2
    assert not (OUT / "invalid.wav").exists()

report = {"status": "passed", "nativeJob": str(job), "tokens": tokens,
          "frames": len(f0), "wavFrames": len(f0) * 512, "sampleRate": 44100, "channels": 2,
          "rms": rms, "waveformQuality": quality, "voicingDbRange": [min(voicing), max(voicing)],
          "minus6DbControl": quiet_quality,
          "rejectionChecks": ["overlap", "unknown phoneme", "muted project", "invalid CLI numeric values"],
          "qualityScope": "real native variance/acoustic/vocoder; neutral energy, saturation and DC regression checks"}
(OUT / "report.json").write_text(json.dumps(report, indent=2))
print("Real DiffSinger: Chinese lyric -> tokens/durations/ASM F0 -> acoustic -> vocoder -> stereo WAV OK")
