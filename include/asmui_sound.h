#ifndef ASMUI_SOUND_H
#define ASMUI_SOUND_H
#include "asmui_application.h"
#define ASMUI_SOUND_CLIPS 128u
#define ASMUI_SOUND_VOICES 256u
typedef struct AsmUiSoundClip { float *pcm; uint32_t frames; } AsmUiSoundClip;
typedef struct AsmUiSoundVoice {
    uint32_t clip, id, position, active, loop;
    float gain;
} AsmUiSoundVoice;
typedef struct AsmUiSoundMixer {
    AsmUiSoundClip clips[ASMUI_SOUND_CLIPS];
    AsmUiSoundVoice voices[ASMUI_SOUND_VOICES];
    uint32_t count, dropped;
    uint64_t started, looped, stopped, mixed_frames, nonzero_samples;
    float peak;
} AsmUiSoundMixer;
/* Owner serializes calls. PCM is copied; the application may release its bank
   after load. Mixing and commands allocate no memory and perform no file I/O. */
int asmui_sound_load(AsmUiSoundMixer *m, const AsmUiAppSound *clips, uint32_t count);
void asmui_sound_close(AsmUiSoundMixer *m);
void asmui_sound_command(AsmUiSoundMixer *m, const AsmUiAppSoundCommand *command);
void asmui_sound_mix(AsmUiSoundMixer *m, float *stereo, uint32_t frames);
#endif
