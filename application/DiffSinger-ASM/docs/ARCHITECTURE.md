# Implementation boundaries

DiffSinger-ASM is a dedicated virtual singer editor under `application/DiffSinger-ASM`, built around the local DiffSinger engine's voicebanks, phoneme frontend, pitch controls and synthesis lifecycle. See [Product direction](PRODUCT.md). It uses the framework's existing assembly layout, draw-list, text expansion, vertex emission, and Vulkan pipeline.

| File | Responsibility |
| --- | --- |
| `src/score.S` / `src/abi.inc` | x86-64 SysV editing, quantization, validated group transforms, geometry, reverse hit testing, curve interpolation, F0 generation, visible-note traversal |
| `src/mix.S` / `src/audio.c` | ASM stereo gain/fade accumulation, chunked libsndfile decoding, libsamplerate sinc conversion, clip peaks, media paths and source protection |
| `include/editor.h` | Fixed-size musical document and verified ASM/C layout |
| `src/project.c` | Restricted TLP JSON reader, document validation, curve storage, transactional writer |
| `src/ustx.c` / `include/project.h` | Bounded libyaml USTX import, typed-field conversion, pitch baking, source snapshot and import notices |
| `src/app.c` | Undo history, SDL input/IME, asynchronous dialog lifetime, render worker, cache revisions, audio stream |
| `src/ui.c` | Native node composition for arrangement, piano roll, inspector, parameters and actual PCM waveform |
| `src/frontend.c` / `include/frontend.h` | Cached bank dictionary/symbol types/languages, ICU/MeCab readings, syllable/sustain groups, validated manual timing and the shared phoneme plan |
| `src/engine.c` | Packed model validation, native subprocess supervision, preflight, tensor files, vocoder overlap, track mixing and WAV |
| `src/main.c` | CLI, SDL event loop, pixel normalization, captures and diagnostic event scripts |
| Framework `backend/canvas.c` | Bounded caller-owned node/text arenas; top-left desktop coordinates translated to parent-local ASM nodes |
| Framework `backend/desktop.c` | Reusable SDL/Vulkan host, glyph pages, shader setup, frame submission and exact PNG readback |

The C layer handles the OS, serialization, shaping and orchestration. Musical editing and rendering primitives remain assembly. No handwritten C neural-network kernel or Python online runtime was added. The existing native DiffSinger acoustic/vocoder binaries own inference.

## Rendering jobs

The UI commits text before rendering, then gives the worker a full score/config snapshot. `score.revision` identifies the snapshot; editing invalidates the previous cache. A finished job is installed only when its revision still matches. Cancellation is atomic, sends TERM to the currently supervised native process, waits, then uses KILL if needed. Joining the worker precedes freeing its snapshot, message or PCM.

Native calls use `posix_spawn` with argv arrays, never a shell command. Each job has a private `mkdtemp` directory. It first validates every audible voice part, then produces phoneme symbols, tokens, durations, per-token languages, ASM-generated F0 and the five control arrays, calls `dsasm-acoustic infer --languages`, then invokes `dsasm-vocoder-m40 infer` on 384-frame blocks. Blocks advance by 320 frames, use 64-frame linear overlap and pad short final blocks with mel -12 / F0 zero. The result is trimmed to the actual acoustic frame count. This is full-part acoustic inference followed by blocked vocoding, not the engine's promoted streaming service.

Mute/solo/gain/pan are applied while mixing audible parts into stereo float PCM. The UI keeps a complete revision-tagged cache, sends it through an SDL F32 audio stream, and uses queued source-byte consumption to advance the playhead. RIFF PCM16 export clamps samples to the supported range and atomically replaces the destination only on success.

Audio clips use immutable source files and tick-based placement/crop/fades. The decoder seeks past the skipped native frames, streams bounded blocks through a two-channel sinc resampler when needed, and uses the ASM accumulator to apply both fades and channel gains. It checks cancellation between blocks. Metadata and 8192 peak buckets per clip survive independently of the PCM ownership; scene geometry reduces each visible clip to at most 128 peak columns. Audio-only jobs skip the voice frontend/model requirement and are visibly distinguished from full voice+audio jobs. Output paths are checked against referenced source media and inode aliases. Media paths in native saves are absolute and can be relinked.

