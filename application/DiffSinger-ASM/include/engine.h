#ifndef DSE_ENGINE_H
#define DSE_ENGINE_H
#include "editor.h"
#include <stdatomic.h>
#define DSE_PATH 4096
typedef struct DseEngineConfig {
    char engine[DSE_PATH], bundle[DSE_PATH], dictionary[DSE_PATH], speaker[64], name[64];
    char bank[DSE_PATH], language_code[16];
    unsigned steps, workers, language;
    float depth;
} DseEngineConfig;
typedef struct DseRender {
    DseScore score;
    DseEngineConfig config;
    char output[DSE_PATH], work[DSE_PATH], message[512];
    float* pcm;
    size_t samples;
    unsigned sample_rate;
    atomic_int cancel, done, progress;
    int success, audio_only;
    DseWavePeaks peaks[DSE_MAX_WAVES];
} DseRender;
void dse_engine_default(DseEngineConfig*, const char* engine_root);
int dse_engine_check(const DseEngineConfig*, char*, size_t);
int dse_engine_load_project(DseEngineConfig*, const char*, char*, size_t);
int dse_engine_resolve(DseEngineConfig*, const char* folder, char*, size_t);
int dse_engine_cycle_speaker(DseEngineConfig*, char*, size_t);
int dse_engine_language(DseEngineConfig*, const char* code, char*, size_t);
int dse_engine_cycle_language(DseEngineConfig*, char*, size_t);
int dse_render_run(DseRender*);
int dse_render_wav(const char*, const float*, size_t, unsigned, char*, size_t);
#endif
