#ifndef EDITOR_APPLICATION_SOUND_H
#define EDITOR_APPLICATION_SOUND_H
#include "asmui_sound.h"
#include <SDL3/SDL.h>
typedef struct ApplicationSound {
    SDL_AudioStream *stream;
    AsmUiSoundMixer mixer;
    uint32_t epoch, sequence, state;
    SDL_AtomicInt error;
    float *capture;
    uint32_t capture_samples, capture_capacity;
    int profile;
    uint64_t callback_count, callback_ns, callback_max_ns;
} ApplicationSound;
void application_sound_sync(ApplicationSound *s, const AsmUiAppFrame *frame);
void application_sound_close(ApplicationSound *s);
#endif
