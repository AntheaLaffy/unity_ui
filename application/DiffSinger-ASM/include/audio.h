#ifndef DSE_AUDIO_H
#define DSE_AUDIO_H
#include "engine.h"
#define DSE_AUDIO_RATE 44100
#define DSE_AUDIO_LIMIT (DSE_AUDIO_RATE * 60 * 30)
int dse_audio_path(char*, size_t, const char* project, const char* asset);
int dse_audio_probe(DseWave*, const char* path, char*, size_t);
int dse_audio_destination(const DseScore*, const char* path, char*, size_t);
double dse_wave_ticks(const DseWave*, float bpm);
int dse_audio_mix(DseRender*, size_t* capacity);
/* SysV x86-64 kernel: accumulate interleaved stereo with track/clip gains and
 * independent fades. index is relative to the retained source interval. */
void dse_mix_stereo(float* dst, const float* src, size_t frames, size_t index, size_t total,
                    size_t fade_in, size_t fade_out, float left, float right);
#endif
