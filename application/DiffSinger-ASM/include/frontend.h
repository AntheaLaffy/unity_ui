#ifndef DSE_FRONTEND_H
#define DSE_FRONTEND_H
#include "engine.h"
typedef struct DseFrontend DseFrontend;
typedef struct DsePhone {
    char symbol[64];
    int32_t token, language, frames, vowel;
} DsePhone;
typedef struct DseNotePlan {
    DsePhone phones[DSE_MAX_PHONEMES];
    unsigned count, source_index, owner_index;
    int start, stop, base_stop, sustain, tied, progression;
    uint64_t signature;
} DseNotePlan;
typedef struct DsePartPlan {
    DsePhone* phones;
    unsigned count, frames;
} DsePartPlan;
DseFrontend* dse_frontend_open(const DseEngineConfig*, char*, size_t);
DseFrontend* dse_frontend_open_cancel(const DseEngineConfig*, const atomic_int*, char*, size_t);
void dse_frontend_close(DseFrontend*);
int dse_frontend_note(DseFrontend*, const DseScore*, unsigned index, DseNotePlan*, char*, size_t);
int dse_frontend_part(DseFrontend*, const DseScore*, unsigned part, DsePartPlan*, char*, size_t);
int dse_frontend_frame(int tick, float bpm);
void dse_part_plan_clear(DsePartPlan*);
/* Caller records history/revision together with the lyric or pronunciation edit. */
void dse_timing_clear_word(DseScore*, unsigned index);
int dse_timing_parse(const char*, const DseScore*, unsigned, const DseNotePlan*, DseTiming*, char*,
                     size_t);
void dse_timing_text(const DseScore*, unsigned, const DseNotePlan*, char*, size_t);
#endif
