#define _POSIX_C_SOURCE 200809L
#include "app.h"
#include "audio.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct DialogRequest {
    DseDialog* dialog;
    int kind;
} DialogRequest;
struct DseDialog {
    SDL_Mutex* mutex;
    SDL_AtomicInt refs, pending;
    uint32_t event;
    int active;
};
static void release_dialog(DseDialog* d) {
    if (SDL_AtomicDecRef(&d->refs)) {
        SDL_DestroyMutex(d->mutex);
        free(d);
    }
}
static void SDLCALL dialog_result(void* user, const char* const* paths, int filter) {
    (void)filter;
    DialogRequest* request = user;
    DseDialog* d = request->dialog;
    SDL_LockMutex(d->mutex);
    if (d->active) {
        SDL_Event event;
        SDL_zero(event);
        event.type = d->event;
        event.user.code = request->kind;
        event.user.data1 = SDL_strdup(paths && paths[0] ? paths[0] : "");
        if (!SDL_PushEvent(&event)) {
            SDL_free(event.user.data1);
            SDL_SetAtomicInt(&d->pending, 0);
        }
    }
    SDL_UnlockMutex(d->mutex);
    free(request);
    release_dialog(d);
}
static void dialog(DseApp* a, int kind) {
    if (!a->desktop || !asmui_desktop_window(a->desktop)) {
        snprintf(a->message, sizeof(a->message), "此操作需要桌面窗口");
        return;
    }
    if (!a->dialog) {
        a->dialog = calloc(1, sizeof(*a->dialog));
        if (!a->dialog)
            return;
        a->dialog->mutex = SDL_CreateMutex();
        a->dialog->event = SDL_RegisterEvents(1);
        if (!a->dialog->mutex || !a->dialog->event) {
            if (a->dialog->mutex)
                SDL_DestroyMutex(a->dialog->mutex);
            free(a->dialog);
            a->dialog = NULL;
            return;
        }
        a->dialog->active = 1;
        SDL_SetAtomicInt(&a->dialog->refs, 1);
    }
    if (SDL_GetAtomicInt(&a->dialog->pending))
        return;
    DialogRequest* r = malloc(sizeof(*r));
    if (!r)
        return;
    *r = (DialogRequest){a->dialog, kind};
    SDL_AtomicIncRef(&a->dialog->refs);
    SDL_SetAtomicInt(&a->dialog->pending, 1);
    SDL_Window* window = asmui_desktop_window(a->desktop);
    static const SDL_DialogFileFilter project = {"DiffSinger-ASM project", "dse;tlp"},
                                      inputs[] = {{"Singing projects", "dse;ustx;tlp"},
                                                  {"OpenUtau project", "ustx"}},
                                      wave = {"Wave audio", "wav"},
                                      media = {"Audio files", "wav;flac;ogg;aiff;mp3"};
    if (kind == B_SINGER)
        SDL_ShowOpenFolderDialog(dialog_result, r, window, NULL, false);
    else if (kind == B_OPEN)
        SDL_ShowOpenFileDialog(dialog_result, r, window, inputs, 2, NULL, false);
    else if (kind == B_AUDIO_IMPORT || kind == B_AUDIO_RELINK)
        SDL_ShowOpenFileDialog(dialog_result, r, window, &media, 1, NULL, false);
    else
        SDL_ShowSaveFileDialog(dialog_result, r, window, kind == B_EXPORT ? &wave : &project, 1,
                               kind == B_EXPORT
                                   ? (a->pcm_audio_only ? "accompaniment.wav" : "mix.wav")
                                   : "Untitled.dse");
}
static void pause_audio(DseApp* a) {
    if (a->audio) {
        SDL_PauseAudioDevice(SDL_GetAudioStreamDevice(a->audio));
        SDL_ClearAudioStream(a->audio);
    }
    a->playing = 0;
}
static void play_audio(DseApp* a) {
    if (a->playing) {
        pause_audio(a);
        return;
    }
    if (!a->pcm || a->pcm_revision != a->score->revision) {
        if (a->score->wave_count)
            dse_app_audio_preview(a, 1);
        else
            dse_app_render(a, 1);
        return;
    }
    if (!a->audio) {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            snprintf(a->message, sizeof(a->message), "音频设备不可用：%.400s", SDL_GetError());
            return;
        }
        const SDL_AudioSpec spec = {SDL_AUDIO_F32, 2, 44100};
        a->audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
        if (!a->audio) {
            snprintf(a->message, sizeof(a->message), "音频设备不可用：%.400s", SDL_GetError());
            return;
        }
    }
    size_t start = (size_t)(a->play_tick * 60.0 * 44100 / (a->score->bpm * 480));
    if (start >= a->samples) {
        start = 0;
        a->play_tick = 0;
    }
    SDL_ClearAudioStream(a->audio);
    a->play_start = start;
    a->play_queued = (a->samples - start) * 2 * sizeof(float);
    if (!SDL_PutAudioStreamData(a->audio, a->pcm + start * 2, (int)a->play_queued) ||
        !SDL_FlushAudioStream(a->audio) ||
        !SDL_ResumeAudioDevice(SDL_GetAudioStreamDevice(a->audio))) {
        snprintf(a->message, sizeof(a->message), "音频播放失败：%.400s", SDL_GetError());
        return;
    }
    a->playing = 1;
    snprintf(a->message, sizeof(a->message), "%s · 空格暂停",
             a->pcm_audio_only ? "伴奏试听" : "播放歌声与伴奏混音");
}
void dse_app_changed(DseApp* a) {
    a->modified = 1;
    if (a->playing)
        pause_audio(a);
}
static void stack_push(DseScore** stack, unsigned* count, const DseScore* s) {
    if (*count == DSE_HISTORY) {
        free(stack[0]);
        memmove(stack, stack + 1, (DSE_HISTORY - 1) * sizeof(*stack));
        (*count)--;
    }
    DseScore* copy = malloc(sizeof(*copy));
    if (!copy) {
        fprintf(stderr, "History allocation failed\n");
        exit(1);
    }
    *copy = *s;
    stack[(*count)++] = copy;
}
void dse_app_history(DseApp* a) {
    for (unsigned i = 0; i < a->history_index; i++)
        free(a->redo[i]);
    a->history_index = 0;
    stack_push(a->history, &a->history_count, a->score);
}
static void restore(DseApp* a, int redo) {
    DseScore **source = redo ? a->redo : a->history, **dest = redo ? a->history : a->redo;
    unsigned *count = redo ? &a->history_index : &a->history_count,
             *dest_count = redo ? &a->history_count : &a->history_index;
    if (!*count)
        return;
    stack_push(dest, dest_count, a->score);
    DseScore* old = source[--*count];
    uint64_t revision = a->score->revision + 1;
    *a->score = *old;
    free(old);
    a->score->revision = revision;
    if ((unsigned)a->view.active_part >= a->score->part_count)
        a->view.active_part = 0;
    if ((unsigned)a->active_wave > a->score->wave_count)
        a->active_wave = 0;
    if ((unsigned)a->active_track >= a->score->track_count)
        a->active_track = 0;
    a->peaks_revision = 0;
    dse_app_changed(a);
    snprintf(a->message, sizeof(a->message), redo ? "已重做" : "已撤销");
}
static void history_clear(DseApp* a) {
    for (unsigned i = 0; i < a->history_count; i++)
        free(a->history[i]);
    for (unsigned i = 0; i < a->history_index; i++)
        free(a->redo[i]);
    a->history_count = a->history_index = 0;
}
static DseNote* selected(DseApp* a) {
    for (unsigned i = 0; i < a->score->count; i++)
        if (a->score->notes[i].selected)
            return &a->score->notes[i];
    return NULL;
}
struct DseFrontendLoad {
    DseEngineConfig config;
    DseFrontend* result;
    atomic_int done;
    char message[512];
};
static int frontend_worker(void* user) {
    DseFrontendLoad* load = user;
    load->result = dse_frontend_open(&load->config, load->message, sizeof(load->message));
    atomic_store_explicit(&load->done, 1, memory_order_release);
    return 0;
}
static int front_equal(const DseEngineConfig* a, const DseEngineConfig* b) {
    return !strcmp(a->bundle, b->bundle) && !strcmp(a->dictionary, b->dictionary) &&
           !strcmp(a->language_code, b->language_code) && a->language == b->language;
}
int dse_app_note_plan(DseApp* a, unsigned index, DseNotePlan* p) {
    memset(p, 0, sizeof(*p));
    if (a->front_load && atomic_load_explicit(&a->front_load->done, memory_order_acquire)) {
        SDL_WaitThread(a->front_worker, NULL);
        a->front_worker = NULL;
        dse_frontend_close(a->frontend);
        a->frontend = a->front_load->result;
        a->front_config = a->front_load->config;
        a->front_valid = 1;
        a->front_plan_valid = 0;
        snprintf(a->front_message, sizeof(a->front_message), "%s", a->front_load->message);
        free(a->front_load);
        a->front_load = NULL;
    }
    if (!a->front_valid || !front_equal(&a->front_config, &a->config)) {
        if (!a->front_load) {
            a->front_load = calloc(1, sizeof(*a->front_load));
            if (!a->front_load) {
                snprintf(a->front_message, sizeof(a->front_message), "音素词典加载分配失败");
                return 0;
            }
            a->front_load->config = a->config;
            atomic_init(&a->front_load->done, 0);
            a->front_worker =
                SDL_CreateThread(frontend_worker, "phoneme-dictionary", a->front_load);
            if (!a->front_worker) {
                free(a->front_load);
                a->front_load = NULL;
                snprintf(a->front_message, sizeof(a->front_message), "无法启动音素词典加载线程");
                return 0;
            }
        }
        snprintf(a->front_message, sizeof(a->front_message), "正在加载声库音素词典…");
        return 0;
    }
    if (!a->frontend)
        return 0;
    if (a->front_plan_valid && a->front_plan_revision == a->score->revision &&
        a->front_plan_index == index) {
        *p = a->front_plan;
        return a->front_plan_success;
    }
    a->front_message[0] = 0;
    a->front_plan_success = dse_frontend_note(a->frontend, a->score, index, p, a->front_message,
                                              sizeof(a->front_message));
    a->front_plan = *p;
    a->front_plan_revision = a->score->revision;
    a->front_plan_index = index;
    a->front_plan_valid = 1;
    return a->front_plan_success;
}
static int commit(DseApp* a) {
    if (!a->focus)
        return 1;
    int id = a->focus;
    DseNote* note = selected(a);
    if (id == B_BPM) {
        char* end;
        float bpm = strtof(a->edit, &end);
        if (*end || !isfinite(bpm) || bpm < 20 || bpm > 400) {
            snprintf(a->message, sizeof(a->message), "BPM 必须在 20 到 400 之间");
            return 0;
        }
        if (a->score->bpm != bpm) {
            for (unsigned w = 0; w < a->score->wave_count; w++)
                if (dse_wave_ticks(&a->score->waves[w], bpm) <= 0 ||
                    a->score->waves[w].pos + dse_wave_ticks(&a->score->waves[w], bpm) >
                        DSE_TICK_LIMIT) {
                    snprintf(a->message, sizeof(a->message), "新速度会使音频裁剪或位置超出范围");
                    return 0;
                }
            dse_app_history(a);
            a->score->bpm = bpm;
            a->score->revision++;
            dse_app_changed(a);
        }
    } else if ((id >= B_AUDIO_POS && id <= B_AUDIO_GAIN) || id == B_TRACK_GAIN ||
               id == B_TRACK_PAN) {
        char* end;
        double value = strtod(a->edit, &end);
        if (!*a->edit || *end || !isfinite(value)) {
            snprintf(a->message, sizeof(a->message), "请输入有效数值");
            return 0;
        }
        if (id == B_TRACK_GAIN || id == B_TRACK_PAN) {
            double limit = id == B_TRACK_GAIN ? 60 : 100;
            if (fabs(value) > limit) {
                snprintf(a->message, sizeof(a->message), "音量范围 ±60 dB，声像范围 ±100");
                return 0;
            }
            DseTrack* track = &a->score->tracks[a->active_track];
            float* field = id == B_TRACK_GAIN ? &track->gain : &track->pan;
            float v = (float)(id == B_TRACK_PAN ? value / 100 : value);
            if (*field != v) {
                dse_app_history(a);
                *field = v;
                a->score->revision++;
                dse_app_changed(a);
            }
        } else if (a->active_wave && (unsigned)a->active_wave <= a->score->wave_count) {
            DseWave* wave = &a->score->waves[a->active_wave - 1];
            DseWave candidate = *wave;
            if (id == B_AUDIO_GAIN) {
                if (fabs(value) > 60) {
                    snprintf(a->message, sizeof(a->message), "片段音量范围 ±60 dB");
                    return 0;
                }
                candidate.gain = (float)value;
            } else {
                if (value < 0 || value > DSE_TICK_LIMIT / (double)DSE_PPQ) {
                    snprintf(a->message, sizeof(a->message), "位置、裁剪和淡入淡出必须为非负拍数");
                    return 0;
                }
                int32_t* fields[] = {&candidate.pos, &candidate.skip, &candidate.trim,
                                     &candidate.fade_in, &candidate.fade_out};
                *fields[id - B_AUDIO_POS] = (int)llround(value * DSE_PPQ);
            }
            if (dse_wave_ticks(&candidate, a->score->bpm) <= 0 ||
                candidate.pos + dse_wave_ticks(&candidate, a->score->bpm) > DSE_TICK_LIMIT) {
                snprintf(a->message, sizeof(a->message), "裁剪超过音频时长或位置超出范围");
                return 0;
            }
            if (memcmp(wave, &candidate, sizeof(candidate))) {
                dse_app_history(a);
                if (candidate.skip != wave->skip || candidate.trim != wave->trim)
                    a->peaks_revision = 0;
                *wave = candidate;
                a->score->revision++;
                dse_app_changed(a);
            }
        }
    } else if (id == B_TIMING && note) {
        unsigned selected_count = 0, index = (unsigned)(note - a->score->notes);
        for (unsigned i = 0; i < a->score->count; i++)
            selected_count += a->score->notes[i].selected != 0;
        DseNotePlan plan;
        DseTiming timing;
        if (selected_count != 1) {
            snprintf(a->message, sizeof(a->message), "请只选择一个音符调整音素时长");
            return 0;
        }
        if (*a->edit && !dse_app_note_plan(a, index, &plan)) {
            snprintf(a->message, sizeof(a->message), "%s", a->front_message);
            return 0;
        }
        if (!*a->edit)
            memset(&timing, 0, sizeof(timing));
        else if (!dse_timing_parse(a->edit, a->score, index, &plan, &timing, a->message,
                                   sizeof(a->message)))
            return 0;
        const DseTiming* old = dse_timing_get(a->score, note);
        if ((old && memcmp(old, &timing, sizeof(timing))) || (!old && timing.count)) {
            dse_app_history(a);
            if (!dse_timing_set(a->score, index, &timing)) {
                snprintf(a->message, sizeof(a->message), "无法保存音素时长");
                return 0;
            }
            dse_app_changed(a);
            snprintf(a->message, sizeof(a->message), "音素时长已更新");
        }
    } else if (note) {
        if (strlen(a->edit) >= 64) {
            snprintf(a->message, sizeof(a->message), "歌词或音素最多 63 个 UTF-8 字节");
            return 0;
        }
        int differs = 0;
        for (unsigned i = 0; i < a->score->count; i++)
            if (a->score->notes[i].selected &&
                strcmp(id == B_LYRIC ? a->score->notes[i].lyric : a->score->notes[i].phonemes,
                       a->edit))
                differs = 1;
        if (differs) {
            dse_app_history(a);
            /* Changing the word or splitting an extension changes every
             * syllable's symbol assignment, including unselected owners. */
            for (unsigned i = 0; i < a->score->count; i++)
                if (a->score->notes[i].selected)
                    dse_timing_clear_word(a->score, i);
            for (unsigned i = 0; i < a->score->count; i++)
                if (a->score->notes[i].selected) {
                    strcpy(id == B_LYRIC ? a->score->notes[i].lyric : a->score->notes[i].phonemes,
                           a->edit);
                    if (id == B_LYRIC)
                        a->score->notes[i].phonemes[0] = 0;
                    a->score->notes[i].timing_slot = 0;
                }
            a->score->revision++;
            dse_app_changed(a);
        }
    }
    a->focus = 0;
    a->composition[0] = 0;
    return 1;
}
static void begin_edit(DseApp* a, int id) {
    if (a->focus == id)
        return;
    if (!commit(a))
        return;
    DseNote* note = selected(a);
    if (id == B_BPM)
        snprintf(a->edit, sizeof(a->edit), "%.3g", a->score->bpm);
    else if (id == B_TRACK_GAIN || id == B_TRACK_PAN) {
        DseTrack* track = &a->score->tracks[a->active_track];
        snprintf(a->edit, sizeof(a->edit), "%.4g",
                 id == B_TRACK_GAIN ? track->gain : track->pan * 100);
    } else if (id >= B_AUDIO_POS && id <= B_AUDIO_GAIN) {
        if (!a->active_wave || (unsigned)a->active_wave > a->score->wave_count)
            return;
        DseWave* wave = &a->score->waves[a->active_wave - 1];
        const int32_t fields[] = {wave->pos, wave->skip, wave->trim, wave->fade_in, wave->fade_out};
        snprintf(a->edit, sizeof(a->edit), "%.6g",
                 id == B_AUDIO_GAIN ? (double)wave->gain
                                    : fields[id - B_AUDIO_POS] / (double)DSE_PPQ);
    } else if (id == B_TIMING && note) {
        unsigned index = (unsigned)(note - a->score->notes);
        DseNotePlan plan;
        if (!dse_app_note_plan(a, index, &plan)) {
            snprintf(a->message, sizeof(a->message), "%s", a->front_message);
            return;
        }
        if (plan.tied) {
            index = plan.owner_index;
            dse_select(a->score, (int)index, 0);
            note = &a->score->notes[index];
            if (!dse_app_note_plan(a, index, &plan)) {
                snprintf(a->message, sizeof(a->message), "%s", a->front_message);
                return;
            }
            snprintf(a->message, sizeof(a->message), "正在调整延音所属音节的起始音符 %u",
                     index + 1);
        }
        dse_timing_text(a->score, index, &plan, a->edit, sizeof(a->edit));
    } else if (note)
        snprintf(a->edit, sizeof(a->edit), "%s", id == B_LYRIC ? note->lyric : note->phonemes);
    else
        return;
    a->focus = id;
    a->select_all = 1;
}
static int render_worker(void* user) {
    dse_render_run(user);
    return 0;
}
static int transform_notes(DseApp* a, int dt, int dk, int dl) {
    if (dl)
        for (unsigned i = 0; i < a->score->count; i++) {
            const DseNote* note = &a->score->notes[i];
            const DseTiming* timing = dse_timing_get(a->score, note);
            if (note->selected && timing && !dse_timing_valid(timing, note->len + dl)) {
                snprintf(a->message, sizeof(a->message),
                         "缩短音符会超过手工音素时长，请先调整或重置时长");
                return -1;
            }
        }
    if (!a->project_info.ustx || (!dt && !dk))
        return dse_transform_selected(a->score, dt, dk, dl);
    struct Range {
        int part, start, end;
    } ranges[DSE_MAX_NOTES];
    unsigned count = 0;
    for (unsigned i = 0; i < a->score->count; i++) {
        const DseNote* note = &a->score->notes[i];
        if (note->selected)
            ranges[count++] = (struct Range){note->part, note->pos, note->pos + note->len};
    }
    /* Imported absolute pitch must follow the edited melody. Validate all
     * affected values before the ASM group transform can mutate the notes. */
    for (unsigned i = 0; i < a->score->curve_count; i++) {
        const DseCurve* curve = &a->score->curves[i];
        if (curve->kind)
            continue;
        for (unsigned j = 0; j < count; j++)
            if (curve->part == ranges[j].part && curve->tick >= ranges[j].start &&
                curve->tick < ranges[j].end && (curve->value + dk < 0 || curve->value + dk > 127))
                return -1;
    }
    int changed = dse_transform_selected(a->score, dt, dk, dl);
    if (changed <= 0)
        return changed;
    unsigned output = 0;
    for (unsigned i = 0; i < a->score->curve_count; i++) {
        DseCurve curve = a->score->curves[i];
        int moved = 0, replaced = 0;
        if (!curve.kind) {
            for (unsigned j = 0; j < count; j++)
                if (curve.part == ranges[j].part && curve.tick >= ranges[j].start &&
                    curve.tick < ranges[j].end) {
                    curve.tick += dt;
                    curve.value += dk;
                    moved = 1;
                    break;
                }
            if (!moved && dt)
                for (unsigned j = 0; j < count; j++)
                    if (curve.part == ranges[j].part && curve.tick >= ranges[j].start + dt &&
                        curve.tick < ranges[j].end + dt) {
                        replaced = 1;
                        break;
                    }
        }
        if (!replaced)
            a->score->curves[output++] = curve;
    }
    a->score->curve_count = output;
    if (dt)
        dse_curve_sort(a->score);
    return changed;
}
static void start_render(DseApp* a, int auto_play, int audio_only) {
    if (a->render) {
        atomic_store(&a->render->cancel, 1);
        a->auto_play = 0;
        return;
    }
    if (!commit(a))
        return;
    pause_audio(a);
    a->render = calloc(1, sizeof(*a->render));
    if (!a->render)
        return;
    a->render->score = *a->score;
    a->render->config = a->config;
    a->render->audio_only = audio_only;
    a->auto_play = auto_play;
    atomic_init(&a->render->cancel, 0);
    atomic_init(&a->render->done, 0);
    atomic_init(&a->render->progress, 0);
    a->worker = SDL_CreateThread(render_worker, "DiffSinger render", a->render);
    if (!a->worker) {
        free(a->render);
        a->render = NULL;
        snprintf(a->message, sizeof(a->message), "无法启动合成线程");
        return;
    }
    snprintf(a->message, sizeof(a->message), "%s，可以继续编辑 · 再次点击可取消",
             audio_only ? "正在准备伴奏" : "正在合成歌声与伴奏");
}
void dse_app_render(DseApp* a, int auto_play) {
    start_render(a, auto_play, 0);
}
void dse_app_audio_preview(DseApp* a, int auto_play) {
    start_render(a, auto_play, 1);
}
int dse_app_import_audio(DseApp* a, const char* path, int relink) {
    if (!commit(a))
        return 0;
    if ((!relink && a->score->wave_count == DSE_MAX_WAVES) ||
        (relink && (!a->active_wave || (unsigned)a->active_wave > a->score->wave_count))) {
        snprintf(a->message, sizeof(a->message), "音频片段容量已满或没有选择片段");
        return 0;
    }
    DseWave wave = relink ? a->score->waves[a->active_wave - 1] : (DseWave){0};
    if (!dse_audio_probe(&wave, path, a->message, sizeof(a->message)))
        return 0;
    if (relink && dse_wave_ticks(&wave, a->score->bpm) <= 0) {
        snprintf(a->message, sizeof(a->message), "新文件短于现有裁剪，请先调整裁剪");
        return 0;
    }
    if (!relink) {
        wave.pos = dse_quantize((int)a->play_tick, a->view.grid);
        wave.track =
            a->score->track_count < DSE_MAX_TRACKS ? (int)a->score->track_count : a->active_track;
    }
    if (wave.pos + dse_wave_ticks(&wave, a->score->bpm) > DSE_TICK_LIMIT) {
        snprintf(a->message, sizeof(a->message), "音频片段超出工程时间范围");
        return 0;
    }
    dse_app_history(a);
    if ((unsigned)wave.track == a->score->track_count) {
        DseTrack* track = &a->score->tracks[a->score->track_count++];
        memset(track, 0, sizeof(*track));
        snprintf(track->name, sizeof(track->name), "伴奏 %u", a->score->wave_count + 1);
        track->color = 0x79abcfff;
    }
    unsigned index = relink ? (unsigned)a->active_wave - 1 : a->score->wave_count++;
    a->score->waves[index] = wave;
    a->peaks_revision = 0;
    a->active_wave = (int)index + 1;
    a->active_track = wave.track;
    a->track_scroll = wave.track > 1 ? wave.track - 1 : 0;
    dse_select(a->score, -1, 0);
    a->score->revision++;
    dse_app_changed(a);
    snprintf(a->message, sizeof(a->message), "已%s音频 %.220s · 点击伴奏试听",
             relink ? "重新链接" : "导入", wave.name);
    return 1;
}
static void new_project(DseApp* a) {
    pause_audio(a);
    history_clear(a);
    dse_project_info_clear(&a->project_info);
    uint64_t revision = a->score->revision + 1;
    dse_score_init(a->score, 0);
    a->score->revision = revision;
    a->path[0] = 0;
    a->view.active_part = 0;
    a->view.scroll_tick = 0;
    a->active_wave = a->active_track = a->arrange_tick = 0;
    a->play_tick = 0;
    a->modified = 0;
    a->pcm_revision = 0;
    a->peaks_revision = 0;
}
static void finish_action(DseApp* a, int kind) {
    if (kind == 1)
        a->quit = 1;
    else if (kind == 2)
        new_project(a);
    else if (kind == 3)
        dialog(a, B_OPEN);
}
static void save_project(DseApp* a, const char* path) {
    if (dse_project_save(a->score, &a->project_info, path, &a->config, a->message,
                         sizeof(a->message))) {
        if (path != a->path)
            snprintf(a->path, sizeof(a->path), "%s", path);
        a->modified = 0;
        snprintf(a->message, sizeof(a->message), "工程已保存");
        int action = a->after_save;
        a->after_save = 0;
        finish_action(a, action);
    } else
        a->after_save = 0;
}
void dse_app_action(DseApp* a, int id) {
    if (a->modal && id != B_MODAL_CANCEL && id != B_MODAL_DISCARD && id != B_MODAL_SAVE)
        return;
    if (id == B_LYRIC || id == B_PHONEMES || id == B_TIMING || id == B_BPM ||
        (id >= B_AUDIO_POS && id <= B_TRACK_PAN)) {
        begin_edit(a, id);
        return;
    }
    if (!commit(a))
        return;
    if (id >= B_PART_BASE && id < B_PART_BASE + DSE_MAX_PARTS) {
        int p = id - B_PART_BASE;
        if ((unsigned)p < a->score->part_count) {
            a->view.active_part = p;
            a->active_wave = 0;
            a->active_track = a->score->parts[p].track;
            a->view.scroll_tick = 0;
            dse_select(a->score, -1, 0);
        }
        return;
    }
    if (id >= B_WAVE_BASE && id < B_WAVE_BASE + DSE_MAX_WAVES) {
        unsigned w = (unsigned)(id - B_WAVE_BASE);
        if (w < a->score->wave_count) {
            a->active_wave = (int)w + 1;
            a->active_track = a->score->waves[w].track;
            dse_select(a->score, -1, 0);
        }
        return;
    }
    if (id >= B_TRACK_BASE && id < B_TRACK_BASE + DSE_MAX_TRACKS) {
        int t = id - B_TRACK_BASE;
        if ((unsigned)t >= a->score->track_count)
            return;
        a->active_track = t;
        a->active_wave = 0;
        for (unsigned w = 0; w < a->score->wave_count; w++)
            if (a->score->waves[w].track == t) {
                a->active_wave = (int)w + 1;
                break;
            }
        for (unsigned p = 0; p < a->score->part_count; p++)
            if (a->score->parts[p].track == t) {
                a->view.active_part = (int)p;
                a->active_wave = 0;
                a->view.scroll_tick = 0;
                dse_select(a->score, -1, 0);
                break;
            }
        return;
    }
    if ((id >= B_MUTE_BASE && id < B_MUTE_BASE + DSE_MAX_TRACKS) ||
        (id >= B_SOLO_BASE && id < B_SOLO_BASE + DSE_MAX_TRACKS)) {
        int solo = id >= B_SOLO_BASE, t = id - (solo ? B_SOLO_BASE : B_MUTE_BASE);
        if ((unsigned)t < a->score->track_count) {
            dse_app_history(a);
            if (solo)
                a->score->tracks[t].solo ^= 1;
            else
                a->score->tracks[t].mute ^= 1;
            a->score->revision++;
            dse_app_changed(a);
        }
        return;
    }
    if (id >= B_LANE_PITCH && id <= B_LANE_VELOCITY) {
        a->lane = id - B_LANE_PITCH;
        return;
    }
    switch (id) {
    case B_AUDIO_IMPORT:
    case B_AUDIO_RELINK:
        dialog(a, id);
        break;
    case B_AUDIO_PREVIEW:
        if (a->playing && a->pcm_audio_only)
            pause_audio(a);
        else if (a->pcm && a->pcm_audio_only && a->pcm_revision == a->score->revision)
            play_audio(a);
        else
            dse_app_audio_preview(a, 1);
        break;
    case B_AUDIO_DELETE:
        if (a->active_wave && (unsigned)a->active_wave <= a->score->wave_count) {
            dse_app_history(a);
            unsigned index = (unsigned)a->active_wave - 1;
            memmove(a->score->waves + index, a->score->waves + index + 1,
                    (a->score->wave_count - index - 1) * sizeof(DseWave));
            memset(&a->score->waves[--a->score->wave_count], 0, sizeof(DseWave));
            a->peaks_revision = 0;
            a->active_wave = 0;
            a->score->revision++;
            dse_app_changed(a);
        }
        break;
    case B_NEW:
        if (a->modified)
            a->modal = 2;
        else
            new_project(a);
        break;
    case B_OPEN:
        if (a->modified)
            a->modal = 3;
        else
            dialog(a, id);
        break;
    case B_SAVE:
        if (*a->path && !dse_path_is_ustx(a->path))
            save_project(a, a->path);
        else
            dialog(a, id);
        break;
    case B_EXPORT:
        if (!a->pcm || a->pcm_revision != a->score->revision)
            snprintf(a->message, sizeof(a->message), "请先合成当前工程再导出");
        else
            dialog(a, id);
        break;
    case B_SINGER:
        dialog(a, id);
        break;
    case B_SPEAKER: {
        if (dse_engine_cycle_speaker(&a->config, a->message, sizeof(a->message))) {
            a->score->revision++;
            dse_app_changed(a);
        }
        break;
    }
    case B_LANGUAGE:
        if (dse_engine_cycle_language(&a->config, a->message, sizeof(a->message))) {
            a->score->revision++;
            dse_app_changed(a);
        }
        break;
    case B_TIMING_RESET: {
        unsigned char reset[DSE_MAX_NOTES] = {0};
        int changed = 0;
        for (unsigned i = 0; i < a->score->count; i++)
            if (a->score->notes[i].selected) {
                unsigned owner = i;
                if (!a->score->notes[i].timing_slot) {
                    DseNotePlan plan;
                    if (dse_app_note_plan(a, i, &plan) && plan.tied)
                        owner = plan.owner_index;
                }
                reset[owner] = 1;
                changed |= a->score->notes[owner].timing_slot != 0;
            }
        if (changed) {
            dse_app_history(a);
            for (unsigned i = 0; i < a->score->count; i++)
                if (reset[i])
                    a->score->notes[i].timing_slot = 0;
            a->score->revision++;
            dse_app_changed(a);
            snprintf(a->message, sizeof(a->message), "已恢复自动音素时长");
        }
        break;
    }
    case B_RENDER:
        dse_app_render(a, 0);
        break;
    case B_PLAY:
        play_audio(a);
        break;
    case B_STOP:
        pause_audio(a);
        a->play_tick = 0;
        break;
    case B_UNDO:
        restore(a, 0);
        break;
    case B_REDO:
        restore(a, 1);
        break;
    case B_TRACK:
        if (a->score->track_count == DSE_MAX_TRACKS || a->score->part_count == DSE_MAX_PARTS) {
            snprintf(a->message, sizeof(a->message), "音轨或片段数量已达上限");
            break;
        }
        dse_app_history(a);
        {
            unsigned t = a->score->track_count++, p = a->score->part_count++;
            snprintf(a->score->tracks[t].name, 64, "歌声 %u", t + 1);
            a->score->tracks[t].color = 0x79abcfff;
            snprintf(a->score->parts[p].name, 64, "新歌声片段 %u", p + 1);
            a->score->parts[p].track = (int)t;
            a->score->parts[p].len = 7680;
            a->view.active_part = (int)p;
            a->active_track = (int)t;
            a->active_wave = 0;
            a->view.scroll_tick = 0;
            a->track_scroll = t > 1 ? (int)t - 1 : 0;
            dse_select(a->score, -1, 0);
            a->score->revision++;
            dse_app_changed(a);
        }
        break;
    case B_SELECT:
        a->view.tool = 0;
        break;
    case B_DRAW:
        a->view.tool = 1;
        break;
    case B_PITCH:
        a->view.tool = 2;
        break;
    case B_ERASE:
        a->view.tool = 3;
        break;
    case B_SNAP: {
        static const int grids[] = {120, 240, 480, 60};
        unsigned i = 0;
        for (; i < 4; i++)
            if (a->view.grid == grids[i])
                break;
        a->view.grid = grids[(i + 1) % 4];
        break;
    }
    case B_ZOOM_OUT:
        a->view.beat_width = fmaxf(24, a->view.beat_width / 1.2f);
        break;
    case B_ZOOM_IN:
        a->view.beat_width = fminf(300, a->view.beat_width * 1.2f);
        break;
    case B_CLEAR_CURVE:
        dse_app_history(a);
        {
            unsigned count = 0;
            for (unsigned i = 0; i < a->score->curve_count; i++)
                if (a->score->curves[i].part != a->view.active_part ||
                    a->score->curves[i].kind != (unsigned)a->lane)
                    a->score->curves[count++] = a->score->curves[i];
            a->score->curve_count = count;
            a->score->revision++;
            dse_app_changed(a);
        }
        break;
    case B_MODAL_CANCEL:
        a->modal = 0;
        break;
    case B_MODAL_DISCARD: {
        int action = a->modal;
        a->modal = 0;
        finish_action(a, action);
        break;
    }
    case B_MODAL_SAVE:
        a->after_save = a->modal;
        a->modal = 0;
        dse_app_action(a, B_SAVE);
        break;
    default:
        break;
    }
}
static int inside(DseRect r, float x, float y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}
static int note_key(DseApp* a, int tick) {
    for (unsigned i = 0; i < a->score->count; i++) {
        DseNote* note = &a->score->notes[i];
        if (note->part == a->view.active_part && tick >= note->pos && tick <= note->pos + note->len)
            return note->key;
    }
    return 60;
}
static void curve_pointer(DseApp* a, float x, float y, int roll) {
    DseView* v = &a->view;
    int tick = dse_quantize(v->scroll_tick + (int)((x - v->x) / v->beat_width * 480), v->grid / 4);
    if (x < v->x || x >= v->x + v->w)
        return;
    float value;
    int kind = roll ? 0 : a->lane;
    if (roll)
        value = v->top_key - (y - v->y) / v->row_height + .5f;
    else {
        float mid = v->y + v->h + 76 + 42 + 33, delta = (mid - y) / 33;
        float scales[] = {2, 12, 5, 12, 1, .75f};
        value = (kind == 0 ? note_key(a, tick) : kind == 5 ? 1 : 0) + delta * scales[kind];
    }
    if (kind == 0)
        value = fmaxf(0, fminf(127, value));
    else if (kind == 5)
        value = fmaxf(.5f, fminf(2, value));
    else if (kind == 4)
        value = fmaxf(-1, fminf(1, value));
    else
        value = fmaxf(kind == 2 ? -5 : -12, fminf(kind == 2 ? 5 : 12, value));
    if (!dse_curve_set(a->score, v->active_part, kind, tick, value))
        snprintf(a->message, sizeof(a->message), "参数曲线已达容量上限");
    else
        dse_app_changed(a);
}
static void pointer_down(DseApp* a, const SDL_MouseButtonEvent* event) {
    float x = event->x, y = event->y;
    a->drag_x = x;
    a->drag_y = y;
    int shift = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
    if (event->button == SDL_BUTTON_RIGHT) {
        int index = dse_hit_note(a->score, &a->view, x, y);
        if (index >= 0) {
            if (!commit(a))
                return;
            dse_select(a->score, index, 0);
            dse_app_history(a);
            dse_delete_selected(a->score);
            dse_app_changed(a);
        }
        return;
    }
    if (event->button != SDL_BUTTON_LEFT)
        return;
    for (unsigned i = a->button_count; i-- > 0;)
        if (inside(a->buttons[i].rect, x, y)) {
            int id = a->buttons[i].id;
            dse_app_action(a, id);
            if (id >= B_WAVE_BASE && id < B_WAVE_BASE + DSE_MAX_WAVES && a->active_wave) {
                float beat = a->arrange_beat > 0 ? a->arrange_beat : 42;
                const DseWave* wave = &a->score->waves[a->active_wave - 1];
                float begin = 208 + (wave->pos - a->arrange_tick) / (float)DSE_PPQ * beat,
                      end = begin + (float)dse_wave_ticks(wave, a->score->bpm) / DSE_PPQ * beat;
                a->drag = x < begin + 7 ? 8 : x > end - 7 ? 7 : 6;
                a->drag_tick =
                    dse_quantize((int)((x - 208) / beat * DSE_PPQ) + a->arrange_tick, a->view.grid);
                a->drag_index = a->active_wave - 1;
            }
            return;
        }
    if (a->modal)
        return;
    if (!commit(a))
        return;
    if (y >= 147 && y < 172 && x >= 208 && x < a->view.width - 276) {
        int was = a->playing;
        pause_audio(a);
        a->play_tick = fmaxf(0, (x - 208) / (a->arrange_beat > 0 ? a->arrange_beat : 42) * DSE_PPQ +
                                    a->arrange_tick);
        if (was)
            play_audio(a);
        return;
    }
    if (event->clicks >= 2 && y >= 174 && y < 270 && x >= 208 && x < a->view.width - 276) {
        if (a->score->part_count == DSE_MAX_PARTS)
            return;
        int track = (int)((y - 174) / 42) + a->track_scroll;
        if (track < 0 || (unsigned)track >= a->score->track_count)
            return;
        int pos =
            dse_quantize((int)((x - 208) / (a->arrange_beat > 0 ? a->arrange_beat : 42) * DSE_PPQ) +
                             a->arrange_tick,
                         1920);
        dse_app_history(a);
        unsigned p = a->score->part_count++;
        snprintf(a->score->parts[p].name, 64, "歌声片段 %u", p + 1);
        a->score->parts[p].track = track;
        a->score->parts[p].pos = pos;
        a->score->parts[p].len = 3840;
        a->view.active_part = (int)p;
        a->active_track = track;
        a->active_wave = 0;
        a->view.scroll_tick = 0;
        dse_select(a->score, -1, 0);
        a->score->revision++;
        dse_app_changed(a);
        return;
    }
    DseView* v = &a->view;
    float expression = v->y + v->h + 118;
    if (y >= expression && y <= expression + 66 && x >= v->x && x < v->x + v->w) {
        dse_app_history(a);
        a->drag = 5;
        curve_pointer(a, x, y, 0);
        return;
    }
    int tick, key;
    if (!dse_grid_coord(v, x, y, &tick, &key))
        return;
    a->active_wave = 0;
    a->active_track = a->score->parts[v->active_part].track;
    tick = dse_quantize(tick, v->grid);
    if (v->tool == 2) {
        dse_app_history(a);
        a->drag = 3;
        curve_pointer(a, x, y, 1);
        return;
    }
    int index = dse_hit_note(a->score, v, x, y);
    if (index >= 0) {
        if (v->tool == 3) {
            dse_select(a->score, index, 0);
            dse_app_history(a);
            dse_delete_selected(a->score);
            dse_app_changed(a);
            return;
        }
        if (shift || !a->score->notes[index].selected)
            dse_select(a->score, index, shift);
        if (event->clicks >= 2) {
            begin_edit(a, B_LYRIC);
            return;
        }
        DseRect r;
        dse_note_rect(&a->score->notes[index], v, &r);
        a->drag = x >= r.x + r.w - 7 ? 2 : 1;
        a->drag_index = index;
    } else if (v->tool == 1) {
        dse_app_history(a);
        dse_select(a->score, -1, 0);
        index = dse_add_note(a->score, v->active_part, tick, v->grid, key);
        if (index < 0) {
            snprintf(a->message, sizeof(a->message), "无法添加音符：容量或时间范围已达上限");
            return;
        }
        a->score->notes[index].phonemes[0] = 0;
        dse_app_changed(a);
        a->drag = 2;
        a->drag_index = index;
        a->drag_started = 1;
    } else if (v->tool == 0) {
        if (!shift)
            dse_select(a->score, -1, 0);
        a->drag = 4;
        a->box_additive = shift;
        a->selection_box = (DseRect){x, y, 0, 0};
    }
    a->drag_tick = tick;
    a->drag_key = key;
    a->drag_x = x;
    a->drag_y = y;
}
static void pointer_move(DseApp* a, float x, float y) {
    a->pointer_x = x;
    a->pointer_y = y;
    if (!a->drag)
        return;
    if (a->drag >= 6 && a->drag <= 8) {
        if ((unsigned)a->drag_index >= a->score->wave_count)
            return;
        float beat = a->arrange_beat > 0 ? a->arrange_beat : 42;
        int tick = dse_quantize((int)((x - 208) / beat * DSE_PPQ) + a->arrange_tick, a->view.grid);
        int delta = tick - a->drag_tick;
        DseWave* wave = &a->score->waves[a->drag_index];
        DseWave candidate = *wave;
        if (a->drag == 6) {
            candidate.pos += delta;
            int track = (int)((y - 174) / 42) + a->track_scroll;
            if (y >= 174 && y < 270 && track >= 0 && (unsigned)track < a->score->track_count)
                candidate.track = track;
        } else if (a->drag == 7)
            candidate.trim -= delta;
        else {
            candidate.pos += delta;
            candidate.skip += delta;
        }
        if (candidate.pos < 0 || candidate.skip < 0 || candidate.trim < 0 ||
            candidate.pos > DSE_TICK_LIMIT || candidate.skip > DSE_TICK_LIMIT ||
            candidate.trim > DSE_TICK_LIMIT || dse_wave_ticks(&candidate, a->score->bpm) <= 0 ||
            candidate.pos + dse_wave_ticks(&candidate, a->score->bpm) > DSE_TICK_LIMIT ||
            !memcmp(wave, &candidate, sizeof(candidate)))
            return;
        if (!a->drag_started) {
            dse_app_history(a);
            a->drag_started = 1;
        }
        *wave = candidate;
        a->drag_tick = tick;
        a->active_track = wave->track;
        a->score->revision++;
        if (a->drag != 6)
            a->peaks_revision = 0;
        dse_app_changed(a);
        return;
    }
    if (a->drag == 3 || a->drag == 5) {
        curve_pointer(a, x, y, a->drag == 3);
        return;
    }
    if (a->drag == 4) {
        a->selection_box = (DseRect){fminf(x, a->drag_x), fminf(y, a->drag_y), fabsf(x - a->drag_x),
                                     fabsf(y - a->drag_y)};
        return;
    }
    int tick, key;
    if (!dse_grid_coord(&a->view, x, y, &tick, &key))
        return;
    tick = dse_quantize(tick, a->view.grid);
    int dt = tick - a->drag_tick, dk = key - a->drag_key;
    if (!dt && (a->drag == 2 || !dk))
        return;
    if (!a->drag_started) {
        dse_app_history(a);
        a->drag_started = 1;
    }
    int changed =
        transform_notes(a, a->drag == 1 ? dt : 0, a->drag == 1 ? dk : 0, a->drag == 2 ? dt : 0);
    if (changed > 0) {
        a->drag_tick = tick;
        a->drag_key = key;
        dse_app_changed(a);
    }
}
static void copy(DseApp* a) {
    a->clipboard_count = 0;
    for (unsigned i = 0; i < a->score->count; i++)
        if (a->score->notes[i].selected) {
            unsigned index = a->clipboard_count++;
            a->clipboard[index] = a->score->notes[i];
            const DseTiming* timing = dse_timing_get(a->score, &a->score->notes[i]);
            a->clipboard_timing[index] = timing ? *timing : (DseTiming){0};
            a->clipboard[index].timing_slot = 0;
        }
    snprintf(a->message, sizeof(a->message), "已复制 %u 个音符", a->clipboard_count);
}
static void paste(DseApp* a) {
    if (!a->clipboard_count)
        return;
    if (a->score->count + a->clipboard_count > DSE_MAX_NOTES) {
        snprintf(a->message, sizeof(a->message), "粘贴会超过音符容量");
        return;
    }
    int first = DSE_TICK_LIMIT;
    for (unsigned i = 0; i < a->clipboard_count; i++)
        if (a->clipboard[i].pos < first)
            first = a->clipboard[i].pos;
    int offset =
        dse_quantize((int)a->play_tick - a->score->parts[a->view.active_part].pos, a->view.grid) -
        first;
    for (unsigned i = 0; i < a->clipboard_count; i++)
        if (a->clipboard[i].pos + offset < 0 ||
            (int64_t)a->clipboard[i].pos + offset + a->clipboard[i].len > DSE_TICK_LIMIT) {
            snprintf(a->message, sizeof(a->message), "粘贴位置超出时间范围");
            return;
        }
    dse_app_history(a);
    dse_select(a->score, -1, 0);
    for (unsigned i = 0; i < a->clipboard_count; i++) {
        int j = dse_add_note(a->score, a->view.active_part, a->clipboard[i].pos + offset,
                             a->clipboard[i].len, a->clipboard[i].key);
        uint32_t id = a->score->notes[j].id;
        a->score->notes[j] = a->clipboard[i];
        a->score->notes[j].id = id;
        a->score->notes[j].part = a->view.active_part;
        a->score->notes[j].pos += offset;
        if (a->clipboard_timing[i].count)
            dse_timing_set(a->score, (unsigned)j, &a->clipboard_timing[i]);
    }
    dse_app_changed(a);
}
static void key_down(DseApp* a, const SDL_KeyboardEvent* key) {
    int ctrl = (key->mod & SDL_KMOD_CTRL) != 0, shift = (key->mod & SDL_KMOD_SHIFT) != 0;
    if (a->modal) {
        if (key->key == SDLK_ESCAPE)
            a->modal = 0;
        return;
    }
    if (a->focus) {
        if (key->key == SDLK_RETURN || key->key == SDLK_KP_ENTER)
            commit(a);
        else if (key->key == SDLK_ESCAPE) {
            a->focus = 0;
            a->composition[0] = 0;
        } else if (ctrl && key->key == SDLK_A)
            a->select_all = 1;
        else if (key->key == SDLK_BACKSPACE) {
            if (a->select_all) {
                a->edit[0] = 0;
                a->select_all = 0;
            } else {
                size_t n = strlen(a->edit);
                if (n) {
                    n--;
                    while (n && ((unsigned char)a->edit[n] & 0xc0) == 0x80)
                        n--;
                    a->edit[n] = 0;
                }
            }
        } else if (key->key == SDLK_TAB) {
            int next = a->focus == B_LYRIC                                  ? B_PHONEMES
                       : a->focus == B_PHONEMES                             ? B_LYRIC
                       : a->focus >= B_AUDIO_POS && a->focus < B_AUDIO_GAIN ? a->focus + 1
                       : a->focus == B_TRACK_GAIN                           ? B_TRACK_PAN
                                                                            : a->focus;
            if (commit(a))
                begin_edit(a, next);
        } else if (ctrl && key->key == SDLK_S) {
            if (commit(a))
                dse_app_action(a, B_SAVE);
        }
        return;
    }
    if (ctrl) {
        switch (key->key) {
        case SDLK_S:
            dse_app_action(a, B_SAVE);
            return;
        case SDLK_O:
            dse_app_action(a, B_OPEN);
            return;
        case SDLK_N:
            dse_app_action(a, B_NEW);
            return;
        case SDLK_Z:
            restore(a, shift);
            return;
        case SDLK_Y:
            restore(a, 1);
            return;
        case SDLK_C:
            copy(a);
            return;
        case SDLK_V:
            paste(a);
            return;
        case SDLK_A:
            for (unsigned i = 0; i < a->score->count; i++)
                a->score->notes[i].selected = a->score->notes[i].part == a->view.active_part;
            return;
        default:
            break;
        }
    }
    if (key->key == SDLK_SPACE) {
        play_audio(a);
        return;
    }
    if (key->key == SDLK_F5) {
        dse_app_render(a, 0);
        return;
    }
    if (key->key == SDLK_ESCAPE) {
        dse_select(a->score, -1, 0);
        return;
    }
    if (key->key == SDLK_RETURN) {
        begin_edit(a, B_LYRIC);
        return;
    }
    if (key->key == SDLK_HOME) {
        pause_audio(a);
        a->play_tick = 0;
        a->view.scroll_tick = 0;
        a->arrange_tick = 0;
        return;
    }
    if (key->key == SDLK_S)
        a->view.tool = 0;
    else if (key->key == SDLK_D)
        a->view.tool = 1;
    else if (key->key == SDLK_R)
        a->view.tool = 2;
    else if (key->key == SDLK_E)
        a->view.tool = 3;
    if (key->key == SDLK_DELETE || key->key == SDLK_BACKSPACE) {
        if (a->active_wave) {
            dse_app_action(a, B_AUDIO_DELETE);
            return;
        }
        if (selected(a)) {
            dse_app_history(a);
            dse_delete_selected(a->score);
            dse_app_changed(a);
        }
        return;
    }
    int dt = 0, dk = 0;
    if (key->key == SDLK_LEFT)
        dt = -a->view.grid;
    if (key->key == SDLK_RIGHT)
        dt = a->view.grid;
    if (key->key == SDLK_UP)
        dk = ctrl ? 12 : 1;
    if (key->key == SDLK_DOWN)
        dk = ctrl ? -12 : -1;
    if (dt && a->active_wave && (unsigned)a->active_wave <= a->score->wave_count) {
        DseWave* wave = &a->score->waves[a->active_wave - 1];
        if (wave->pos + dt >= 0 &&
            wave->pos + dt + dse_wave_ticks(wave, a->score->bpm) <= DSE_TICK_LIMIT) {
            dse_app_history(a);
            wave->pos += dt;
            a->score->revision++;
            dse_app_changed(a);
        }
        return;
    }
    if ((dt || dk) && selected(a)) {
        dse_app_history(a);
        if (transform_notes(a, shift ? 0 : dt, dk, shift ? dt : 0) > 0)
            dse_app_changed(a);
    }
}
void dse_app_event(DseApp* a, const SDL_Event* event) {
    if (a->dialog && event->type == a->dialog->event) {
        const char* path = event->user.data1;
        SDL_SetAtomicInt(&a->dialog->pending, 0);
        if (path && *path) {
            switch (event->user.code) {
            case B_SAVE:
                save_project(a, path);
                break;
            case B_OPEN:
                if (dse_project_load(a->score, &a->project_info, path, a->message,
                                     sizeof(a->message))) {
                    pause_audio(a);
                    history_clear(a);
                    snprintf(a->path, sizeof(a->path), "%s", path);
                    dse_engine_load_project(&a->config, path, a->message, sizeof(a->message));
                    if (dse_path_is_ustx(path))
                        dse_engine_import(&a->config, &a->project_info);
                    a->modified = 0;
                    a->view.active_part = 0;
                    a->active_wave = a->active_track = a->arrange_tick = 0;
                    a->peaks_revision = 0;
                    a->view.scroll_tick = 0;
                    a->play_tick = 0;
                    dse_select(a->score, -1, 0);
                    if (a->project_info.ustx && a->score->count) {
                        a->view.scroll_tick = a->score->notes[0].pos / 480 * 480;
                        a->view.top_key = a->score->notes[0].key + 8;
                        if (a->view.top_key > 127)
                            a->view.top_key = 127;
                        a->track_scroll = a->score->parts[0].track;
                    }
                    if (a->score->wave_count && !a->render)
                        dse_app_audio_preview(a, 0);
                }
                break;
            case B_AUDIO_IMPORT:
            case B_AUDIO_RELINK:
                if (dse_app_import_audio(a, path, event->user.code == B_AUDIO_RELINK) && !a->render)
                    dse_app_audio_preview(a, 0);
                break;
            case B_SINGER:
                if (dse_engine_resolve(&a->config, path, a->message, sizeof(a->message))) {
                    a->front_valid = 0;
                    a->score->revision++;
                    dse_app_changed(a);
                    snprintf(a->message, sizeof(a->message), "已选择声库 %s", a->config.name);
                }
                break;
            case B_EXPORT:
                if (a->pcm && a->pcm_revision == a->score->revision &&
                    dse_audio_destination(a->score, path, a->message, sizeof(a->message)) &&
                    dse_render_wav(path, a->pcm, a->samples, 44100, a->message, sizeof(a->message)))
                    snprintf(a->message, sizeof(a->message), "%s WAV 已导出",
                             a->pcm_audio_only ? "伴奏" : "歌声与伴奏混音");
                break;
            }
        } else
            a->after_save = 0;
        SDL_free(event->user.data1);
        return;
    }
    switch (event->type) {
    case SDL_EVENT_DROP_FILE:
        if (event->drop.data && dse_app_import_audio(a, event->drop.data, 0) && !a->render)
            dse_app_audio_preview(a, 0);
        break;
    case SDL_EVENT_QUIT:
        if (a->modified)
            a->modal = 1;
        else
            a->quit = 1;
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        pointer_down(a, &event->button);
        break;
    case SDL_EVENT_MOUSE_MOTION:
        pointer_move(a, event->motion.x, event->motion.y);
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (a->drag == 4) {
            for (unsigned i = 0; i < a->score->count; i++)
                if (a->score->notes[i].part == a->view.active_part) {
                    DseRect r;
                    dse_note_rect(&a->score->notes[i], &a->view, &r);
                    if (r.x < a->selection_box.x + a->selection_box.w &&
                        r.x + r.w > a->selection_box.x &&
                        r.y < a->selection_box.y + a->selection_box.h &&
                        r.y + r.h > a->selection_box.y)
                        a->score->notes[i].selected = 1;
                }
        }
        a->drag = a->drag_started = 0;
        break;
    case SDL_EVENT_KEY_DOWN:
        if (!event->key.repeat)
            key_down(a, &event->key);
        break;
    case SDL_EVENT_TEXT_INPUT:
        if (a->focus) {
            const char* text = event->text.text;
            size_t old = a->select_all ? 0 : strlen(a->edit), add = strlen(text);
            if (old + add < sizeof(a->edit)) {
                if (a->select_all)
                    a->edit[0] = 0;
                memcpy(a->edit + old, text, add + 1);
                a->select_all = 0;
            }
            a->composition[0] = 0;
        }
        break;
    case SDL_EVENT_TEXT_EDITING:
        if (a->focus)
            snprintf(a->composition, sizeof(a->composition), "%s", event->edit.text);
        break;
    case SDL_EVENT_MOUSE_WHEEL: {
        SDL_Keymod mods = SDL_GetModState();
        float dy = event->wheel.y;
        if (a->pointer_x > a->view.width - 276 && a->pointer_y >= 340 &&
            a->pointer_y < a->view.height - 115) {
            int max = (int)(780 - (a->view.height - 115));
            if (max < 0)
                max = 0;
            a->inspector_scroll -= (int)(dy * 40);
            if (a->inspector_scroll < 0)
                a->inspector_scroll = 0;
            if (a->inspector_scroll > max)
                a->inspector_scroll = max;
            break;
        }
        if (a->pointer_y < 281) {
            if (mods & SDL_KMOD_SHIFT || fabsf(event->wheel.x) > .01f) {
                a->arrange_tick += (int)(-dy * DSE_PPQ * 4 - event->wheel.x * DSE_PPQ * 4);
                if (a->arrange_tick < 0)
                    a->arrange_tick = 0;
                if (a->arrange_tick > DSE_TICK_LIMIT - 10000)
                    a->arrange_tick = DSE_TICK_LIMIT - 10000;
            } else if (mods & SDL_KMOD_CTRL) {
                float beat = a->arrange_beat > 0 ? a->arrange_beat : 42;
                a->arrange_beat = fmaxf(3, fminf(168, beat * powf(1.12f, dy)));
            } else
                a->track_scroll -= (int)dy;
            int max = (int)a->score->track_count - 2;
            if (max < 0)
                max = 0;
            if (a->track_scroll < 0)
                a->track_scroll = 0;
            if (a->track_scroll > max)
                a->track_scroll = max;
        } else if (mods & SDL_KMOD_CTRL) {
            float factor = powf(1.12f, dy);
            a->view.beat_width = fmaxf(24, fminf(300, a->view.beat_width * factor));
        } else if (mods & SDL_KMOD_SHIFT || fabsf(event->wheel.x) > .01f) {
            int delta = (int)(-dy * 480 - event->wheel.x * 480);
            a->view.scroll_tick += delta;
            if (a->view.scroll_tick < 0)
                a->view.scroll_tick = 0;
            if (a->view.scroll_tick > DSE_TICK_LIMIT - 10000)
                a->view.scroll_tick = DSE_TICK_LIMIT - 10000;
        } else {
            a->view.top_key += (int)dy * 2;
            int minimum = (int)(a->view.h / a->view.row_height);
            if (a->view.top_key < minimum)
                a->view.top_key = minimum;
            if (a->view.top_key > 127)
                a->view.top_key = 127;
        }
        break;
    }
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        a->drag = a->drag_started = 0;
        break;
    default:
        break;
    }
}
void dse_app_tick(DseApp* a) {
    if (a->playing && a->audio) {
        int queued = SDL_GetAudioStreamQueued(a->audio);
        if (queued >= 0) {
            size_t remaining = (size_t)queued;
            if (remaining > a->play_queued)
                remaining = a->play_queued;
            size_t sample = a->play_start + (a->play_queued - remaining) / (2 * sizeof(float));
            a->play_tick = (float)(sample * 480.0 * a->score->bpm / (60 * 44100));
            if (!queued)
                pause_audio(a);
        }
    }
    if (a->render && atomic_load_explicit(&a->render->done, memory_order_acquire)) {
        SDL_WaitThread(a->worker, NULL);
        a->worker = NULL;
        DseRender* r = a->render;
        snprintf(a->message, sizeof(a->message), "%s", r->message);
        printf("DiffSinger job: %s\n", r->work);
        int auto_play = a->auto_play;
        a->auto_play = 0;
        if (r->success && r->score.revision == a->score->revision) {
            free(a->pcm);
            a->pcm = r->pcm;
            r->pcm = NULL;
            a->samples = r->samples;
            a->pcm_revision = a->score->revision;
            a->pcm_audio_only = r->audio_only;
            memcpy(a->peaks, r->peaks, sizeof(a->peaks));
            a->peaks_revision = a->score->revision;
        } else if (r->success)
            snprintf(a->message, sizeof(a->message), "合成期间工程已修改，请重新合成当前版本");
        free(r->pcm);
        free(r);
        a->render = NULL;
        if (auto_play && a->pcm_revision == a->score->revision)
            play_audio(a);
    }
    if (a->desktop) {
        SDL_Window* window = asmui_desktop_window(a->desktop);
        if (window) {
            if (a->focus && !SDL_TextInputActive(window))
                SDL_StartTextInput(window);
            if (!a->focus && SDL_TextInputActive(window))
                SDL_StopTextInput(window);
            if (a->focus) {
                SDL_Rect r = {(int)(a->view.width - 256), a->focus == B_LYRIC ? 449 : 528, 236, 34};
                if (a->focus == B_BPM)
                    r = (SDL_Rect){458, 57, 87, 31};
                for (unsigned b = 0; b < a->button_count; b++)
                    if (a->buttons[b].id == a->focus) {
                        DseRect bounds = a->buttons[b].rect;
                        r = (SDL_Rect){(int)bounds.x, (int)bounds.y, (int)bounds.w, (int)bounds.h};
                        break;
                    }
                SDL_SetTextInputArea(window, &r, r.w);
            }
        }
    }
}
void dse_app_cleanup(DseApp* a) {
    if (a->front_load) {
        SDL_WaitThread(a->front_worker, NULL);
        dse_frontend_close(a->front_load->result);
        free(a->front_load);
    }
    dse_frontend_close(a->frontend);
    if (a->render) {
        atomic_store(&a->render->cancel, 1);
        SDL_WaitThread(a->worker, NULL);
        free(a->render->pcm);
        free(a->render);
    }
    if (a->audio)
        SDL_DestroyAudioStream(a->audio);
    if (a->dialog) {
        SDL_LockMutex(a->dialog->mutex);
        a->dialog->active = 0;
        SDL_UnlockMutex(a->dialog->mutex);
        SDL_Event event;
        while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, a->dialog->event, a->dialog->event) > 0)
            SDL_free(event.user.data1);
        release_dialog(a->dialog);
    }
    history_clear(a);
    dse_project_info_clear(&a->project_info);
    free(a->pcm);
    free(a->score);
    free(a->nodes);
    free(a->strings);
}