The frontend deliberately labels its current deterministic duration behavior. There is no variance-model prediction, learned phoneme aligner or automatic pitch model. Future work must validate the supported DiffSinger model contracts and pronunciation, timing, pitch and listening results on real songs.

The UI loads a separate dictionary context on an SDL thread. Publication uses release/acquire atomics; joining precedes installing or freeing its result, including stale configurations and application shutdown. The note plan cache keys score revision, selected index and frontend configuration. Model jobs own independent contexts and pass their cancellation token to the dictionary input handler, entry construction and part planning. Cancellation can therefore stop frontend work before any neural subprocess starts. See [frontend behavior](FRONTEND.md).

Manual timing reuses the note's previously reserved 32-bit slot at byte 156 and appends a bounded timing pool after the existing score arrays. The 160-byte ASM note layout and existing offsets remain unchanged. Pool allocation counts actual references, so deleted entries are reusable and shared entries copy on write. Clipboard records own their timing values; history snapshots include the pool. Timings retain a symbol-order signature to detect pronunciation changes and are serialized in the native `phonemeTiming` extension.

Word planning separates each syllable's owning note from its vowel-extension notes. The note inspector gets the owner's symbol slice and signature; the render job joins every segment into the full word. Each owner validates its own timing against its own note length. Vowel holds add frames only to that segment's nucleus and retain any trailing coda. Lyric/pronunciation edits clear timing for the affected word before reassigning symbols, in the same history transaction. Editing or resetting a vowel hold targets its syllable owner.

## UI framework extensions

The new canvas/desktop APIs leave the original Unity-style editor host independent. Desktop font selection prefers installed LXGW WenKai while preserving the general font finder's previous fallback ordering. Required interface glyphs are validated. New lyric/IME glyphs get immutable texture pages; after 64 pages the current corpus is rebuilt following the completed frame fence and existing descriptor sets are updated. Missing user glyphs use replacement glyphs rather than aborting the document session.

The desktop host rebuilds stale/suboptimal Vulkan swapchains even when SDL's size has not changed. Captures of already submitted frames complete before a rebuild; callers keep pending captures when acquire submitted no frame. This fixes missing readbacks during native window/surface configuration changes.

The application allocates 32768 nodes, 131072 draws and a 1 MiB UTF-8 arena. Pitch line geometry is bounded by viewport width; note labels have short visual previews while the document retains full text. Tests exercise all 2048 notes and maximum supported window dimensions. Button hit areas intersect ancestor clips, so invisible arrangement controls cannot capture clicks.

Musical documents hold up to 262144 curve points to accommodate full USTX songs. Points are sorted by part/lane/tick for ASM lower-bound lookup; the previous scan remains available for caller-owned unsorted scores. Import baking appends points in bulk before sorting. Source snapshots are owned separately from ASM scores and undo history. Imported pitch moves/transposes with selected notes and participates in the same undo snapshots.

Input scripts are diagnostic tools. They queue ordinary SDL mouse/keyboard/text events into the same application handler. Layout-relative targets are resolved after pending native resize events and scene layout; event timing expands to accommodate startup/font stalls. `--duration`, state dumps and captures are explicit automated-run exit behavior.

`wait-frontend`, `wait-render` and `wait-idle` script gates poll actual asynchronous readiness or cancellation completion while the normal event/render loop continues. Their deadlines report failure; elapsed time never substitutes for a successful frontend or current PCM cache. This makes dictionary loading, cancellation and native inference part of the window acceptance rather than fixed startup-delay assumptions.

## Project compatibility

TLP is an interchange compatibility layer. Loads are staged into a separate score. Failed validation leaves the live score intact. Saves fsync a sibling temporary file before rename and identify the application as `DiffSinger-ASM`. Only constant-tempo MIDI voice parts and the documented curve/mixing subset are supported. Engine settings live in an `asmui` object; they are local paths, not a portable model package.

The DiffSinger source link points to the original checkout. Packed generation directories are read as model data only. No OpenUtau consumer runtime, removed ABI or offline exchange protocol was restored. Changes in that checkout remain governed by its own instructions.

USTX is read only. Native `.dse` files use the current JSON musical document with an `openutau` source snapshot object and native `audio` part records. Original source text, source path and import status survive native save/reopen; they are provenance data rather than an edited USTX export. Audio placement/crop/fade semantics are active; unmodeled expression semantics remain retained but inactive. See [USTX import](USTX_IMPORT.md).
