#include "asmui_sound.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    AsmUiSoundMixer m={0};
    float original[]={.6f,-.6f,.2f,-.2f}, out[12];
    AsmUiAppSound clip={original,2,0};
    assert(asmui_sound_load(&m,&clip,1));
    memset(original,0,sizeof(original)); /* prove the bank does not borrow app memory */
    AsmUiAppSoundCommand one={.clip=0,.gain=1};
    asmui_sound_command(&m,&one);asmui_sound_command(&m,&one);
    asmui_sound_mix(&m,out,3);
    assert(out[0]==1 && out[1]==-1 && fabsf(out[2]-.4f)<1e-6f && out[4]==0);
    AsmUiAppSoundCommand loop={.clip=0,.voice=71,.action=APP_SOUND_LOOP,.gain=.5f};
    asmui_sound_command(&m,&loop);asmui_sound_mix(&m,out,5);
    for (int i=0;i<5;i++) assert(fabsf(out[2*i]-(i%2?.1f:.3f))<1e-6f);
    asmui_sound_command(&m,&(AsmUiAppSoundCommand){.voice=71,.action=APP_SOUND_STOP});
    asmui_sound_mix(&m,out,3);for(int i=0;i<6;i++) assert(out[i]==0);
    assert(m.started==3 && m.looped==1 && m.stopped==1 && !m.dropped);
    asmui_sound_command(&m,&(AsmUiAppSoundCommand){.clip=7,.gain=1});
    assert(m.started==3);
    asmui_sound_close(&m);
    original[0]=NAN;assert(!asmui_sound_load(&m,&clip,1));
    assert(!m.count && !m.clips[0].pcm);
    puts("Sound mixer: owned PCM, overlapping voices, gain, clipping, loop wrap, stop, invalid bank OK");
}
