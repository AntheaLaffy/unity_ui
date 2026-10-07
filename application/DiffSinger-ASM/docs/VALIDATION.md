# Validation record

## 2026-10-05 — audio clips and real project accompaniment

The audio integration passed strict C11 compilation with `-Wall -Wextra -Werror`, ASM/C document and event tests, and ASan/UBSan with leak detection. Numerical tests check stereo accumulation and overlapping fade ramps, native-frame crop, tick placement, mono duplication, WAV/FLAC decoding, 8/16/22.05/32/44.1/48/88.2/96/192/384 kHz input paths, 44100 Hz output timing and sinusoid samples, clip peaks, native save/reopen, cancellation, mute and invalid/overflowing PCM rejection. Interaction tests cover clip fields, invalid crop rejection, gain/pan, drag movement, delete, undo/redo and 64 coincident waveforms without exceeding scene capacity.

| Check | Result and scope |
| --- | --- |
| `make diffsinger-audio-check` | Passed. All 16 distinct actual WAV/FLAC references under the user's Music projects were imported and decoded as middle excerpts. Original SHA-256 hashes are unchanged. A known stereo fixture was mixed with a real native DiffSinger voice and compared sample by sample, including placement and pan. Solo bypasses inactive voice models; mute and source/hardlink overwrite rejection preserve the original files. |
| `make diffsinger-audio-window-check` | Passed on X11 / Intel(R) Graphics (RPL-P). Opened `fly.ustx`, prepared its audible full-length backing tracks, displayed waveforms, played using SDL's consumed-byte clock, edited placement/fade/gain/pan with undo/redo, resized to 1024×800, prepared the revised audio, exported cached PCM and saved a native document retaining the exact source snapshot. The revised cache has 9812351 stereo frames at 44100 Hz. |
| `make diffsinger-ustx-check` | Passed all 25 source files again. Every voice note and imported audio position/path/duration/crop/fade field matches; native save/reopen preserves them and the original USTX text. |
| `make diffsinger-check`, `make diffsinger-sanitize` | Passed current musical, audio and interaction fixtures, including sample-rate extremes and overflowing mixed PCM rejection. Assembly instructions remain outside sanitizer instrumentation. |
| `make diffsinger-window-check`, `make diffsinger-ustx-window-check`, `make diffsinger-visual-check`, `make diffsinger-render-check` | Passed existing edit/IME/save/reopen/resize/cancel/native-render/audio-clock, real USTX, 2048-note/font-bank and native tensor/PCM regressions. |
| `make editor-check sound-check test` | Passed shared editor, sound, ASM render-core and retained-scene regressions. |

The desktop swapchain fix was exercised by captures before and after native surface/window changes. A submitted suboptimal frame now completes its readback, and a stale swapchain is rebuilt even when SDL reports the same extent. Pending captures survive an acquire that submitted no frame. The audio screenshots were visually inspected, including the 1024×800 layout and higher-resolution clip peaks.

Current reports: [audio and source protection](evidence/audio-report.json), [audio window](evidence/audio-window-report.json), [USTX corpus](evidence/ustx-report.json). Captures: [backing tracks opened](evidence/audio-opened.png), [edited minimum-size window](evidence/audio-edited.png), [prepared backing waveform](evidence/audio-playing.png).

These checks verify numerical mixing and editor workflows. They use SDL's dummy audio device for playback timing; physical speaker output and perceptual singing quality still need separate acceptance. Variable tempo, full Japanese phonemization/sustain, learned duration/variance/pitch and incremental singing preview remain open in [the complete-product checklist](DEVELOPMENT.md).

## 2026-10-04 — initial editor and USTX baseline

Validated on the current local Linux x86-64 checkout with Intel(R) Graphics (RPL-P), SDL3 3.4.14 and the existing Yousa v1.56 packed model generation `1421cb4e305d47e09fd9df6728f7f4dd`. No model download, substitute synthesizer or software-renderer fallback was used.

The DiffSinger editor checks below were rerun after renaming the application to `DiffSinger-ASM`. The directory, `diffsinger-asm` executable, CLI help, window/UI title, build targets and newly saved `application` metadata use the new identity. Saved and reopened documents retain their content and engine settings. Runtime environment variables use `DIFFSINGER_EDITOR_`; build/run arguments use `DIFFSINGER_ARGS`. The retained screenshots and reports were regenerated with the renamed executable.

