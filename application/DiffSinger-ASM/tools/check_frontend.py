#!/usr/bin/env python3
"""Actual multilingual bank -> frontend -> native acoustic/vocoder tensors/PCM."""
from pathlib import Path
import array
import hashlib
import json
import math
import re
import subprocess
import wave
from audio_quality import check_voice

APP = Path(__file__).resolve().parents[1]
BIN = APP / "build/diffsinger-asm"
OUT = APP / "build/frontend-check"
OUT.mkdir(parents=True, exist_ok=True)
BANK = Path("/home/fuurin/.local/share/OpenUtau/Singers/DongFangZhiZi_Nectar_DiffSinger_CE_26.08.31")
generation = json.loads((BANK / "dsasm/current.json").read_text())["generation"]
acoustic = BANK / "dsasm/generations" / generation / "acoustic"
vocab = json.loads((acoustic / "phonemes.json").read_text())
language_ids = json.loads((acoustic / "languages.json").read_text())


def invoke(fixture, *args, ok=True):
    p = subprocess.run([str(BIN), "--project", str(fixture), "--singer", str(BANK),
                        "--language", "ja", *map(str, args)], text=True, capture_output=True, timeout=180)
    (OUT / "last-output.txt").write_text(p.stdout + p.stderr)
    assert (p.returncode == 0) == ok, p.stdout + p.stderr
    return p


def save(score, name):
    path = OUT / name
    path.write_text(json.dumps(score, ensure_ascii=False, indent=2))
    return path


def phone_signature(symbols):
    value = 14695981039346656037
    for symbol in symbols:
        for c in symbol.encode() + b"\0":
            value = ((value ^ c) * 1099511628211) & ((1 << 64) - 1)
    return f"{value:016x}"


score = json.loads((APP / "examples/first-song.tlp").read_text())
part = score["tracks"][0]["parts"][0]
part["pitch"] = []
part["notes"] = [dict(pos=120, dur=480, pitch=69, lyric="か", pronunciation="", properties={}),
                  dict(pos=600, dur=240, pitch=72, lyric="+", pronunciation="", properties={}),
                  dict(pos=840, dur=960, pitch=69, lyric="世界", pronunciation="", properties={})]
part["endOffset"] = 1920
fixture = save(score, "japanese.dse")
preflight = json.loads(invoke(fixture, "--check-lyrics").stdout)
phones = preflight["parts"][0]["phones"]
expected = ["SP", "ja/k", "ja/a", "ja/s", "ja/e", "ja/k", "ja/a", "ja/i"]
assert [p["symbol"] for p in phones] == expected
assert preflight["languageId"] == language_ids["ja"] == 2
assert [p["language"] for p in phones] == [0] + [2] * 7

renders = []
for mode in ["automatic", "manual"]:
    if mode == "manual":
        part["notes"][0]["phonemeTiming"] = dict(signature=phone_signature(["ja/k", "ja/a"]), ticks=[77, 0])
        fixture = save(score, "manual.dse")
    output = OUT / f"{mode}.wav"
    result = invoke(fixture, "--render", output)
    job = Path(re.search(r"任务目录: (.+)", result.stdout)[1])
    symbols = (job / "phonemes.txt").read_text().split()
    tokens = [int(x) for x in (job / "tokens.txt").read_text().split()]
    durations = [int(x) for x in (job / "durations.txt").read_text().split()]
    languages = [int(x) for x in (job / "languages.txt").read_text().split()]
    f0 = array.array("f", (job / "f0.f32").read_bytes())
    assert symbols == expected and tokens == [vocab[x] for x in expected]
    assert len(tokens) == len(languages) == len(durations) and sum(durations) == len(f0)
    assert languages == [0] + [2] * 7
    assert durations[1] == (7 if mode == "manual" else 3)
    assert len(f0) == round(1800 * 60 * 44100 / (120 * 480 * 512))
    assert abs(f0[30] - 440) < .01 and abs(f0[60] - 523.251) < .02, "Tie note lost its own pitch"
    with wave.open(str(output)) as w:
        assert (w.getnchannels(), w.getframerate(), w.getsampwidth()) == (2, 44100, 2)
        assert w.getnframes() == len(f0) * 512
        pcm = array.array("h", w.readframes(w.getnframes()))
        assert pcm[::2] == pcm[1::2]
        rms = math.sqrt(sum((v / 32768) ** 2 for v in pcm) / len(pcm))
        quality = check_voice(pcm)
    renders.append(dict(mode=mode, job=str(job), durations=durations, languages=languages,
                        frames=len(f0), rms=rms, waveformQuality=quality,
                        sha256=hashlib.sha256(output.read_bytes()).hexdigest()))
assert renders[0]["sha256"] != renders[1]["sha256"], "Manual timing did not reach native inference"

