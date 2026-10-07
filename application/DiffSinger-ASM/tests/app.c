#define _POSIX_C_SOURCE 200809L
#include "app.h"
#include "audio.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Document interaction tests do not need a video device. Window and GPU
 * integration is independently exercised by check_window.py. */
SDL_Window* asmui_desktop_window(AsmUiDesktop* desktop) {
    (void)desktop;
    return NULL;
}
static void key(DseApp* a, SDL_Keycode code, SDL_Keymod mod) {
    SDL_Event event;
    SDL_zero(event);
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.key = code;
    event.key.mod = mod;
    event.key.down = true;
    event.key.repeat = false;
    dse_app_event(a, &event);
}
static void pointer(DseApp* a, unsigned type, float x, float y) {
    SDL_Event event;
    SDL_zero(event);
    event.type = type;
    if (type == SDL_EVENT_MOUSE_MOTION) {
        event.motion.x = x;
        event.motion.y = y;
    } else {
        event.button.button = SDL_BUTTON_LEFT;
        event.button.clicks = 1;
        event.button.x = x;
        event.button.y = y;
    }
    dse_app_event(a, &event);
}
static void text(DseApp* a, const char* value, int preedit) {
    SDL_Event event;
    SDL_zero(event);
    if (preedit) {
        event.type = SDL_EVENT_TEXT_EDITING;
        event.edit.text = value;
    } else {
        event.type = SDL_EVENT_TEXT_INPUT;
        event.text.text = value;
    }
    dse_app_event(a, &event);
}
int main(void) {
    assert(SDL_Init(SDL_INIT_EVENTS));
    DseApp* a = calloc(1, sizeof(*a));
    assert(a);
    a->score = malloc(sizeof(*a->score));
    a->nodes = calloc(DSE_NODES, sizeof(*a->nodes));
    a->strings = malloc(1024 * 1024);
    assert(a->score && a->nodes && a->strings);
    dse_score_init(a->score, 1);
    a->view = (DseView){.width = 1440,
                        .height = 940,
                        .beat_width = 64,
                        .row_height = 22,
                        .top_key = 72,
                        .grid = 120,
                        .tool = 1};
    dse_app_layout(a);
    dse_app_scene(a);
    DseRect r;
    dse_note_rect(&a->score->notes[0], &a->view, &r);
    pointer(a, SDL_EVENT_MOUSE_BUTTON_DOWN, r.x + 10, r.y + r.h / 2);
    pointer(a, SDL_EVENT_MOUSE_BUTTON_UP, r.x + 10, r.y + r.h / 2);
    assert(a->score->notes[0].selected);
    key(a, SDLK_UP, 0);
    assert(a->score->notes[0].key == 61 && a->modified);
    key(a, SDLK_Z, SDL_KMOD_CTRL);
    assert(a->score->notes[0].key == 60);
    key(a, SDLK_Y, SDL_KMOD_CTRL);
    assert(a->score->notes[0].key == 61);

    dse_app_action(a, B_LYRIC);
    text(a, "虚拟歌姬", 1);
    assert(!strcmp(a->composition, "虚拟歌姬") && !strcmp(a->score->notes[0].lyric, "让"));
    text(a, "虚拟歌姬", 0);
    key(a, SDLK_BACKSPACE, 0);
    assert(!strcmp(a->edit, "虚拟歌"));
    text(a, "姬", 0);
    key(a, SDLK_RETURN, 0);
    assert(!a->focus && !strcmp(a->score->notes[0].lyric, "虚拟歌姬"));
    dse_app_action(a, B_LYRIC);
    memset(a->edit, 'a', 70);
    a->edit[70] = 0;
    key(a, SDLK_RETURN, 0);
    assert(a->focus && !strcmp(a->score->notes[0].lyric, "虚拟歌姬"));
    key(a, SDLK_ESCAPE, 0);
    key(a, SDLK_RIGHT, SDL_KMOD_SHIFT);
    assert(a->score->notes[0].len == 540);

    key(a, SDLK_C, SDL_KMOD_CTRL);
    a->play_tick = 6720;
    key(a, SDLK_V, SDL_KMOD_CTRL);
    assert(a->score->count == 13 && a->score->notes[12].pos == 6720 &&
           !strcmp(a->score->notes[12].lyric, "虚拟歌姬"));
    key(a, SDLK_DELETE, 0);
    assert(a->score->count == 12);
    key(a, SDLK_Z, SDL_KMOD_CTRL);
    assert(a->score->count == 13);
    key(a, SDLK_Y, SDL_KMOD_CTRL);
    assert(a->score->count == 12);

    char path[] = "/tmp/diffsinger-app-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    close(fd);
    strcpy(a->path, path);
    strcpy(a->config.speaker, "Normal");
    a->config.steps = 4;
    a->config.workers = 8;
    a->config.depth = .6f;
    dse_app_action(a, B_SAVE);
    assert(!a->modified && !strcmp(a->path, path));
    dse_select(a->score, 0, 0);
    key(a, SDLK_UP, 0);
    dse_app_action(a, B_SAVE);
    DseScore* loaded = calloc(1, sizeof(*loaded));
    assert(loaded);
    assert(dse_score_load(loaded, path, a->message, sizeof(a->message)) &&
           loaded->notes[0].key == a->score->notes[0].key && !a->modified);
    unlink(path);
    free(loaded);

    char singer[] = "/tmp/diffsinger-speakers-XXXXXX", acoustic[128], embedding[160];
    assert(mkdtemp(singer));
    snprintf(acoustic, sizeof(acoustic), "%s/acoustic", singer);
    assert(mkdir(acoustic, 0700) == 0);
    const char* voices[] = {"Alpha", "Beta"};
    for (unsigned i = 0; i < 2; i++) {
        snprintf(embedding, sizeof(embedding), "%s/%s.emb", acoustic, voices[i]);
        FILE* file = fopen(embedding, "wb");
        assert(file);
        fclose(file);
    }
    strcpy(a->config.bundle, singer);
    a->modified = 0;
    dse_app_action(a, B_SPEAKER);
    assert(!strcmp(a->config.speaker, "Alpha") && a->modified);
    dse_app_action(a, B_SPEAKER);
    assert(!strcmp(a->config.speaker, "Beta"));
    dse_app_action(a, B_SPEAKER);
    assert(!strcmp(a->config.speaker, "Alpha"));
    for (unsigned i = 0; i < 2; i++) {
        snprintf(embedding, sizeof(embedding), "%s/%s.emb", acoustic, voices[i]);
        unlink(embedding);
    }
    rmdir(acoustic);
    rmdir(singer);

    dse_app_action(a, B_TRACK);
    assert(a->score->track_count == 2 && a->view.active_part == 1);
    key(a, SDLK_Z, SDL_KMOD_CTRL);
    assert(a->score->track_count == 1 && a->view.active_part == 0);
    dse_app_action(a, B_NEW);
    assert(a->modal == 2 && a->score->count == 12);
    dse_app_action(a, B_MODAL_CANCEL);
    assert(!a->modal && a->score->count == 12);
    dse_app_action(a, B_NEW);
    dse_app_action(a, B_MODAL_DISCARD);
    assert(!a->modal && !a->modified && !a->score->count && !a->history_count);

    dse_add_note(a->score, 0, 0, 1920, 60);
    dse_select(a->score, 0, 0);
    for (int i = 0; i < 40; i++)
        key(a, SDLK_UP, 0);
    assert(a->score->notes[0].key == 100 && a->history_count == DSE_HISTORY);
    for (int i = 0; i < 40; i++)
        key(a, SDLK_Z, SDL_KMOD_CTRL);
    assert(a->score->notes[0].key == 68 && a->history_index == DSE_HISTORY);
    for (int i = 0; i < 40; i++)
        key(a, SDLK_Y, SDL_KMOD_CTRL);
    assert(a->score->notes[0].key == 100);
    dse_score_init(a->score, 0);
    for (int i = 0; i < DSE_MAX_NOTES; i++) {
        assert(dse_add_note(a->score, 0, 0, 1920, 60) == i);
        memset(a->score->notes[i].lyric, 'a', 63);
        a->score->notes[i].lyric[63] = 0;
    }
    dse_app_scene(a);
    assert(!a->canvas.overflow && a->canvas.count < DSE_NODES && a->canvas.used < 1024 * 1024);
    a->view.width = 7680;
    a->view.height = 4320;
    a->view.grid = 60;
    a->view.beat_width = 24;
    dse_app_layout(a);
    dse_app_scene(a);
    assert(!a->canvas.overflow && a->canvas.count < DSE_NODES);
    dse_score_init(a->score, 0);
    assert(dse_add_note(a->score, 0, 480, 480, 69) == 0);
    dse_select(a->score, 0, 0);
    a->project_info.ustx = strdup("source metadata");
    assert(a->project_info.ustx);
    assert(dse_curve_set(a->score, 0, 0, 500, 69.25f));
    key(a, SDLK_UP, 0);
    assert(a->score->notes[0].key == 70 && dse_curve_value(a->score, 0, 0, 500, 0) == 70.25f);
    key(a, SDLK_Z, SDL_KMOD_CTRL);
    assert(a->score->notes[0].key == 69 && dse_curve_value(a->score, 0, 0, 500, 0) == 69.25f);
    key(a, SDLK_RIGHT, 0);
    assert(a->score->notes[0].pos == 540 && dse_curve_value(a->score, 0, 0, 560, 0) == 69.25f);
    key(a, SDLK_Z, SDL_KMOD_CTRL);
    assert(a->score->notes[0].pos == 480 && dse_curve_value(a->score, 0, 0, 500, 0) == 69.25f);
    DseTiming manual = {.count = 2, .ticks = {360, 0}, .signature = 42};
    assert(dse_timing_set(a->score, 0, &manual));
    key(a, SDLK_LEFT, SDL_KMOD_SHIFT);
    assert(a->score->notes[0].len == 420);
    key(a, SDLK_LEFT, SDL_KMOD_SHIFT);
    assert(a->score->notes[0].len == 420 &&
           dse_score_validate(a->score, a->message, sizeof(a->message)));
    key(a, SDLK_C, SDL_KMOD_CTRL);
    a->play_tick = 1920;
    key(a, SDLK_V, SDL_KMOD_CTRL);
    assert(a->score->count == 2 &&
           a->score->notes[1].timing_slot != a->score->notes[0].timing_slot);
    manual.ticks[0] = 200;
    assert(dse_timing_set(a->score, 1, &manual));
    assert(dse_timing_get(a->score, &a->score->notes[0])->ticks[0] == 360);
    dse_app_action(a, B_TIMING_RESET);
    assert(!a->score->notes[1].timing_slot);
    key(a, SDLK_Z, SDL_KMOD_CTRL);
    assert(dse_timing_get(a->score, &a->score->notes[1])->ticks[0] == 200);
    dse_app_action(a, B_LYRIC);
    text(a, "新", 0);
    key(a, SDLK_RETURN, 0);
    assert(!a->score->notes[1].timing_slot);
    key(a, SDLK_Z, SDL_KMOD_CTRL);
    assert(dse_timing_get(a->score, &a->score->notes[1])->ticks[0] == 200);
    dse_score_init(a->score, 0);
    for (unsigned i = 0; i < 3; i++) {
        assert(dse_add_note(a->score, 0, (int)i * 480, 480, 69) == (int)i);
        strcpy(a->score->notes[i].lyric, i ? "+" : "世界");
        a->score->notes[i].phonemes[0] = 0;
        assert(dse_timing_set(a->score, i, &manual));
    }
    dse_select(a->score, 1, 0);
    dse_app_action(a, B_LYRIC);
    text(a, "か", 0);
    key(a, SDLK_RETURN, 0);
    for (unsigned i = 0; i < 3; i++)
        assert(!a->score->notes[i].timing_slot);
    key(a, SDLK_Z, SDL_KMOD_CTRL);
    for (unsigned i = 0; i < 3; i++)
        assert(dse_timing_get(a->score, &a->score->notes[i])->ticks[0] == 200);
    dse_score_init(a->score, 0);
    a->score->wave_count = 1;
    a->score->waves[0] = (DseWave){.duration_ms = 4000};
    strcpy(a->score->waves[0].name, "测试伴奏.wav");
    strcpy(a->score->waves[0].path, "/tmp/missing-audio-edit-test.wav");
    a->active_wave = 1;
    a->active_track = 0;
    a->view.width = 1440;
    a->view.height = 940;
    a->view.grid = 120;
    dse_app_action(a, B_AUDIO_SKIP);
    text(a, "1.25", 0);
    key(a, SDLK_RETURN, 0);
    assert(a->score->waves[0].skip == 600);
    dse_app_action(a, B_AUDIO_TRIM);
    text(a, "99", 0);
    key(a, SDLK_RETURN, 0);
    assert(a->focus && a->score->waves[0].trim == 0);
    key(a, SDLK_ESCAPE, 0);
    dse_app_action(a, B_TRACK_PAN);
    text(a, "-50", 0);
    key(a, SDLK_RETURN, 0);
    assert(a->score->tracks[0].pan == -.5f);
    dse_app_scene(a);
    pointer(a, SDL_EVENT_MOUSE_BUTTON_DOWN, 230, 195);
    pointer(a, SDL_EVENT_MOUSE_MOTION, 272, 195);
    pointer(a, SDL_EVENT_MOUSE_BUTTON_UP, 272, 195);
    assert(a->score->waves[0].pos == 480);
    key(a, SDLK_Z, SDL_KMOD_CTRL);
    assert(a->score->waves[0].pos == 0);
    key(a, SDLK_Y, SDL_KMOD_CTRL);
    assert(a->score->waves[0].pos == 480);
    dse_app_action(a, B_AUDIO_DELETE);
    assert(!a->score->wave_count);
    key(a, SDLK_Z, SDL_KMOD_CTRL);
    assert(a->score->wave_count == 1 && a->score->waves[0].pos == 480);
    a->score->wave_count = DSE_MAX_WAVES;
    for (unsigned w = 1; w < DSE_MAX_WAVES; w++)
        a->score->waves[w] = a->score->waves[0];
    a->peaks_revision = 1;
    for (unsigned w = 0; w < DSE_MAX_WAVES; w++)
        for (unsigned bin = 0; bin < DSE_WAVE_PEAKS; bin++) {
            a->peaks[w].low[bin] = -.5f;
            a->peaks[w].high[bin] = .5f;
        }
    dse_app_scene(a);
    assert(!a->canvas.overflow && a->canvas.count < DSE_NODES && a->button_count < DSE_BUTTONS);
    dse_app_cleanup(a);
    free(a);
    SDL_Quit();
    puts("DiffSinger-ASM interaction: UTF-8, selection, editing, history, persistence, "
         "unsaved guards "
         "and dense scenes OK");
}
