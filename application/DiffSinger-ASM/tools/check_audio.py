#!/usr/bin/env python3
"""Audio metadata, real local media decoding and native voice+audio mixing."""
from pathlib import Path
import array
import hashlib
import json
import math
import subprocess
import wave
import yaml

APP = Path(__file__).resolve().parents[1]
BIN = APP / "build/diffsinger-asm"
OUT = APP / "build/audio-check"
OUT.mkdir(parents=True, exist_ok=True)


def invoke(*args, success=True):
    p = subprocess.run([str(BIN), *map(str, args)], text=True, capture_output=True, timeout=120)
    assert (p.returncode == 0) == success, p.stdout + p.stderr
    return p


def read(path):
    with wave.open(str(path)) as f:
        assert (f.getnchannels(), f.getframerate(), f.getsampwidth()) == (2, 44100, 2)
        return f.getnframes(), array.array("h", f.readframes(f.getnframes()))


# The production application decodes the media. Python only compares its exported PCM.
assets = set()
for source in Path("/home/fuurin/Music").rglob("*.ustx"):
    doc = yaml.load(source.read_text(encoding="utf-8-sig"), Loader=yaml.CBaseLoader)
    for clip in doc.get("wave_parts", []):
        assets.add((source.parent / clip["relative_path"]).resolve())
rows = []
for i, asset in enumerate(sorted(assets)):
    digest = hashlib.sha256(asset.read_bytes()).hexdigest()
    imported = OUT / "import.dse"
    invoke("--new", "--import-audio", asset, "--write-demo", imported)
    score = json.loads(imported.read_text())
    clip = next(p for t in score["tracks"] for p in t["parts"] if p["type"] == "audio")
    # Read a middle excerpt; separated vocals often begin with digital silence.
    clip["skip"] = math.floor(clip["durationMs"] * 120 * 480 / 60000 / 2)
    clip["trim"] = max(0, math.ceil(clip["durationMs"] * 120 * 480 / 60000 - clip["skip"] - 1920))
    imported.write_text(json.dumps(score, ensure_ascii=False))
    invoke("--project", imported, "--render-audio", OUT / "excerpt.wav")
    frames, pcm = read(OUT / "excerpt.wav")
    assert abs(frames - 88200) <= 60, (asset, frames)
    assert pcm and max(map(abs, pcm)) > 0, asset
    assert hashlib.sha256(asset.read_bytes()).hexdigest() == digest
    rows.append({"file": str(asset), "sha256": digest, "excerptFrames": frames,
                 "durationMs": clip["durationMs"], "sourceUnchanged": True})
    print(f"Audio {i + 1}/{len(assets)}: {asset.name} OK", flush=True)

# Known stereo data verifies offset/gain/pan and native voice addition independently.
backing = OUT / "backing.wav"
with wave.open(str(backing), "wb") as f:
    f.setparams((2, 2, 44100, 0, "NONE", "not compressed"))
    f.writeframes(array.array("h", [1024, -2048] * 44100).tobytes())
score = json.loads((APP / "examples/first-song.tlp").read_text())
score["tracks"][0]["gain"] = -12
part = score["tracks"][0]["parts"][0]
part["notes"] = [{"pos": 120, "dur": 600, "pitch": 69, "lyric": "啊", "pronunciation": "", "properties": {}}]
part["pitch"] = [[120, 69, 720, 69]]
part["endOffset"] = 1920
fixture = OUT / "voice.dse"
fixture.write_text(json.dumps(score, ensure_ascii=False))
invoke("--project", fixture, "--render", OUT / "voice.wav")
voice_frames, voice = read(OUT / "voice.wav")
assert max(map(abs, voice)) < 30000, "The independent voice reference must retain headroom"
score["tracks"].append({"name": "Backing", "gain": 0, "pan": .25, "mute": False, "solo": False,
                       "parts": [{"type": "audio", "name": "backing", "file": str(backing),
                                  "pos": 120, "durationMs": 1000, "skip": 0, "trim": 0,
                                  "fadeIn": 0, "fadeOut": 0, "gain": 0}]})
fixture.write_text(json.dumps(score, ensure_ascii=False))
invoke("--project", fixture, "--render", OUT / "mixed.wav")
mixed_frames, mixed = read(OUT / "mixed.wav")
offset = math.floor(120 * 60 * 44100 / (120 * 480) + .5)
assert mixed_frames == offset + 44100
for frame in range(mixed_frames):
    for channel in range(2):
        expected = voice[frame * 2 + channel] if frame < voice_frames else 0
        if frame >= offset:
            expected += 768 if channel == 0 else -2048
        assert abs(mixed[frame * 2 + channel] - expected) <= 2, (frame, channel, expected)

# Solo selects the audio track and bypasses unavailable voice models.
score["tracks"][1]["solo"] = True
fixture.write_text(json.dumps(score))
invoke("--project", fixture, "--engine", "/tmp/no-diffsinger-engine", "--render", OUT / "solo.wav")
_, solo = read(OUT / "solo.wav")
assert all(solo[i] == 0 for i in range(offset * 2))
assert abs(solo[offset * 2] - 768) <= 1 and abs(solo[offset * 2 + 1] + 2048) <= 1
original = backing.read_bytes()
invoke("--project", fixture, "--render-audio", backing, success=False)
invoke("--project", fixture, "--write-demo", backing, success=False)
assert backing.read_bytes() == original
alias = OUT / "backing-alias.wav"
if not alias.exists():
    alias.hardlink_to(backing)
invoke("--project", fixture, "--render-audio", alias, success=False)
assert alias.read_bytes() == original
score["tracks"][1]["mute"] = True
fixture.write_text(json.dumps(score))
prior = (OUT / "mixed.wav").read_bytes()
invoke("--project", fixture, "--render", OUT / "mixed.wav", success=False)
assert (OUT / "mixed.wav").read_bytes() == prior
report = {"status": "passed", "actualAssets": len(rows), "assets": rows,
          "checks": ["real WAV/FLAC decoding", "source hashes", "clip timing", "native voice plus backing PCM comparison",
                     "stereo pan", "solo bypasses inactive voice models", "mute", "original media and hardlink write guards"],
          "qualityScope": "numerical PCM mixing; perceptual singing quality and physical speaker output not certified"}
(OUT / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2))
print("Audio acceptance: actual media, native voice addition, stereo timing, solo/mute and source protection OK")
