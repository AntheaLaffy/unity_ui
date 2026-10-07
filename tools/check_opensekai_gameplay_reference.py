#!/usr/bin/env python3
"""Capture 0114 music-clock states and compare the real native canvas to video."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from PIL import Image, ImageDraw
from check_opensekai_song import isolated_run
from compare_opensekai_reference import panel

ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / "application/OpenSekai-ASMR"
REFERENCE = PROJECT / "resources/references/0114_01"


def capture_case(output, name, manifest, speed, events, autoplay, chart="0114_01-master", audio_frames=5382910):
    destination = output / name
    destination.mkdir()
    source = PROJECT / "content/library" / chart
    requested = sorted(time for time, command in events if command == "capture")
    with tempfile.TemporaryDirectory(prefix="ojsk-gameplay-reference-", dir=ROOT / "build") as temporary:
        data = Path(temporary)
        entry = data / source.name
        entry.mkdir()
        for filename in ("manifest.json", manifest["audioFileName"], manifest["scoreFileName"], manifest["jacketFileName"]):
            shutil.copyfile(source / filename, entry / filename)
        (data / "native-play-settings.json").write_text(json.dumps({"noteSpeed": speed}))
        script = data / "input.txt"
        script.write_text("".join(f"music {time:.9f} {command}\n" for time, command in sorted(events)))
        duration = max(requested) + 8
        command = [str(ROOT / "build/editor"), "--project", str(PROJECT), "--data-dir", str(data),
                   "--app-chart", manifest["id"], "--app-screen", "auto" if autoplay else "play",
                   "--app-only", "--duration", str(duration), "--size", "1280", "720",
                   "--app-input-script", str(script), "--app-captures", str(destination),
                   "--output", str(destination / "final.png")]
        env = os.environ.copy()
        env.update(SDL_AUDIO_DRIVER="dummy", ASMUI_PROFILE_AUDIO="1")
        try:
            run = isolated_run(command, env, duration+40)
        except subprocess.TimeoutExpired as error:
            logs = [(part.decode(errors="replace") if isinstance(part, bytes) else part or "")
                    for part in (error.stdout, error.stderr)]
            (destination / "run.log").write_text("".join(logs))
            raise
        log = run.stdout + run.stderr
        (destination / "run.log").write_text(log)
        assert run.returncode == 0, log
        assert f"Application audio: {audio_frames} stereo frames" in log
        assert "Application sound bank: 18 clips" in log
        assert not re.search(r"Validation Error|VUID-|sound error:|missing=[1-9]|dropped=[1-9]", log), log
        captures = re.findall(r"Application capture: wall=([\d.]+) music=([\d.]+) requested=([\d.]+)", log)
        images = sorted(destination.glob("frame-*.png"))
        assert len(images) == len(captures) == len(requested), (images, captures)
        rows = []
        for image, capture, target in zip(images, captures, requested):
            wall, actual, logged_target = map(float, capture)
            assert abs(logged_target-target) < 1e-6 and 0 <= actual-target < .05
            rows.append({"native": str(image),
                          "requested_music_seconds": target, "actual_music_seconds": actual,
                          "capture_wall_seconds": wall, "note_speed": speed,
                          "input": "Actual native Auto session" if autoplay else "Real SDL keyboard input"})
        return rows


def run_case(output, name, states, manifest, first_note, speed, manual):
    events = []
    if manual:
        # The real first long note covers lanes 0..5; A is the held contact.
        events += [(manifest["fillerSec"], "97 1"), (manifest["fillerSec"] + 60/159, "97 0")]
    events += [(manifest["fillerSec"] + frame["recording_seconds"] - first_note, "capture") for frame in states]
    rows = capture_case(output, name, manifest, speed, events, not manual)
    return [{**row, "state": state["state"], "reference": str(REFERENCE / state["path"]),
             "recording_seconds": state["recording_seconds"]} for row, state in zip(rows, states)]


def feedback_details(output, manifest):
    gold_start = manifest["fillerSec"] + 4*60/159
    manual = [(0.22, "Empty press: column flashes white"),
              (0.85, "Still held: column flash has faded"),
              (gold_start+0.075, "Gold hold: first hit and gain popup"),
              (4.35, "Gold hold sustained: beam/pattern, no gain popup"),
              (5.05, "Released: hold beam stops, body clipped at line")]
    events = [(0.20, "97 1"), (1.10, "97 0"), (gold_start, "107 1"), (4.75, "107 0")]
    events += [(time, "capture") for time, _ in manual]
    rows = capture_case(output, "feedback-manual", manifest, 6.0, events, False)
    states = [{**row, "state": state} for row, (_, state) in zip(rows, manual)]
    flick = [(5.95, "Flick marker: suspended above base"),
             (6.65, "Flick marker: rising and fading"),
             (7.35, "Flick marker: next rise cycle near hit line")]
    rows = capture_case(output, "feedback-flick", manifest, 6.0,
                        [(time, "capture") for time, _ in flick], True)
    states += [{**row, "state": state,
                "rise_phase": ((row["actual_music_seconds"]-manifest["fillerSec"])*2) % 1}
               for row, (_, state) in zip(rows, flick)]
    canvas = Image.new("RGB", (2560, ((len(states)+1)//2)*760), (20, 20, 24))
    draw = ImageDraw.Draw(canvas)
    for i, state in enumerate(states):
        x, y = (i % 2)*1280, (i//2)*760
        draw.text((x+16, y+10), f"{state['state']} / music {state['actual_music_seconds']:.3f}s", fill="white")
        canvas.paste(panel(state["native"], True), (x, y+40))
    path = output / "feedback.png"
    canvas.save(path)
    return {"image": str(path), "states": states,
            "scope": "Actual official-chart inputs and animation states for visual inspection; no claim of automatic pixel equivalence."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefix", default="opensekai-gameplay-reference")
    parser.add_argument("--hold-only", action="store_true")
    parser.add_argument("--feedback-details", action="store_true", help="also capture empty presses, continuous holds and flick rise cycles")
    parser.add_argument("--speed", type=float, default=11.3)
    args = parser.parse_args()
    if Path(args.prefix).name != args.prefix or not 1 <= args.speed <= 12:
        parser.error("prefix must be a plain name; speed must be 1..12")
    output = ROOT / "build" / args.prefix
    output.mkdir(exist_ok=True)
    if list(output.glob("*/frame-*.png")):
        parser.error("Use a fresh prefix to preserve the previous captures")
    manifest = json.loads((PROJECT / "content/library/0114_01-master/manifest.json").read_text())
    recipe = json.loads((PROJECT / "resources/audio/0114_01/edit.json").read_text())
    reference = json.loads((REFERENCE / "reference.json").read_text())
    frames = {frame["state"]: frame for frame in reference["frames"]}
    first_note = recipe["calibration"]["visual_first_judgement_seconds"]
    pairs = run_case(output, "manual-hold", [frames["hold-start"]], manifest, first_note, args.speed, True)
    if not args.hold_only:
        pairs += run_case(output, "auto", [frames[n] for n in ("flick-pink", "tap-blue", "curved-hold")],
                          manifest, first_note, args.speed, False)
    canvas = Image.new("RGB", (2560, len(pairs)*760), (20, 20, 24))
    draw = ImageDraw.Draw(canvas)
    for i, pair in enumerate(pairs):
        y = i*760
        draw.text((16, y+10), f"Reference: {pair['state']} / {pair['recording_seconds']:.3f}s", fill="white")
        draw.text((1296, y+10), f"Rust / ASM: {pair['input']} / speed {args.speed:.1f}", fill="white")
        canvas.paste(panel(pair["reference"]), (0, y+40))
        canvas.paste(panel(pair["native"], True), (1280, y+40))
    comparison = output / "comparison.png"
    canvas.save(comparison)
    report = {"chart": manifest["id"], "comparison": str(comparison), "pairs": pairs,
              "speed": {"capture": args.speed, "application_default": 6.0,
                        "reason": "Visual fit of first hold depth; recording's exact setting is unknown."},
              "allowed_differences": ["Native Auto judgement and combo statistics differ from the recorded manual performance",
                                      "Rust gameplay data and timing need not match the original implementation"],
              "scope": "Actual hardware Vulkan canvas, original official MASTER, supplied WAV and source SE. Video preview is excluded. This comparison supports visual review, not pixel equivalence or physical speaker verification."}
    if args.feedback_details:
        report["feedback"] = feedback_details(output, manifest)
    (output / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2)+"\n")
    print(json.dumps(report, ensure_ascii=False, indent=2), flush=True)


if __name__ == "__main__":
    main()
