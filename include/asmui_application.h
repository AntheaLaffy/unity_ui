#ifndef ASMUI_APPLICATION_H
#define ASMUI_APPLICATION_H
#include "asmui.h"

/* Versioned, renderer-independent application ABI. Coordinates are top-left,
   reference-canvas pixels. Strings and frame arrays live until the next call.
   Calls occur on the editor thread; ownership never crosses the ABI boundary. */
#define ASMUI_APPLICATION_ABI 5u
enum { APP_RECT, APP_TEXT, APP_SPRITE, APP_QUAD };
enum { APP_POINTER_DOWN = 1, APP_POINTER_UP, APP_POINTER_MOVE, APP_WHEEL,
       APP_KEY, APP_TEXT_INPUT, APP_COMPOSITION, APP_PLAY_STATE, APP_FILE_SELECTED, APP_FILE_ERROR,
       APP_NAVIGATE, APP_KEY_UP, APP_AUDIO_CLOCK, APP_FOCUS_LOST, APP_TICK, APP_RESOURCES_READY };
typedef struct AsmUiAppDraw {
    uint32_t kind, rgba, font, align;
    float rect[4], clip[4], uv[4];
    float font_size, rotation, min_font_size;
    uint32_t reserved; /* bit 0: text autosize; bit 1: original selected-note brightness; bit 2: additive sprite/quad;
                         bit 3: mix atlas RGB, preserving base alpha (font_size=mix, min_font_size=UV Y offset);
                         bit 4: planar projective quad (perspective-correct texture interpolation) */
    const char *text;
    /* APP_QUAD uses text as texture path and these canvas-space vertices.
       Order is 0,1,2,3 with triangles (0,1,2), (0,2,3); UV corners follow
       (u0,v1), (u1,v1), (u1,v0), (u0,v0). rect remains the bounding box. */
    float xy[8];
} AsmUiAppDraw;
typedef struct AsmUiAppSound {
    const float *pcm; /* stereo float32, 48000 Hz; copied by host per audio_epoch */
    uint32_t frames, reserved;
} AsmUiAppSound;
enum { APP_SOUND_ONESHOT, APP_SOUND_LOOP, APP_SOUND_STOP };
typedef struct AsmUiAppSoundCommand {
    uint32_t clip, voice, action; /* loops have a nonzero voice ID; stop uses that ID */
    float gain;
} AsmUiAppSoundCommand;
typedef struct AsmUiAppFrame {
    const AsmUiAppDraw *draws;
    uint32_t count, wants_text;
    float width, height, input_rect[4];
    /* Borrowed stereo float32 PCM at 48000 Hz. Host copies once per epoch.
       state: 0 stop, 1 play, 2 pause. APP_AUDIO_CLOCK x is seconds consumed,
       key: 0 playing, 1 end, 2 output error. No ownership crosses this ABI. */
    const float *audio_pcm;
    uint32_t audio_frames, audio_epoch, audio_state;
    uint32_t reserved; /* bit 0: preparing (host replies RESOURCES_READY); bit 1: completed live result */
    const AsmUiAppSound *sounds;
    uint32_t sound_count, sound_state; /* 0 stop, 1 play, 2 pause; independent of music */
    const AsmUiAppSoundCommand *sound_commands;
    uint32_t sound_command_count, sound_sequence; /* consume each sequence once per audio_epoch */
} AsmUiAppFrame;
typedef struct AsmUiAppEvent {
    uint32_t kind, key;
    float x, y, delta;
    uint32_t modifiers;
    const char *text;
} AsmUiAppEvent;
typedef struct AsmUiApplicationV5 {
    uint32_t abi, size;
    const char *name;
    void *(*create)(const char *project, const char *data);
    void (*destroy)(void *app);
    const char *(*font_path)(void *app, uint32_t font);
    const char *(*font_samples)(void *app);
    int (*frame)(void *app, float dt, uint32_t locale, AsmUiAppFrame *frame);
    int (*event)(void *app, const AsmUiAppEvent *event);
    /* Consumes one native file-picker request: ZIP/audio/jacket/chart/video/save
       ZIP = 1..6. Delivery uses APP_FILE_SELECTED with the same key. */
    uint32_t (*take_request)(void *app);
} AsmUiApplicationV5;
typedef const AsmUiApplicationV5 *(*AsmUiGetApplication)(uint32_t abi);

_Static_assert(sizeof(AsmUiAppDraw) == 120, "Application draw ABI");
_Static_assert(sizeof(AsmUiAppEvent) == 32, "Application event ABI");
_Static_assert(sizeof(AsmUiAppFrame) == 96, "Application frame ABI");
_Static_assert(sizeof(AsmUiAppSound) == 16, "Application sound ABI");
_Static_assert(sizeof(AsmUiAppSoundCommand) == 16, "Application sound command ABI");

typedef struct AsmUiAppHost {
    void *library, *instance;
    const AsmUiApplicationV5 *api;
    AsmUiAppFrame frame;
    float viewport[4], canvas[4], scale;
    uint32_t locale;
    float dt;
    uint32_t (*resolve_image)(void *user, const char *path);
    void *image_user;
} AsmUiAppHost;
int asmui_app_open(AsmUiAppHost *app, const char *project, const char *data);
void asmui_app_close(AsmUiAppHost *app);
uint32_t asmui_app_build(AsmUiAppHost *app, AsmUiNode *nodes, uint32_t start,
                         uint32_t capacity, int32_t parent, const float viewport[4]);
int asmui_app_event(AsmUiAppHost *app, AsmUiAppEvent *event);
void asmui_app_patch_quads(const AsmUiAppHost *app, AsmUiContext *ctx);
#endif