# Each syllable in a multi-note word owns its independent articulation timing.
part["notes"] = [dict(pos=120, dur=480, pitch=69, lyric="世界", pronunciation="", properties={}),
                  dict(pos=600, dur=480, pitch=72, lyric="+", pronunciation="", properties={}),
                  dict(pos=1080, dur=480, pitch=74, lyric="+", pronunciation="", properties={}),
                  dict(pos=1560, dur=240, pitch=76, lyric="+~", pronunciation="", properties={})]
part["notes"][0]["phonemeTiming"] = dict(signature=phone_signature(["ja/s", "ja/e"]), ticks=[58, 0])
part["notes"][1]["phonemeTiming"] = dict(signature=phone_signature(["ja/k", "ja/a"]), ticks=[96, 0])
part["notes"][2]["phonemeTiming"] = dict(signature=phone_signature(["ja/i"]), ticks=[0])
fixture = save(score, "syllable-timing.dse")
syllable_phones = json.loads(invoke(fixture, "--check-lyrics").stdout)["parts"][0]["phones"]
assert [p["symbol"] for p in syllable_phones] == ["SP", "ja/s", "ja/e", "ja/k", "ja/a", "ja/i"]
assert [p["frames"] for p in syllable_phones] == [11, 5, 38, 8, 35, 64]
result = invoke(fixture, "--render", OUT / "syllable.wav")
syllable_job = Path(re.search(r"任务目录: (.+)", result.stdout)[1])
assert [int(x) for x in (syllable_job / "durations.txt").read_text().split()] == [11, 5, 38, 8, 35, 64]
with wave.open(str(OUT / "syllable.wav")) as w:
    assert w.getnframes() == 161 * 512
    assert any(array.array("h", w.readframes(w.getnframes())))

# Real CMU entries and coda relocation; no reattacked consonant on the tie.
part["notes"] = [dict(pos=0, dur=480, pitch=69, lyric="en/world", pronunciation="", properties={}),
                  dict(pos=480, dur=480, pitch=72, lyric="+", pronunciation="", properties={})]
fixture = save(score, "english-coda.dse")
english = json.loads(invoke(fixture, "--check-lyrics").stdout)["parts"][0]["phones"]
assert len(english) == 4 and [x["language"] for x in english] == [language_ids["en"]] * 4
assert english[1]["frames"] > 60 and english[-1]["frames"] == 3
result = invoke(fixture, "--render", OUT / "english.wav")
english_job = Path(re.search(r"任务目录: (.+)", result.stdout)[1])
assert [int(x) for x in (english_job / "languages.txt").read_text().split()] == [1] * 4
assert [int(x) for x in (english_job / "durations.txt").read_text().split()] == [x["frames"] for x in english]
with wave.open(str(OUT / "english.wav")) as w:
    assert w.getnframes() == sum(x["frames"] for x in english) * 512
    assert any(array.array("h", w.readframes(w.getnframes())))

part["notes"][1]["pos"] += 1
fixture = save(score, "bad-tie.dse")
protected = OUT / "protected.wav"
protected.write_bytes(b"Existing export must survive failed preflight")
invoke(fixture, "--render", protected, ok=False)
assert protected.read_bytes() == b"Existing export must survive failed preflight"

# Real source songs stay byte-for-byte intact, including retained expressions.
songs = []
for song in [Path("/home/fuurin/Music/萤琢/fly.ustx"),
             Path("/home/fuurin/Music/雪结晶/snow.ustx")]:
    if not song.exists():
        matches = list(Path("/home/fuurin/Music").rglob(song.name))
        assert len(matches) == 1
        song = matches[0]
    before = song.read_bytes()
    p = subprocess.run([str(BIN), "--project", str(song), "--singer", str(BANK),
                        "--language", "ja", "--check-lyrics"], capture_output=True, text=True, timeout=180)
    assert song.read_bytes() == before
    report = json.loads(p.stdout)
    # A diagnosed source edit issue is recorded rather than treating valid PCM
    # on the small fixture as proof that every imported song is renderable.
    songs.append(dict(source=str(song), sha256=hashlib.sha256(before).hexdigest(),
                      errors=report["errors"],
                      diagnostics=[part["error"] for part in report["parts"] if "error" in part],
                      parts=len(report["parts"])))

report = dict(status="passed", bank=str(BANK), generation=generation, symbols=expected,
              renders=renders, englishJob=str(english_job), syllableJob=str(syllable_job),
              syllableDurations=[p["frames"] for p in syllable_phones], songs=songs, sourceUnchanged=True,
              checks=["kana/kanji", "per-phone language metadata", "tie pitch", "manual timing in real tensors/PCM",
                      "independent syllable timing inside + groups", "English coda relocation",
                      "failed preflight protects existing WAV", "actual song preflight"],
              qualityScope="native tensor and PCM contract; perceptual pronunciation/alignment quality is not certified")
(OUT / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2))
print("Native frontend: Japanese/English languages, kana/kanji, ties, edited timing, source songs and PCM OK")
