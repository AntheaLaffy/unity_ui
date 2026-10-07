#include "../demo/application_sound.h"
#include <assert.h>
#include <stdio.h>
static uint64_t mixed(ApplicationSound *s) {
    assert(SDL_LockAudioStream(s->stream));
    uint64_t count=s->mixer.mixed_frames;
    assert(SDL_UnlockAudioStream(s->stream));return count;
}
int main(void) {
    assert(SDL_SetHint(SDL_HINT_AUDIO_DRIVER,"dummy"));
    ApplicationSound sound={0};float samples[]={.2f,-.2f,.4f,-.4f};
    AsmUiAppSound clip={samples,2,0};
    AsmUiAppSoundCommand command={.voice=1,.action=APP_SOUND_LOOP,.gain=1};
    AsmUiAppFrame frame={.sounds=&clip,.sound_count=1,.sound_state=1,.audio_epoch=1,
        .sound_commands=&command,.sound_command_count=1,.sound_sequence=1};
    application_sound_sync(&sound,&frame);assert(sound.stream);
    SDL_Delay(80);assert(mixed(&sound)>0);
    application_sound_sync(&sound,&frame); /* replaying a frame must not restart voices */
    frame.sound_state=2;application_sound_sync(&sound,&frame);
    uint64_t paused=mixed(&sound);SDL_Delay(60);assert(mixed(&sound)==paused);
    assert(sound.mixer.started==1 && sound.mixer.nonzero_samples>0);
    frame.sound_state=1;frame.sound_sequence=2;command.action=APP_SOUND_STOP;
    application_sound_sync(&sound,&frame);SDL_Delay(40);
    frame.sound_state=2;application_sound_sync(&sound,&frame);
    assert(sound.mixer.stopped==1 && sound.mixer.started==1);
    frame.audio_epoch=2;frame.sound_sequence=3;command.action=APP_SOUND_LOOP;
    application_sound_sync(&sound,&frame);assert(sound.mixer.started==1 && sound.epoch==2);
    frame.sound_state=0;application_sound_sync(&sound,&frame);assert(!sound.stream && !sound.mixer.count);
    application_sound_close(&sound);SDL_Quit();
    puts("SDL sound stream: callback output, command de-duplication, pause, resume, epoch replacement, teardown OK");
}
