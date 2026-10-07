#include "application_sound.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void SDLCALL sound_callback(void *user, SDL_AudioStream *stream, int additional, int total) {
    (void)total;
    ApplicationSound *s=user;
    uint64_t started=s->profile?SDL_GetTicksNS():0;
    float pcm[1024*2];
    /* SDL holds this stream's mutex throughout the callback. Produce only the
       requested input frames, so hits never wait behind a render-fed buffer. */
    while (additional>0) {
        uint32_t frames=((uint32_t)additional+7)/8;
        if (frames>1024) frames=1024;
        asmui_sound_mix(&s->mixer,pcm,frames);
        if (!SDL_PutAudioStreamData(stream,pcm,(int)(frames*8))) {
            SDL_SetAtomicInt(&s->error,1);return;
        }
        uint32_t samples=frames*2;
        if (samples>s->capture_capacity-s->capture_samples) samples=s->capture_capacity-s->capture_samples;
        if (samples) {memcpy(s->capture+s->capture_samples,pcm,samples*sizeof(float));s->capture_samples+=samples;}
        additional-=(int)(frames*8);
    }
    if (s->profile) {
        uint64_t elapsed=SDL_GetTicksNS()-started;
        s->callback_count++;s->callback_ns+=elapsed;
        if (elapsed>s->callback_max_ns) s->callback_max_ns=elapsed;
    }
}
void application_sound_close(ApplicationSound *s) {
    if (s->stream) {SDL_DestroyAudioStream(s->stream);s->stream=NULL;}
    if (s->mixer.count) {
        printf("Application sound: epoch=%u, starts=%llu, loops=%llu, stops=%llu, frames=%llu, nonzero=%llu, peak=%.6f, dropped=%u\n",
            s->epoch,(unsigned long long)s->mixer.started,(unsigned long long)s->mixer.looped,
            (unsigned long long)s->mixer.stopped,(unsigned long long)s->mixer.mixed_frames,
            (unsigned long long)s->mixer.nonzero_samples,s->mixer.peak,s->mixer.dropped);
    }
    if (s->profile) printf("Application sound callback: calls=%llu total_ms=%.3f max_ms=%.3f\n",
        (unsigned long long)s->callback_count,s->callback_ns/1e6,s->callback_max_ns/1e6);
    if (s->capture) {
        const char *path=SDL_getenv("ASMUI_CAPTURE_SFX");
        FILE *out=path&&*path?fopen(path,"wb"):NULL;
        if (out) {fwrite(s->capture,sizeof(float),s->capture_samples,out);fclose(out);}
        free(s->capture);s->capture=NULL;
    }
    s->capture_samples=s->capture_capacity=0;
    s->callback_count=s->callback_ns=s->callback_max_ns=0;
    asmui_sound_close(&s->mixer);s->state=0;
}
void application_sound_sync(ApplicationSound *s, const AsmUiAppFrame *f) {
    if (s->stream && (s->epoch!=f->audio_epoch || !f->sound_state)) application_sound_close(s);
    if (!f->sound_state || !f->sounds || !f->sound_count) return;
    if (!s->stream && s->epoch!=f->audio_epoch) {
        s->epoch=f->audio_epoch;s->sequence=UINT32_MAX;s->state=2;
        s->profile=SDL_getenv("ASMUI_PROFILE_AUDIO")!=NULL;
        SDL_AudioSpec spec={.format=SDL_AUDIO_F32,.channels=2,.freq=48000};
        if (!asmui_sound_load(&s->mixer,f->sounds,f->sound_count)) {
            SDL_SetError("Invalid sound bank or allocation failure");goto fail;
        }
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO) ||
            !(s->stream=SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&spec,sound_callback,s))) goto fail;
        const char *capture=SDL_getenv("ASMUI_CAPTURE_SFX");
        if (capture && *capture) {
            s->capture=malloc(48000u*30u*2u*sizeof(float));
            if (s->capture) s->capture_capacity=48000u*30u*2u;
        }
        printf("Application sound bank: %u clips, epoch=%u\n",f->sound_count,s->epoch);
    }
    if (!s->stream) return;
    if (!SDL_LockAudioStream(s->stream)) goto fail;
    if (s->sequence!=f->sound_sequence) {
        if (f->sound_commands) for (uint32_t i=0;i<f->sound_command_count;i++) asmui_sound_command(&s->mixer,&f->sound_commands[i]);
        s->sequence=f->sound_sequence;
    }
    SDL_UnlockAudioStream(s->stream);
    if (s->state!=f->sound_state) {
        bool ok=f->sound_state==1?SDL_ResumeAudioStreamDevice(s->stream):SDL_PauseAudioStreamDevice(s->stream);
        if (!ok) goto fail;
        if (f->sound_state==2 && !SDL_ClearAudioStream(s->stream)) goto fail;
        s->state=f->sound_state;
    }
    return;
fail:
    fprintf(stderr,"Application sound error: %s\n",SDL_GetError());
    application_sound_close(s);SDL_SetAtomicInt(&s->error,1);
}
