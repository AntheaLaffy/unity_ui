#ifndef DSE_SCORE_H
#define DSE_SCORE_H
#include "asmui_canvas.h"
#include <stddef.h>
#include <stdint.h>
#define DSE_MAX_NOTES 2048
#define DSE_MAX_TRACKS 8
#define DSE_MAX_PARTS 32
#define DSE_MAX_CURVES 262144
#define DSE_MAX_WAVES 64
#define DSE_WAVE_PEAKS 8192
#define DSE_MAX_PHONEMES 64
#define DSE_TICK_LIMIT 10000000
#define DSE_PPQ 480
typedef struct DseTrack {
    char name[64];
    uint32_t color, mute, solo;
    float gain, pan;
    uint32_t reserved[3];
} DseTrack;
typedef struct DsePart {
    char name[64];
    int32_t track, pos, len;
    uint32_t reserved[5];
} DsePart;
typedef struct DseNote {
    uint32_t id;
    int32_t pos, len, key, part;
    uint32_t selected;
    char lyric[64], phonemes[64];
    float velocity;
    uint32_t timing_slot;
} DseNote;
typedef struct DseTiming {
    uint32_t count;
    /* One zero is the automatic remainder; positive durations are score ticks. */
    int32_t ticks[DSE_MAX_PHONEMES];
    uint64_t signature;
} DseTiming;
typedef struct DseCurve {
    int32_t part, tick;
    float value;
    uint32_t kind;
} DseCurve;
typedef struct DseWave {
    char name[256], path[4096];
    int32_t track, pos, skip, trim, fade_in, fade_out;
    double duration_ms;
    float gain;
    uint32_t reserved;
} DseWave;
typedef struct DseWavePeaks {
    float low[DSE_WAVE_PEAKS], high[DSE_WAVE_PEAKS];
} DseWavePeaks;
typedef struct DseScore {
    uint32_t count, next_id, track_count, part_count;
    uint64_t revision;
    float bpm;
    uint32_t numerator, denominator, curve_count, curve_sorted, wave_count, reserved[4];
    DseTrack tracks[DSE_MAX_TRACKS];
    DsePart parts[DSE_MAX_PARTS];
    DseNote notes[DSE_MAX_NOTES];
    DseCurve curves[DSE_MAX_CURVES];
    DseWave waves[DSE_MAX_WAVES];
    DseTiming timings[DSE_MAX_NOTES];
} DseScore;
typedef struct DseView {
    float width, height, x, y, w, h, beat_width, row_height;
    int32_t scroll_tick, top_key, active_part, tool, grid;
    uint32_t reserved[3];
} DseView;
typedef struct DseRect {
    float x, y, w, h;
} DseRect;
_Static_assert(sizeof(DseNote) == 160, "ASM note ABI");
_Static_assert(offsetof(DseNote, timing_slot) == 156, "ASM timing slot ABI");
_Static_assert(sizeof(DseTrack) == 96 && sizeof(DsePart) == 96, "ASM track/part ABI");
_Static_assert(offsetof(DseScore, notes) == 3904 && offsetof(DseScore, curves) == 331584,
               "ASM score ABI");
_Static_assert(sizeof(DseView) == 64, "ASM viewport ABI");
_Static_assert(offsetof(DseScore, curve_sorted) == 40, "ASM curve ordering ABI");
_Static_assert(offsetof(DseScore, wave_count) == 44, "ASM audio count ABI");
int dse_add_note(DseScore*, int part, int pos, int len, int key);
int dse_delete_selected(DseScore*);
void dse_select(DseScore*, int index, int additive);
int dse_transform_selected(DseScore*, int delta_pos, int delta_key, int delta_len);
int dse_quantize(int tick, int grid);
void dse_note_rect(const DseNote*, const DseView*, DseRect*);
int dse_grid_coord(const DseView*, float x, float y, int* tick, int* key);
int dse_hit_note(const DseScore*, const DseView*, float x, float y);
float dse_curve_value(const DseScore*, int part, int kind, float tick, float fallback);
void dse_fill_f0(const DseScore*, int part, float*, uint32_t frames, uint32_t rate, uint32_t hop);
void dse_draw_notes(const DseScore*, const DseView*, AsmUiCanvas*);
/* Platform leaf used by the ASM scene traversal. */
void dse_paint_note(AsmUiCanvas*, const DseNote*, const DseRect*);
void dse_score_init(DseScore*, int demo);
int dse_score_load(DseScore*, const char*, char*, size_t);
int dse_score_save(const DseScore*, const char*, char*, size_t);
struct DseEngineConfig;
int dse_score_save_config(const DseScore*, const char*, const struct DseEngineConfig*, char*,
                          size_t);
int dse_score_validate(const DseScore*, char*, size_t);
int dse_curve_set(DseScore*, int part, int kind, int tick, float value);
void dse_curve_sort(DseScore*);
const DseTiming* dse_timing_get(const DseScore*, const DseNote*);
int dse_timing_valid(const DseTiming*, int len);
int dse_timing_set(DseScore*, unsigned index, const DseTiming*);
#endif
