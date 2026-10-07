#!/usr/bin/env python3
"""Capture the four manual endings and match their actual mixed PCM to source SE."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import numpy as np
from check_opensekai_song import isolated_run

ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / "application/OpenSekai-ASMR"
sys.path.insert(0, str(PROJECT / "tools"))
from live_fixture import create


def match_pcm(recording, cue):
    bank = json.loads((PROJECT / "resources/opensekai/soundbank.json").read_text())
    sound = next(c for c in bank["cues"] if c["name"] == cue)
    source = PROJECT / "resources/opensekai" / sound["clips"][0]["path"]
    decoded = subprocess.check_output(["ffmpeg", "-v", "error", "-i", str(source),
                                      "-f", "f32le", "-acodec", "pcm_f32le", "-ac", "2", "-ar", "48000", "pipe:1"])
    reference = np.clip(np.frombuffer(decoded, dtype="<f4").reshape(-1,2)[:24000,0] * sound["gain"], -1, 1)
    mixed = np.fromfile(recording, dtype="<f4").reshape(-1,2)[:,0].astype(np.float64)
    length = 1 << (len(mixed) + len(reference) - 1).bit_length()
    correlation = np.fft.irfft(np.fft.rfft(mixed, length) * np.conj(np.fft.rfft(reference, length)), length)
    energy = np.concatenate(([0.], np.cumsum(mixed*mixed)))
    source_energy = np.sum(reference.astype(float)**2)
    assert source_energy > 1e-5
    window_energy = np.maximum(energy[len(reference):] - energy[:-len(reference)], 0)
    norm = np.sqrt(window_energy * source_energy)
    # FFT roundoff in silent windows is not evidence of a source match.
    similarity = np.divide(correlation[:len(norm)], norm, out=np.zeros(len(norm)),
                           where=window_energy > source_energy * 1e-6)
    position = int(np.argmax(similarity))
    assert similarity[position] > .995, (cue, similarity[position])
    assert 4.8 < position/48000 < 5.7, (cue, position/48000)
    return {"cue": cue, "matched_seconds": len(reference)/48000,
            "normalized_correlation": float(similarity[position]), "onset_in_capture_seconds": position/48000}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefix", default="opensekai-results")
    parser.add_argument("--cover", type=Path, help="optional local PNG cover for visual reference captures")
    parser.add_argument("--motion", action="store_true", help="also capture rank fill, cover entry and rolling result data")
    parser.add_argument("--character", action="store_true", help="keep the settled result open to capture the looping 2D character")
    parser.add_argument("--kind", choices=("all_perfect", "full_combo", "clear", "finish"), help="capture just one ending")
    args = parser.parse_args()
    assert re.fullmatch(r"[a-zA-Z0-9_-]+", args.prefix)
    output = ROOT / "build" / args.prefix
    output.mkdir(exist_ok=True)
    reports = []
    for kind in (args.kind,) if args.kind else ("all_perfect", "full_combo", "clear", "finish"):
        captures = output / kind
        captures.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="opensekai-result-", dir=ROOT / "build") as data:
            entry = create(data)
            if args.cover:
                shutil.copyfile(args.cover, entry / "jacket.png")
                manifest = json.loads((entry / "manifest.json").read_text())
                manifest.update(title="裏表ラバーズ", scoreTitle="裏表ラバーズ [MASTER]",
                                musicDifficultyType="master", jacketFileName="jacket.png")
                (entry / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False))
            score = json.loads((entry / "score.json").read_text())
            first = score["NoteList"][0]
            ticks = [96*i for i in range(1,21)] if kind == "finish" else [960,1920]
            score["NoteList"] = [dict(first, id=i+1, ticks=tick) for i,tick in enumerate(ticks)]
            score["MusicScoreTicksMax"] = max(ticks)
            (entry / "score.json").write_text(json.dumps(score))
            keys = []
            if kind in ("all_perfect", "full_combo"):
                keys += [(3.,ord("a"),1), (3.15,ord("a"),0)]
            if kind != "finish":
                time = 4.06 if kind == "full_combo" else 4.
                keys += [(time,ord("a"),1),(time+.15,ord("a"),0)]
            for time in (5.65,6.1,7.0):
                keys += [(time,0x40000045,1),(time+.02,0x40000045,0)]
            motion_times = [11.4,12.1,13.2,13.9,14.6] if args.motion else []
            character_times = [15.7,16.55,16.95,18.2,19.2,20.2] if args.character else []
            for time in motion_times + character_times:
                keys += [(time,0x40000045,1),(time+.02,0x40000045,0)]
            script = Path(data) / "keys.txt"
            script.write_text("".join(f"{t} {key} {down}\n" for t,key,down in sorted(keys)))
            command = [str(ROOT / "build/editor"), "--project", str(PROJECT), "--data-dir", data,
                       "--app-chart", "native-live-fixture", "--app-screen", "play", "--app-only",
                       "--duration", "22" if args.character else "20", "--app-input-script", str(script),
                       "--size", "1280", "720",
                       "--app-captures", str(captures), "--output", str(captures / "result.png")]
            if not args.character:
                command.append("--app-exit-on-result")
            env = os.environ.copy()
            env.update(SDL_AUDIO_DRIVER="dummy", ASMUI_CAPTURE_SFX=str(captures / "sound.f32"))
            run = isolated_run(command, env, timeout=80)
            log = run.stdout + run.stderr
            (captures / "run.log").write_text(log)
            assert run.returncode == 0, captures / "run.log"
            assert "Application sound bank: 18 clips" in log and "Editor screenshot:" in log
            assert not re.search(r"Validation Error|VUID-|Application sound error:|dropped=[1-9]|missing=[1-9]", log)
            saved = json.loads((entry / "native-last-result.json").read_text())
            assert saved["scoringRule"] == "ojsk-dynamic-v1"
            result = saved["result"]
            c = result["counts"]
            if kind == "all_perfect": assert c[0]+c[1] == result["total"], saved
            elif kind == "full_combo": assert result["max_combo"] == result["total"] and c[2] > 0, saved
            elif kind == "clear": assert c[5] == 1 and result["life"] > 0, saved
            else: assert result["life"] == 0 and c[5] == 20, saved
            images = re.findall(r"Editor screenshot: (.+/frame-\d+\.png)", log)
            assert len(images) == 3+len(motion_times)+len(character_times) and all(Path(p).is_file() for p in images)
            sound = match_pcm(captures / "sound.f32", "se_live_" + kind)
            reports.append({"kind": kind, "result": saved, "sound": sound, "animation_frames": images[:3],
                            "result_motion_frames": [{"wall_seconds":t,"image":image} for t,image in zip(motion_times,images[3:])],
                            "character_frames": [{"wall_seconds":t,"image":image} for t,image in zip(character_times,images[3+len(motion_times):])],
                            "result_image": str(captures / "result.png"),
                            "scope": "Hardware Vulkan window, imported background/sprites/curves/ring/particle modules and captured source SE. Visual comparison and native particle integration parity remain separate checks."})
            print(kind, sound, flush=True)
    (output / "report.json").write_text(json.dumps(reports, ensure_ascii=False, indent=2) + "\n")
    if args.motion:
        from PIL import Image,ImageDraw
        from compare_opensekai_reference import panel
        rows = next((r for r in reports if r["kind"] == "full_combo"), reports[0])
        states = [("Rank entry",rows["result_motion_frames"][0]["image"]),
                  ("Rank fill",rows["result_motion_frames"][1]["image"]),
                  ("Cover/header entry",rows["result_motion_frames"][2]["image"]),
                  ("Rolling score/data",rows["result_motion_frames"][3]["image"]),
                  ("Statistics settle",rows["result_motion_frames"][4]["image"]),
                  ("Final saved result",rows["result_image"])]
        canvas=Image.new("RGB",(2560,3*760),(20,20,24));draw=ImageDraw.Draw(canvas)
        for i,(state,path) in enumerate(states):
            x,y=(i%2)*1280,(i//2)*760
            draw.text((x+16,y+10),state,fill="white");canvas.paste(panel(path,True),(x,y+40))
        canvas.save(output/"result-motion.png")
    if args.character:
        from PIL import Image, ImageDraw
        from compare_opensekai_reference import panel
        rows = next((r for r in reports if r["kind"] == "full_combo"), reports[0])
        canvas=Image.new("RGB",(1560,2*650),(20,20,24));draw=ImageDraw.Draw(canvas)
        for i,frame in enumerate(rows["character_frames"]):
            x,y=(i%3)*520,(i//3)*650
            draw.text((x+16,y+10),f"Settled result, wall {frame['wall_seconds']}s",fill="white")
            image=panel(frame["image"],True)
            canvas.paste(image.crop((735,125,1125,720)),(x+65,y+40))
        canvas.save(output/"result-character.png")


if __name__ == "__main__":
    main()
