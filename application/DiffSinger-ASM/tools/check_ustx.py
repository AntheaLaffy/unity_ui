#!/usr/bin/env python3
"""Read-only acceptance of the user's USTX corpus and native save/reopen."""
from pathlib import Path
import hashlib
import json
import subprocess
import sys
import yaml

APP = Path(__file__).resolve().parents[1]
BIN = APP / "build/diffsinger-asm"
OUT = APP / "build/ustx-check"
OUT.mkdir(parents=True, exist_ok=True)
ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("/home/fuurin/Music")
FILES = sorted(ROOT.rglob("*.ustx"))
assert FILES, f"No USTX projects under {ROOT}"
rows = []

def invoke(*args, success=True):
    p = subprocess.run([str(BIN), *map(str, args)], text=True, capture_output=True, timeout=60)
    assert (p.returncode == 0) == success, p.stdout + p.stderr
    return p

for index, source in enumerate(FILES):
    original = source.read_bytes()
    digest = hashlib.sha256(original).hexdigest()
    # BaseLoader mirrors typed USTX string fields rather than YAML 1.1 booleans:
    # the Japanese syllable "no" remains literal text.
    parsed = yaml.load(original.decode("utf-8-sig"), Loader=yaml.CBaseLoader)
    saved = OUT / f"{index:02d}.dse"
    invoke("--project", source, "--write-demo", saved)
    actual = json.loads(saved.read_text())
    assert actual["application"] == "DiffSinger-ASM"
    assert actual["tempos"][0]["bpm"] == float(parsed["tempos"][0]["bpm"])
    assert len(actual["tracks"]) == len(parsed["tracks"])
    assert actual["openutau"]["ustx"].encode("utf-8") == original
    assert actual["openutau"]["waveParts"] == len(parsed.get("wave_parts", []))
    notes = 0
    for wave in parsed.get("wave_parts", []):
        candidates = [p for p in actual["tracks"][int(wave["track_no"])]["parts"]
                      if p["type"] == "audio" and p["name"] == wave["name"]]
        assert len(candidates) == 1
        dest = candidates[0]
        assert Path(dest["file"]) == (source.parent / wave["relative_path"]).resolve()
        assert dest["pos"] == int(wave["position"]) and dest["durationMs"] == float(wave["file_duration_ms"])
        for key, imported in [("skip", "skip"), ("trim", "trim"), ("fadein", "fadeIn"), ("fadeout", "fadeOut")]:
            assert dest[imported] == int(wave[key])
    for part in parsed["voice_parts"]:
        track = actual["tracks"][int(part["track_no"])]
        matches = [p for p in track["parts"] if p["name"] == part["name"] and p["pos"] == int(part["position"])]
        assert len(matches) == 1, source
        dest = matches[0]
        assert len(dest["notes"]) == len(part["notes"])
        for expected, note in zip(part["notes"], dest["notes"]):
            assert (note["pos"], note["dur"], note["pitch"], note["lyric"]) == (
                int(expected["position"]), int(expected["duration"]), int(expected["tone"]), expected["lyric"]), (source, expected, note)
        notes += len(part["notes"])
    roundtrip = OUT / f"{index:02d}-reopened.dse"
    invoke("--project", saved, "--write-demo", roundtrip)
    reopened = json.loads(roundtrip.read_text())
    assert reopened["tracks"] == actual["tracks"] and reopened["openutau"] == actual["openutau"]
    assert hashlib.sha256(source.read_bytes()).hexdigest() == digest, "Source project changed"
    rows.append({"file": str(source.relative_to(ROOT)), "status": "passed", "sourceSha256": digest,
                 "notes": notes, "tracks": len(parsed["tracks"]), "voiceParts": len(parsed["voice_parts"]),
                 "preservedWaveParts": actual["openutau"]["waveParts"],
                 "bakedPitchPoints": sum(sum(len(line) // 2 for line in p["pitch"]) for t in actual["tracks"] for p in t["parts"] if p["type"] == "midi")})
    print(f"USTX {index + 1}/{len(FILES)}: {source.name} ({notes} notes) OK", flush=True)

# Test malformed imports and the original-file write guard on disposable files.
base = {"ustx_version": "0.9", "tempos": [{"position": 0, "bpm": 120}],
        "time_signatures": [{"bar_position": 0, "beat_per_bar": 4, "beat_unit": 4}],
        "tracks": [{"track_name": "Vocal"}], "voice_parts": [{"name": "Voice", "track_no": 0,
        "position": 0, "duration": 480, "notes": [{"position": 0, "duration": 480, "tone": 69, "lyric": "a"}], "curves": []}]}
fixture = OUT / "guard.ustx"
fixture.write_text(json.dumps(base))
guard = fixture.read_bytes()
invoke("--project", fixture, "--write-demo", fixture, success=False)
assert fixture.read_bytes() == guard
for name, edit in [
    ("tempo map", lambda d: d["tempos"].append({"position": 480, "bpm": 90})),
    ("nonfinite tempo", lambda d: d["tempos"][0].update(bpm="nan")),
    ("negative note", lambda d: d["voice_parts"][0]["notes"][0].update(position=-1)),
    ("curve mismatch", lambda d: d["voice_parts"][0]["curves"].append({"abbr": "pitd", "xs": [0, 1], "ys": [0]})),
    ("curve order", lambda d: d["voice_parts"][0]["curves"].append({"abbr": "pitd", "xs": [2, 1], "ys": [0, 0]})),
]:
    value = json.loads(json.dumps(base))
    edit(value)
    fixture.write_text(json.dumps(value))
    invoke("--project", fixture, "--write-demo", OUT / "invalid.dse", success=False)
    assert not (OUT / "invalid.dse").exists(), name
fixture.write_text("name: first\nname: duplicate\n")
invoke("--project", fixture, "--write-demo", OUT / "invalid.dse", success=False)
report = {"status": "passed", "projects": len(rows), "sourceFilesUnchanged": True,
          "sourceDataRetained": True, "noteAndLyricComparison": True, "nativeRoundtrip": True,
          "scope": "editable voice/audio clip metadata and original USTX retained; audio decoding/playback checked separately; unmodeled expressions not rendered", "files": rows}
(OUT / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2))
print(f"USTX acceptance: {len(rows)} projects imported, saved and reopened; source hashes unchanged")
