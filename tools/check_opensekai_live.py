#!/usr/bin/env python3
"""Verify live input and audio through a real SDL/Vulkan window, in temporary data."""
import argparse
import array
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / "application/OpenSekai_Community_rs"
sys.path.insert(0, str(PROJECT / "tools"))
from live_fixture import create
from check_opensekai_song import isolated_run


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--editor", type=Path, default=ROOT / "build/editor")
    parser.add_argument("--prefix", default="opensekai-live-window")
    args = parser.parse_args()
    assert re.fullmatch(r"[a-zA-Z0-9_-]+", args.prefix)
    report = []
    # Actual SDL key events exercise both Shift keys as game columns.
    for music, row, lane, middle_key, outer_key in (
        (False, "upper", 7, ord("k"), ord("i")),
        (False, "lower-left-shift", 0, ord("a"), 1073742049),
        (False, "lower-right-shift", 11, 13, 1073742053),
        (True, "auto", 7, ord("k"), ord("i")),
    ):
        name = args.prefix + ("-audio" if music else "-keys" if row == "upper" else "-"+row)
        output = ROOT / "build" / name
        with tempfile.TemporaryDirectory(prefix=name + "-", dir=ROOT / "build") as temporary:
            entry = create(temporary, music)
            if not music:
                chart = json.loads((entry / "score.json").read_text())
                chart["NoteList"][-1].update(laneStart=lane, laneEnd=min(lane+1, 11))
                (entry / "score.json").write_text(json.dumps(chart))
            command = [str(args.editor.resolve()), "--project", str(PROJECT), "--data-dir", temporary,
                       "--app-chart", "native-live-fixture", "--app-screen", "auto" if music else "play",
                       "--app-only", "--app-exit-on-result", "--duration", "20", "--size", "1440", "900", "--output", str(output.with_suffix(".png"))]
            if not music:
                script = Path(temporary) / "keys.txt"
                script.write_text("\n".join(f"{time} {key} {down}" for time, key, down in [
                    (.4, 27, 1), (.41, 27, 0), (.9, 13, 1), (.91, 13, 0),
                    (3.45, ord("q"), 1), (3.46, ord("q"), 0),
                    (3.5, ord("a"), 1), (3.6, ord("a"), 0),
                    (4.5, ord("f"), 1), (5.5, ord("f"), 0),
                    (6.4, outer_key, 1), (6.41, outer_key, 0),
                    (6.44, middle_key, 1), (6.47, middle_key, 0),
                    (6.5, outer_key, 1), (6.6, outer_key, 0)]) + "\n")
                command += ["--app-input-script", str(script)]
            env = os.environ.copy()
            env["SDL_AUDIO_DRIVER"] = "dummy"
            env["ASMUI_PROFILE_FRAMES"] = "1"
            env["ASMUI_CAPTURE_SFX"] = str(output.with_suffix(".sfx.f32"))
            result = isolated_run(command, env, timeout=120)
            log = result.stdout + result.stderr
            output.with_suffix(".log").write_text(log)
            assert result.returncode == 0, f"Native live failed: {output}.log"
            assert "Editor rendered:" in log and "Application image:" in log
            assert "Application sound bank: 18 clips" in log and "Application sound error:" not in log
            sound_match = re.search(r"Application sound: epoch=\d+, starts=(\d+), loops=(\d+), stops=(\d+), frames=(\d+), nonzero=(\d+), peak=([\d.]+), dropped=(\d+)", log)
            assert sound_match, log
            starts, loops, stops, frames, nonzero, peak, dropped = map(float, sound_match.groups())
            assert starts >= 5 and loops >= 1 and stops >= 1 and nonzero > 0 and dropped == 0
            pcm = array.array("f")
            pcm.frombytes(output.with_suffix(".sfx.f32").read_bytes())
            assert len(pcm) == int(frames) * 2 and max(map(abs, pcm)) > .01
            assert not re.search(r"Validation Error|VUID-|ERROR: AddressSanitizer|runtime error:|missing=[1-9]", log), log
            saved = json.loads((entry / "native-last-result.json").read_text())
            data = saved["result"]
            assert data["total"] == 7, saved
            assert data["life"] == 1000 and sum(data["counts"]) == 7, saved
            assert data["counts"][3:6] == [0, 0, 0], saved
            assert data["autoplay"] == music and data["practice_without_music"] != music
            if music:
                assert data["counts"][6] == 7 and "Application audio: 336000 stereo frames" in log, saved
            else:
                assert data["max_combo"] == 7 and data["counts"][6] == 0, saved
                assert sum(data["counts"][:2]) == 7, saved  # invalid outer presses cannot steal hits
                assert len(re.findall("Application input:", log)) == 16
            report.append({"mode": "SDL dummy audio / autoplay" if music else "SDL key input / "+row,
                           "gesture": {"middle":middle_key, "outer":outer_key, "lane":lane,
                                       "invalid_outer_press_tested":not music},
                           "sound": {"starts": starts, "loops": loops, "stops": stops, "mixed_frames": frames,
                                     "nonzero_samples": nonzero, "peak_before_clip": peak, "dropped": dropped,
                                     "capture": str(output.with_suffix(".sfx.f32"))},
                           "result": saved, "image": str(output.with_suffix(".png")),
                           "scope": "Actual native window, scripted input; no Unity raster comparison or physical speaker verification."})
    (ROOT / "build" / (args.prefix + ".json")).write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    print(json.dumps(report, ensure_ascii=False))


if __name__ == "__main__":
    main()
