#include "asmui_sound.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

void asmui_sound_close(AsmUiSoundMixer *m) {
    for (uint32_t i=0;i<ASMUI_SOUND_CLIPS;i++) free(m->clips[i].pcm);
    memset(m,0,sizeof(*m));
}
int asmui_sound_load(AsmUiSoundMixer *m, const AsmUiAppSound *clips, uint32_t count) {
    asmui_sound_close(m);
    if (!clips || !count || count>ASMUI_SOUND_CLIPS) return 0;
    for (uint32_t i=0;i<count;i++) {
        uint32_t frames=clips[i].frames;
        if (!clips[i].pcm || !frames || frames>48000u*30u) goto fail;
        float *pcm=malloc((size_t)frames*2*sizeof(float));
        if (!pcm) goto fail;
        m->clips[i]=(AsmUiSoundClip){pcm,frames};
        memcpy(pcm,clips[i].pcm,(size_t)frames*2*sizeof(float));
        for (uint32_t j=0;j<frames*2;j++) if (!isfinite(pcm[j])) goto fail;
    }
    m->count=count;return 1;
fail:
    asmui_sound_close(m);return 0;
}
void asmui_sound_command(AsmUiSoundMixer *m, const AsmUiAppSoundCommand *c) {
    if (c->action==APP_SOUND_STOP) {
        if (!c->voice) return;
        for (uint32_t i=0;i<ASMUI_SOUND_VOICES;i++) {
            AsmUiSoundVoice *v=&m->voices[i];
            if (v->active && v->id==c->voice) {v->active=0;m->stopped++;}
        }
        return;
    }
    if (c->action>APP_SOUND_LOOP || c->clip>=m->count || !isfinite(c->gain) ||
        (c->action==APP_SOUND_LOOP && !c->voice)) return;
    AsmUiSoundVoice *slot=NULL;
    for (uint32_t i=0;i<ASMUI_SOUND_VOICES;i++) {
        AsmUiSoundVoice *v=&m->voices[i];
        if (c->action==APP_SOUND_LOOP && v->active && v->id==c->voice) {slot=v;break;}
        if (!slot && !v->active) slot=v;
    }
    if (!slot) {m->dropped++;return;}
    *slot=(AsmUiSoundVoice){.clip=c->clip,.id=c->action==APP_SOUND_LOOP?c->voice:0,
        .active=1,.loop=c->action==APP_SOUND_LOOP,.gain=fmaxf(0.f,fminf(c->gain,1.f))};
    m->started++;m->looped+=slot->loop;
}
void asmui_sound_mix(AsmUiSoundMixer *m, float *stereo, uint32_t frames) {
    memset(stereo,0,(size_t)frames*2*sizeof(float));
    for (uint32_t i=0;i<ASMUI_SOUND_VOICES;i++) {
        AsmUiSoundVoice *v=&m->voices[i];
        if (!v->active) continue;
        const AsmUiSoundClip *clip=&m->clips[v->clip];
        for (uint32_t j=0;j<frames;j++) {
            stereo[2*j]+=clip->pcm[2*v->position]*v->gain;
            stereo[2*j+1]+=clip->pcm[2*v->position+1]*v->gain;
            if (++v->position==clip->frames) {
                v->position=0;
                if (!v->loop) {v->active=0;break;}
            }
        }
    }
    for (uint32_t i=0;i<frames*2;i++) {
        float sample=stereo[i];
        m->peak=fmaxf(m->peak,fabsf(sample));
        stereo[i]=fmaxf(-1.f,fminf(1.f,sample));
        m->nonzero_samples+=fabsf(sample)>0.000001f;
    }
    m->mixed_frames+=frames;
}
