#ifndef DSE_APP_H
#define DSE_APP_H
#include "asmui_desktop.h"
#include "editor.h"
#include "engine.h"
#include "frontend.h"
#include "project.h"
#define DSE_HISTORY 32
#define DSE_BUTTONS 256
#define DSE_NODES 32768
#define DSE_DRAWS 131072
enum {
    B_NEW = 1,
    B_OPEN,
    B_SAVE,
    B_RENDER,
    B_EXPORT,
    B_PLAY,
    B_STOP,
    B_UNDO,
    B_REDO,
    B_TRACK,
    B_SINGER,
    B_SPEAKER,
    B_SELECT,
    B_DRAW,
    B_PITCH,
    B_ERASE,
    B_SNAP,
    B_ZOOM_OUT,
    B_ZOOM_IN,
    B_LYRIC,
    B_PHONEMES,
    B_BPM,
    B_CLEAR_CURVE,
    B_LANE_PITCH,
    B_LANE_BREATH,
    B_LANE_TENSION,
    B_LANE_VOICE,
    B_LANE_GENDER,
    B_LANE_VELOCITY,
    B_MODAL_CANCEL,
    B_MODAL_DISCARD,
    B_MODAL_SAVE,
    B_AUDIO_IMPORT,
    B_AUDIO_PREVIEW,
    B_AUDIO_RELINK,
    B_AUDIO_DELETE,
    B_AUDIO_POS,
    B_AUDIO_SKIP,
    B_AUDIO_TRIM,
    B_AUDIO_FADE_IN,
    B_AUDIO_FADE_OUT,
    B_AUDIO_GAIN,
    B_TRACK_GAIN,
    B_TRACK_PAN,
    B_LANGUAGE,
    B_TIMING,
    B_TIMING_RESET,
    B_TRACK_BASE = 100,
    B_MUTE_BASE = 120,
    B_SOLO_BASE = 140,
    B_PART_BASE = 200,
    B_WAVE_BASE = 300
};
typedef struct DseButton {
    int id;
    DseRect rect;
} DseButton;
typedef struct DseDialog DseDialog;
typedef struct DseFrontendLoad DseFrontendLoad;
typedef struct DseApp {
    DseProjectInfo project_info;
    DseScore *score, *history[DSE_HISTORY], *redo[DSE_HISTORY];
    unsigned history_count, history_index;
    DseView view;
    AsmUiDesktop* desktop;
    AsmUiNode* nodes;
    char* strings;
    AsmUiCanvas canvas;
    DseButton buttons[DSE_BUTTONS];
    unsigned button_count;
    DseEngineConfig config;
    DseFrontend* frontend;
    DseFrontendLoad* front_load;
    SDL_Thread* front_worker;
    DseEngineConfig front_config;
    char front_message[512];
    int front_valid, inspector_scroll;
    DseNotePlan front_plan;
    uint64_t front_plan_revision;
    unsigned front_plan_index;
    int front_plan_valid, front_plan_success;
    DseRender* render;
    SDL_Thread* worker;
    float* pcm;
    size_t samples;
    uint64_t pcm_revision;
    int pcm_audio_only, active_wave, active_track, arrange_tick;
    float arrange_beat;
    DseWavePeaks peaks[DSE_MAX_WAVES];
    uint64_t peaks_revision;
    SDL_AudioStream* audio;
    size_t play_start, play_queued;
    int playing, auto_play;
    float play_tick;
    int modified, quit, modal, after_save, focus, select_all, lane, track_scroll;
    int drag, drag_tick, drag_key, drag_started, drag_index, box_additive;
    float drag_x, drag_y, pointer_x, pointer_y;
    DseRect selection_box;
    char edit[1024], composition[256], message[512], path[DSE_PATH], output[DSE_PATH];
    DseNote clipboard[DSE_MAX_NOTES];
    DseTiming clipboard_timing[DSE_MAX_NOTES];
    unsigned clipboard_count;
    DseDialog* dialog;
    int render_requested;
    unsigned captures;
} DseApp;
void dse_app_layout(DseApp*);
void dse_app_scene(DseApp*);
void dse_app_event(DseApp*, const SDL_Event*);
void dse_app_tick(DseApp*);
void dse_app_history(DseApp*);
void dse_app_changed(DseApp*);
void dse_app_action(DseApp*, int);
void dse_app_render(DseApp*, int);
void dse_app_audio_preview(DseApp*, int auto_play);
int dse_app_import_audio(DseApp*, const char*, int relink);
void dse_app_cleanup(DseApp*);
int dse_app_note_plan(DseApp*, unsigned index, DseNotePlan*);
#endif