| Check | Result and scope |
| --- | --- |
| `make diffsinger-check CFLAGS='-O2 -Wall -Wextra -Werror -std=c11'` | Passed. ASM editing/quantization/group bounds, geometry/NaN hits, independently calculated F0, endpoint interpolation, transactional TLP, clipping rejection, UTF-8 editing, history, saved path, unsaved guards, real speaker-file discovery and dense/max-size scene construction. |
| `make diffsinger-sanitize` | Passed with AddressSanitizer, UBSan and leak detection. Covers C interaction/model orchestration/file/canvas code and its calls into assembly; assembly instructions are not instrumented by ASan. |
| `make diffsinger-window-check` | Passed on real X11/Intel Vulkan: move, transpose, IME preedit/commit, undo/redo/delete, Ctrl+S saved file equality, reopen/settings persistence, track undo and resize; cancellation, subsequent native synthesis, queued-audio playback clock and cached WAV export. |
| `make diffsinger-render-check` | Passed using current acoustic/vocoder binaries. Chinese lyric -> actual token/duration files -> ASM F0 -> mel -> native PCM -> stereo PCM16 WAV. Also rejects overlaps, unknown phonemes, all-muted output and invalid CLI numeric arguments; failed synthesis preserves existing WAV. |
| `make diffsinger-visual-check` | Passed on real Vulkan, including 2048 overlapping display notes and 72 distinct new CJK input glyphs. Reached and passed the 64-page font-bank recycle branch. |
| `make diffsinger-demo` | Passed; exact Vulkan readback at 1440 × 940, visually inspected. |
| `make editor-check sound-check test` | Passed during the initial framework integration: existing Unity-style editor, font/localization/input, SDL sound and ASM render-core regressions. The application rename does not modify those shared framework sources. |
| `make -C application/DiffSinger-ASM all` | Passed application-local build entry. |
| `make diffsinger-ustx-check` | Passed all 25 USTX 0.7/0.9 files under `/home/fuurin/Music`: note position/duration/key/lyric comparison, source text retention, native save/reopen, unchanged source SHA-256, malformed-input rejection and USTX write protection. Includes 7 tracks / 3 voice parts / 1384 notes. |
| `make diffsinger-ustx-window-check` | Passed real X11/Intel Vulkan with `fly.ustx`: pitch display, note transpose with matching pitch-curve movement, undo/redo, IME, resize and native save retaining the unchanged source text. |

The import extension also passed strict compilation, sorted-versus-unsorted ASM curve lookup checks, an independently calculated pitch-unit/easing fixture, transactional source-snapshot tests and the existing window/render checks. The sanitizer target now includes the USTX parser and native project/source save-reopen path. USTX evidence and current limits are described in [USTX import](USTX_IMPORT.md).

The short native render fixture produced 65 acoustic frames, 33280 stereo sample frames at 44100 Hz. Its F0 gap is zero and A4 is 440 Hz. Centered left/right channels are equal and PCM is nonzero. This verifies contracts and a functioning native synthesis path; pronunciation, naturalness and block-boundary quality require separate evaluation on real songs.

The window fixture rendered 44032 sample frames and advanced the playback clock while using SDL's dummy audio device. This tests actual audio-stream consumption without a physical output device. Audible playback through the user's speakers and interaction with a specific OS IME candidate popup are not certified by that automation; IME events and their visible preedit/commit path are covered.

Retained evidence:

- [Default GPU screenshot](editor.png)
- [IME preedit](evidence/ime.png), [resized selected note](evidence/resized.png), [native PCM playback view](evidence/playing.png)
- [Window report](evidence/window-report.json), [native render report](evidence/render-report.json), [dense/font stress report](evidence/visual-report.json)
- [USTX import report](evidence/ustx-report.json), [USTX window report](evidence/ustx-window-report.json), [imported song](evidence/ustx-opened.png), [edited song](evidence/ustx-edited.png)

Native tensor/log directories in the JSON reports are temporary local diagnostic paths, not distributed assets. Test outputs and executables remain under `build/`; `docs/evidence` retains the small reports/screenshots for review.

The source link was checked with `readlink -f application/diffsinger`, resolving to `/home/fuurin/asm/diffsinger`. This work adds the editor and framework services without editing that linked engine checkout or reviving its removed consumer ABI.

No throughput, latency, champion-runtime, continuous acoustic streaming or finished voice-quality claim is made. Full-part rendering, fixed phoneme-duration allocation and the documented TLP feature limits remain visible development boundaries.
