#!/usr/bin/env python3
"""Exercise the real SDL/Vulkan host with isolated OpenSekai song data."""
import argparse
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--validation-root", type=Path)
    parser.add_argument("--editor", type=Path, default=ROOT / "build/editor")
    parser.add_argument("--artifact-prefix", default="opensekai-native-smoke")
    args = parser.parse_args()
    env = os.environ.copy()
    if args.validation_root:
        prefix = args.validation_root.resolve()
        layer = prefix / "share/vulkan/explicit_layer.d"
        assert (layer / "VkLayer_khronos_validation.json").is_file(), layer
        env["VK_INSTANCE_LAYERS"] = "VK_LAYER_KHRONOS_validation"
        env["VK_LAYER_PATH"] = str(layer)
        env["LD_LIBRARY_PATH"] = str(prefix / "lib") + (":" + env["LD_LIBRARY_PATH"] if env.get("LD_LIBRARY_PATH") else "")
        env["VK_LOADER_DEBUG"] = "layer"
    build = ROOT / "build"
    if not re.fullmatch(r"[A-Za-z0-9_-]+", args.artifact_prefix):
        parser.error("--artifact-prefix must be a simple file name")
    image = build / f"{args.artifact_prefix}.png"
    log_file = build / f"{args.artifact_prefix}.log"
    with tempfile.TemporaryDirectory(prefix="opensekai-window-", dir=build) as temporary:
        command = [str(args.editor.resolve()), "--project", str(ROOT / "application/OpenSekai-ASMR"),
                   "--data-dir", temporary, "--app-only", "--app-smoke", "--frames", "110",
                   "--size", "1920", "1080", "--output", str(image)]
        result = subprocess.run(command, cwd=ROOT, env=env, text=True, capture_output=True, timeout=120)
        log = result.stdout + result.stderr
        log_file.write_text(log)
        assert result.returncode == 0, f"Native host failed ({result.returncode}): {log_file}"
        assert "frames=110" in log, log_file
        assert not re.search(r"Validation Error|VUID-", log), f"Vulkan validation failure: {log_file}"
        assert not re.search(r"ERROR: AddressSanitizer|runtime error:", log), f"Native memory/UB failure: {log_file}"
        if args.validation_root:
            assert re.search(r'Insert(?:ed|ing)? (?:instance |device )?layer "VK_LAYER_KHRONOS_validation"', log), "Requested validation layer did not load"
        manifests = list(Path(temporary).glob("*/manifest.json"))
        assert len(manifests) == 1, manifests
        manifest = json.loads(manifests[0].read_text())
        assert manifest["title"] == "ASM 原生中文输入", manifest["title"]
        score = json.loads((manifests[0].parent / manifest["scoreFileName"]).read_text())
        assert len(score["NoteList"]) == 3, score["NoteList"]
        note = next(n for n in score["NoteList"] if n["category"] == 0)
        assert (note["ticks"], note["laneStart"], note["laneEnd"], note["noteBaseType"]) == (480, 1, 3, 1), note
        long_notes = sorted((n for n in score["NoteList"] if n["category"] == 1), key=lambda n: n["ticks"])
        assert len(long_notes) == 2, long_notes
        assert [(n["ticks"], n["laneStart"], n["laneEnd"], n["noteBaseType"]) for n in long_notes] == [(480,5,7,2),(720,5,7,1)], long_notes
        assert long_notes[0]["nextConnectionId"] == long_notes[1]["id"]
        assert long_notes[1]["previousConnectionId"] == long_notes[0]["id"]
        assert "Application image:" in log, "Maker did not load original sprite atlases"
        assert not re.search(r"missing=[1-9]\d*", log), "Application has missing glyphs"
        header = image.read_bytes()[:24]
        assert header[:8] == b"\x89PNG\r\n\x1a\n", image
        dimensions = struct.unpack(">II", header[16:24])
        assert dimensions == (1280, 800), f"Window resize did not reach the framebuffer: {dimensions}"
        report = {"frames": 110, "committed_title": manifest["title"], "final_framebuffer": dimensions,
                  "editor": str(args.editor.resolve()),
                  "framebuffers": re.findall(r"Application framebuffer: (\d+x\d+)", log),
                  "vulkan_validation": bool(args.validation_root), "image": str(image),
                  "saved_note": note, "original_atlas_loaded": True,
                  "saved_long_notes": long_notes,
                  "scope": "Native SDL/Vulkan Chinese input, manager-to-maker navigation, single/long note placement, undo/redo/save, modal and resize; not a Unity raster comparison."}
    (build / f"{args.artifact_prefix}.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    print(json.dumps(report, ensure_ascii=False))


if __name__ == "__main__":
    main()
