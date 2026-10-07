#!/usr/bin/env python3
"""Play a real library song to completion without editing user data."""
import argparse
import array
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import signal
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / "application/OpenSekai_Community_rs"


def isolated_run(command, env, timeout):
    """Keep desktop focus changes out of a long native-window playback check."""
    if not shutil.which("labwc"):
        raise RuntimeError("labwc is required for isolation; use --desktop to test on the current desktop")
    with tempfile.TemporaryDirectory(prefix="opensekai-wayland-") as temporary:
        root = Path(temporary)
        (root / "runtime").mkdir(mode=0o700)
        (root / "config").mkdir()
        status = root / "exit-status"
        isolated_env = env.copy()
        for key in ("DISPLAY", "WAYLAND_DISPLAY", "WAYLAND_SOCKET", "DBUS_SESSION_BUS_ADDRESS", "LABWC_PID"):
            isolated_env.pop(key, None)
        isolated_env.update(XDG_RUNTIME_DIR=str(root / "runtime"), WLR_BACKENDS="headless",
                            WLR_RENDERER="gles2", WLR_HEADLESS_OUTPUTS="1",
                            LABWC_UPDATE_ACTIVATION_ENV="0", SDL_VIDEO_DRIVER="wayland")
        devices = sorted(Path("/dev/dri").glob("renderD*"))
        if devices:
            isolated_env.setdefault("WLR_RENDER_DRM_DEVICE", str(devices[0]))
        # labwc does not propagate the session client's exit status.
        launcher = ("import json, subprocess, sys\nfrom pathlib import Path\n"
                    "result = subprocess.run(json.loads(sys.argv[2]))\n"
                    "Path(sys.argv[1]).write_text(str(result.returncode))\n")
        session = shlex.join([sys.executable, "-c", launcher, str(status), json.dumps(command)])
        with subprocess.Popen(["labwc", "-C", str(root / "config"), "-S", session],
                              cwd=ROOT, env=isolated_env, text=True, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, start_new_session=True) as process:
            try:
                stdout, stderr = process.communicate(timeout=timeout)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGTERM)
                try:
                    process.communicate(timeout=5)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.communicate()
                raise
        code = int(status.read_text()) if status.is_file() else process.returncode or 1
        return subprocess.CompletedProcess(command, code, stdout, stderr)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--editor", type=Path, default=ROOT / "build/editor")
    parser.add_argument("--chart", default="0001_01-master", help="library package directory")
    parser.add_argument("--prefix", default="opensekai-tell-your-world", help="output filename in build/")
    parser.add_argument("--desktop", action="store_true", help="use the current desktop instead of an isolated Wayland session")
    args = parser.parse_args()
    if Path(args.chart).name != args.chart or Path(args.prefix).name != args.prefix:
        parser.error("chart and prefix must be plain names")
    source = PROJECT / "content/library" / args.chart
    manifest = json.loads((source / "manifest.json").read_text())
    output = ROOT / "build" / args.prefix
    decoded = subprocess.check_output(["ffmpeg", "-nostdin", "-v", "error", "-i",
                                      str(source / manifest["audioFileName"]), "-vn",
                                      "-f", "f32le", "-ac", "2", "-ar", "48000", "pipe:1"])
    assert decoded and len(decoded) % 8 == 0
    expected_frames = len(decoded) // 8
    expected_seconds = expected_frames / 48000
    del decoded
    duration = max(30, int(expected_seconds + manifest["fillerSec"] + 30))
    with tempfile.TemporaryDirectory(prefix="opensekai-song-", dir=ROOT / "build") as temporary:
        song = Path(temporary) / source.name
        song.mkdir()
        for name in ["manifest.json", manifest["audioFileName"], manifest["scoreFileName"], manifest["jacketFileName"]]:
            shutil.copyfile(source / name, song / name)
        env = os.environ.copy()
        env["SDL_AUDIO_DRIVER"] = "dummy"
        env["ASMUI_CAPTURE_SFX"] = str(output.with_suffix(".sfx.f32"))
        env["ASMUI_PROFILE_AUDIO"] = "1"
        command = [str(args.editor.resolve()), "--project", str(PROJECT), "--data-dir", temporary,
                   "--app-chart", manifest["id"], "--app-screen", "auto", "--app-only",
                   "--app-exit-on-result", "--duration", str(duration), "--size", "1280", "720", "--output", str(output.with_suffix(".png"))]
        run = (subprocess.run(command, cwd=ROOT, env=env, text=True, capture_output=True, timeout=duration+30)
               if args.desktop else isolated_run(command, env, duration+30))
        log = run.stdout + run.stderr
        output.with_suffix(".log").write_text(log)
        assert run.returncode == 0, log
        assert f"Application audio: {expected_frames} stereo frames" in log
        assert "Application sound bank: 18 clips" in log
        assert not re.search(r"sound error:|Validation Error|VUID-|runtime error:|AddressSanitizer|missing=[1-9]", log), log
        saved_path = song / "native-last-result.json"
        assert saved_path.is_file(), f"Song did not reach results; inspect {output}.log (including focus/pause events)"
        saved = json.loads(saved_path.read_text())
        result = saved["result"]
        assert result["autoplay"] and not result["practice_without_music"]
        assert result["total"] == result["counts"][6] == manifest["officialNoteCount"]
        assert result["life"] == 1000 and result["counts"][5] == 0
        clocks = [{"wall":float(w),"sample":float(s)} for w,s in re.findall(r"Application music clock: wall=([\d.]+) sample=([\d.]+)", log)]
        complete = re.search(r"Application music complete: frames=(\d+) sample=([\d.]+)", log)
        assert complete and int(complete[1]) == expected_frames and abs(float(complete[2]) - expected_seconds) < .001, "Music was cut before the WAV ended"
        match = re.search(r"Application sound: epoch=\d+, starts=(\d+), loops=(\d+), stops=(\d+), frames=(\d+), nonzero=(\d+), peak=([\d.]+), dropped=(\d+)", log)
        assert match, log
        starts, loops, stops, frames, nonzero, peak, dropped = map(float, match.groups())
        assert starts > 100 and loops > 0 and loops == stops and nonzero > 0 and dropped == 0
        pcm = array.array("f")
        pcm.frombytes(output.with_suffix(".sfx.f32").read_bytes())
        assert len(pcm) == 48000 * 30 * 2 and .01 < max(map(abs, pcm)) <= 1
        report = {"chart": manifest["id"], "audio_source": manifest.get("sourceEditedMedia", manifest.get("sourceRestoredMedia", {})),
                  "filler_seconds": manifest["fillerSec"],
                  "music_clock": clocks,
                  "music_completed": {"frames": int(complete[1]), "sample_seconds": float(complete[2])},
                  "result": saved, "sound": {"starts": starts, "loops": loops, "stops": stops,
                  "mixed_frames": frames, "nonzero_samples": nonzero, "peak_before_clip": peak, "dropped": dropped},
                  "capture": str(output.with_suffix(".sfx.f32")), "image": str(output.with_suffix(".png")),
                  "window": "desktop" if args.desktop else "isolated labwc Wayland / hardware Vulkan",
                  "scope": "Full library music with original SE in SDL dummy output; SFX capture covers the first 30 seconds. No physical speaker or Unity raster claim."}
        output.with_suffix(".json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
        print(json.dumps(report, ensure_ascii=False))


if __name__ == "__main__":
    main()
