#ifndef DSE_PROJECT_H
#define DSE_PROJECT_H
#include "engine.h"

/* Source data is separate from the fixed ASM score and its undo snapshots. */
typedef struct DseProjectInfo {
    char* ustx;
    char source[DSE_PATH], singer[512], phonemizer[256];
    unsigned wave_parts, unsupported_curves, expressions, overrides, mixed_singers;
} DseProjectInfo;

int dse_path_is_ustx(const char*);
void dse_project_info_clear(DseProjectInfo*);
void dse_project_notice(const DseProjectInfo*, char*, size_t);
int dse_project_load(DseScore*, DseProjectInfo*, const char*, char*, size_t);
int dse_project_save(const DseScore*, const DseProjectInfo*, const char*,
                     const DseEngineConfig*, char*, size_t);
int dse_ustx_load(DseScore*, DseProjectInfo*, const char*, char*, size_t);
void dse_engine_import(DseEngineConfig*, const DseProjectInfo*);
#endif
