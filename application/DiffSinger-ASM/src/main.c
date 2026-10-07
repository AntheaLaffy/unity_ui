#define _POSIX_C_SOURCE 200809L
#include "app.h"
#include "audio.h"
#include <errno.h>
#include <json-c/json.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef DSE_FRAMEWORK_ROOT
#define DSE_FRAMEWORK_ROOT "."
#endif
typedef struct Script {
    double seconds, wait_start;
    char command[32], args[1024];
} Script;
/* Gates poll actual asynchronous state while the normal window loop keeps
 * running. The clock limit is a failure bound, not a guessed readiness time. */
static int script_gate(DseApp* a, Script* s, double seconds) {
    int frontend = !strcmp(s->command, "wait-frontend"),
        render = !strcmp(s->command, "wait-render"), idle = !strcmp(s->command, "wait-idle");
    if (!frontend && !render && !idle)
        return 0;
    if (!s->wait_start)
        s->wait_start = seconds;
    if (frontend) {
        unsigned index = a->score->count;
        for (unsigned i = 0; i < a->score->count; i++)
            if (a->score->notes[i].selected) {
                index = i;
                break;
            }
        DseNotePlan plan;
        if (index < a->score->count && dse_app_note_plan(a, index, &plan))
            return 1;
        if (index == a->score->count || (!a->front_load && a->front_valid)) {
            fprintf(stderr, "Script frontend gate failed: %s\n", a->front_message);
            return -2;
        }
    } else if (idle) {
        if (!a->render)
            return 1;
    } else {
        if (!a->render && a->pcm && a->pcm_revision == a->score->revision)
            return 1;
        if (!a->render) {
            fprintf(stderr, "Script render gate failed: %s\n", a->message);
            return -2;
        }
    }
    if (seconds - s->wait_start > (frontend ? 60 : 180)) {
        fprintf(stderr, "Script %s timed out\n", s->command);
        return -2;
    }
    return -1;
}
static unsigned unsigned_option(const char* option, const char* value, unsigned low,
                                unsigned high) {
    char* end;
    errno = 0;
    unsigned long number = strtoul(value, &end, 10);
    if (!*value || *value == '-' || *end || errno || number < low || number > high) {
        fprintf(stderr, "%s requires an integer in %u..%u\n", option, low, high);
        exit(2);
    }
    return (unsigned)number;
}
static unsigned script_load(const char* path, Script* events, unsigned capacity) {
    if (!path)
        return 0;
    FILE* file = fopen(path, "r");
    if (!file) {
        perror(path);
        exit(1);
    }
    char line[1200];
    unsigned count = 0;
    double previous = -1;
    while (fgets(line, sizeof(line), file)) {
        if (line[0] == '#' || line[0] == '\n')
            continue;
        Script event = {0};
        int offset = 0;
        if (sscanf(line, "%lf %31s %n", &event.seconds, event.command, &offset) < 2 ||
            count == capacity || !isfinite(event.seconds) || event.seconds < 0 ||
            event.seconds < previous) {
            fprintf(stderr, "Invalid input script\n");
            exit(1);
        }
        snprintf(event.args, sizeof(event.args), "%s", line + offset);
        size_t n = strlen(event.args);
        while (n && (event.args[n - 1] == '\n' || event.args[n - 1] == '\r'))
            event.args[--n] = 0;
        previous = event.seconds;
        events[count++] = event;
    }
    fclose(file);
    return count;
}
static void scripted_event(SDL_Event* event) {
    if (event->type == SDL_EVENT_KEY_DOWN) {
        event->key.down = true;
        event->key.repeat = false;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN || event->type == SDL_EVENT_MOUSE_BUTTON_UP)
        event->button.down = event->type == SDL_EVENT_MOUSE_BUTTON_DOWN;
    if (!SDL_PushEvent(event)) {
        fprintf(stderr, "SDL script push failed: %s\n", SDL_GetError());
        exit(1);
    }
}
static void push_script(DseApp* a, Script* s, char* capture, size_t n) {
    if (getenv("DIFFSINGER_EDITOR_TRACE_INPUT")) {
        unsigned selected = 0;
        for (unsigned k = 0; k < a->score->count; k++)
            selected += a->score->notes[k].selected != 0;
        printf("Script %.2f %s %s | viewport=%.0fx%.0f top=%d part=%d focus=%d drag=%d "
               "selected=%u note0=%d,%d\n",
               s->seconds, s->command, s->args, a->view.width, a->view.height, a->view.top_key,
               a->view.active_part, a->focus, a->drag, selected, a->score->notes[0].pos,
               a->score->notes[0].key);
    }
    SDL_Event event;
    SDL_zero(event);
    SDL_Window* window = asmui_desktop_window(a->desktop);
    float logical_x = 1, logical_y = 1;
    if (window) {
        int w, h;
        SDL_GetWindowSize(window, &w, &h);
        logical_x = w / a->view.width;
        logical_y = h / a->view.height;
    }
    if (!strcmp(s->command, "button")) {
        static const struct {
            const char* name;
            int id;
        } names[] = {{"lyric", B_LYRIC},
                     {"phonemes", B_PHONEMES},
                     {"timing", B_TIMING},
                     {"timing-reset", B_TIMING_RESET},
                     {"language", B_LANGUAGE},
                     {"draw", B_DRAW},
                     {"pitch", B_PITCH},
                     {"select", B_SELECT},
                     {"track", B_TRACK},
                     {"undo", B_UNDO},
                     {"redo", B_REDO},
                     {"render", B_RENDER},
                     {"audio", B_AUDIO_PREVIEW},
                     {"audio-pos", B_AUDIO_POS},
                     {"audio-skip", B_AUDIO_SKIP},
                     {"audio-trim", B_AUDIO_TRIM},
                     {"audio-fade-in", B_AUDIO_FADE_IN},
                     {"audio-fade-out", B_AUDIO_FADE_OUT},
                     {"audio-gain", B_AUDIO_GAIN},
                     {"track-gain", B_TRACK_GAIN},
                     {"track-pan", B_TRACK_PAN},
                     {"audio-delete", B_AUDIO_DELETE}};
        int id = 0;
        for (unsigned k = 0; k < sizeof(names) / sizeof(*names); k++)
            if (!strcmp(s->args, names[k].name))
                id = names[k].id;
        for (unsigned k = 0; k < a->button_count; k++)
            if (a->buttons[k].id == id) {
                DseRect r = a->buttons[k].rect;
                event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
                event.button.button = SDL_BUTTON_LEFT;
                event.button.clicks = 1;
                event.button.x = (r.x + r.w / 2) * logical_x;
                event.button.y = (r.y + r.h / 2) * logical_y;
                scripted_event(&event);
                event.type = SDL_EVENT_MOUSE_BUTTON_UP;
                scripted_event(&event);
                return;
            }
        fprintf(stderr, "Script button is not visible: %s\n", s->args);
        exit(1);
    }
    if (!strcmp(s->command, "wave")) {
        unsigned index = (unsigned)strtoul(s->args, NULL, 10);
        if (index >= a->score->wave_count)
            exit(1);
        for (unsigned k = 0; k < a->button_count; k++)
            if (a->buttons[k].id == B_WAVE_BASE + (int)index) {
                DseRect bounds = a->buttons[k].rect;
                event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
                event.button.button = SDL_BUTTON_LEFT;
                event.button.clicks = 1;
                event.button.x = (bounds.x + fminf(bounds.w / 2, 40)) * logical_x;
                event.button.y = (bounds.y + bounds.h / 2) * logical_y;
                scripted_event(&event);
                event.type = SDL_EVENT_MOUSE_BUTTON_UP;
                scripted_event(&event);
                return;
            }
        fprintf(stderr, "Script wave is not visible: %u\n", index);
        exit(1);
    }
    if (!strcmp(s->command, "note") || !strcmp(s->command, "down-note")) {
        unsigned index = (unsigned)strtoul(s->args, NULL, 10);
        if (index >= a->score->count) {
            fprintf(stderr, "Invalid script note\n");
            exit(1);
        }
        DseRect r;
        dse_note_rect(&a->score->notes[index], &a->view, &r);
        event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        event.button.button = SDL_BUTTON_LEFT;
        event.button.clicks = 1;
        event.button.x = (r.x + fminf(r.w / 2, 20)) * logical_x;
        event.button.y = (r.y + r.h / 2) * logical_y;
        scripted_event(&event);
        if (!strcmp(s->command, "note")) {
            event.type = SDL_EVENT_MOUSE_BUTTON_UP;
            scripted_event(&event);
        }
        return;
    }
    if (!strcmp(s->command, "move-delta")) {
        float tick, key;
        if (sscanf(s->args, "%f %f", &tick, &key) != 2)
            exit(1);
        event.type = SDL_EVENT_MOUSE_MOTION;
        event.motion.x = (a->drag_x + tick / 480 * a->view.beat_width) * logical_x;
        event.motion.y = (a->drag_y - key * a->view.row_height) * logical_y;
        scripted_event(&event);
        return;
    }
    if (!strcmp(s->command, "point")) {
        int tick, key;
        if (sscanf(s->args, "%d %d", &tick, &key) != 2)
            exit(1);
        event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        event.button.button = SDL_BUTTON_LEFT;
        event.button.clicks = 1;
        event.button.x =
            (a->view.x + (tick - a->view.scroll_tick) / 480.f * a->view.beat_width + 2) * logical_x;
        event.button.y =
            (a->view.y + (a->view.top_key - key + .5f) * a->view.row_height) * logical_y;
        scripted_event(&event);
        event.type = SDL_EVENT_MOUSE_BUTTON_UP;
        scripted_event(&event);
        return;
    }
    if (!strcmp(s->command, "capture")) {
        snprintf(capture, n, "%s", s->args);
        return;
    }
    if (!strcmp(s->command, "resize")) {
        int w, h;
        if (sscanf(s->args, "%d %d", &w, &h) == 2 && window)
            SDL_SetWindowSize(window, w, h);
        return;
    }
    if (!strcmp(s->command, "text")) {
        event.type = SDL_EVENT_TEXT_INPUT;
        event.text.text = s->args;
    } else if (!strcmp(s->command, "audio-file")) {
        event.type = SDL_EVENT_DROP_FILE;
        event.drop.data = s->args;
    } else if (!strcmp(s->command, "preedit")) {
        event.type = SDL_EVENT_TEXT_EDITING;
        event.edit.text = s->args;
    } else if (!strcmp(s->command, "key")) {
        char* key = s->args;
        SDL_Keymod mods = 0;
        for (;;) {
            if (!strncmp(key, "ctrl+", 5)) {
                mods |= SDL_KMOD_CTRL;
                key += 5;
            } else if (!strncmp(key, "shift+", 6)) {
                mods |= SDL_KMOD_SHIFT;
                key += 6;
            } else
                break;
        }
        event.type = SDL_EVENT_KEY_DOWN;
        event.key.key = SDL_GetKeyFromName(key);
        event.key.mod = mods;
    } else if (!strcmp(s->command, "move")) {
        event.type = SDL_EVENT_MOUSE_MOTION;
        sscanf(s->args, "%f %f", &event.motion.x, &event.motion.y);
    } else if (!strcmp(s->command, "down") || !strcmp(s->command, "up") ||
               !strcmp(s->command, "click") || !strcmp(s->command, "double")) {
        event.type =
            !strcmp(s->command, "up") ? SDL_EVENT_MOUSE_BUTTON_UP : SDL_EVENT_MOUSE_BUTTON_DOWN;
        event.button.button = SDL_BUTTON_LEFT;
        event.button.clicks = !strcmp(s->command, "double") ? 2 : 1;
        sscanf(s->args, "%f %f", &event.button.x, &event.button.y);
        if (!strcmp(s->command, "click") || !strcmp(s->command, "double")) {
            scripted_event(&event);
            event.type = SDL_EVENT_MOUSE_BUTTON_UP;
        }
    } else if (!strcmp(s->command, "wheel")) {
        event.type = SDL_EVENT_MOUSE_WHEEL;
        sscanf(s->args, "%f %f", &event.wheel.x, &event.wheel.y);
    } else if (!strcmp(s->command, "render")) {
        event.type = SDL_EVENT_KEY_DOWN;
        event.key.key = SDLK_F5;
    } else {
        fprintf(stderr, "Unknown input script command: %s\n", s->command);
        exit(1);
    }
    scripted_event(&event);
}
static void help(void) {
    puts("DiffSinger-ASM · dedicated DiffSinger singing editor (x86-64)\n"
         "  diffsinger-asm [--project song.dse|song.ustx|song.tlp] [--new] [--singer "
         "PACKED_DIR]\n"
         "  diffsinger-asm --project song.dse --render output.wav\n"
         "  diffsinger-asm --project song.ustx --render-audio accompaniment.wav\n"
         "  diffsinger-asm --new --import-audio backing.flac\n"
         "  diffsinger-asm --headless --output screenshot.png\n"
         "  diffsinger-asm --project song.ustx --check-lyrics\n"
         "  --engine DIR --dictionary FILE --language CODE --speaker NAME --steps N --workers N\n"
         "  --width N --height N --frames N --duration SECONDS --script FILE\n"
         "  --state-out FILE --render-preview --audio-preview --write-demo FILE\n"
         "Controls: S select, D draw, R pitch, E erase, Space play/pause, F5 render,\n"
         "Ctrl+S save, Ctrl+Z/Y undo/redo, Ctrl+C/V copy/paste, Delete, arrow keys.\n"
         "Constant tempo; voice and audio parts; DiffSinger-ASM acoustic + vocoder only.");
}
static int check_lyrics(DseApp* a) {
    DseFrontend* frontend = dse_frontend_open(&a->config, a->message, sizeof(a->message));
    json_object *root = json_object_new_object(), *parts = json_object_new_array();
    json_object_object_add(root, "language", json_object_new_string(a->config.language_code));
    json_object_object_add(root, "languageId", json_object_new_int(a->config.language));
    json_object_object_add(root, "bank", json_object_new_string(a->config.name));
    unsigned errors = 0;
    if (!frontend) {
        json_object_object_add(root, "error", json_object_new_string(a->message));
        errors++;
    } else
        for (unsigned p = 0; p < a->score->part_count; p++) {
            json_object *part = json_object_new_object(), *phones = json_object_new_array();
            json_object_object_add(part, "part", json_object_new_int(p));
            json_object_object_add(part, "name", json_object_new_string(a->score->parts[p].name));
            DsePartPlan plan;
            if (!dse_frontend_part(frontend, a->score, p, &plan, a->message, sizeof(a->message))) {
                json_object_object_add(part, "error", json_object_new_string(a->message));
                errors++;
            } else {
                json_object_object_add(part, "frames", json_object_new_int(plan.frames));
                for (unsigned i = 0; i < plan.count; i++) {
                    const DsePhone* phone = &plan.phones[i];
                    json_object* v = json_object_new_object();
                    json_object_object_add(v, "symbol", json_object_new_string(phone->symbol));
                    json_object_object_add(v, "token", json_object_new_int(phone->token));
                    json_object_object_add(v, "language", json_object_new_int(phone->language));
                    json_object_object_add(v, "frames", json_object_new_int(phone->frames));
                    json_object_array_add(phones, v);
                }
                dse_part_plan_clear(&plan);
            }
            json_object_object_add(part, "phones", phones);
            json_object_array_add(parts, part);
        }
    dse_frontend_close(frontend);
    json_object_object_add(root, "parts", parts);
    json_object_object_add(root, "errors", json_object_new_int(errors));
    puts(json_object_to_json_string_ext(root, JSON_C_TO_STRING_PRETTY));
    json_object_put(root);
    return errors ? 1 : 0;
}
int main(int argc, char** argv) {
    const char *project = NULL, *singer = NULL, *dictionary = NULL, *render = NULL, *output = NULL,
               *input = NULL, *state = NULL, *demo = NULL, *speaker = NULL, *report = NULL,
               *cache = NULL, *import_audio = NULL, *language = NULL;
    char engine[DSE_PATH];
    snprintf(engine, sizeof(engine), "%s/application/diffsinger", DSE_FRAMEWORK_ROOT);
    int headless = 0, new = 0, render_preview = 0, audio_only = 0, audio_preview = 0,
        lyric_check = 0;
    unsigned maxframes = 0, width = 1440, height = 940, steps = 0, workers = 0;
    double duration = 0;
    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];
        if (!strcmp(arg, "--help")) {
            help();
            return 0;
        }
        if (!strcmp(arg, "--headless")) {
            headless = 1;
            continue;
        }
        if (!strcmp(arg, "--new")) {
            new = 1;
            continue;
        }
        if (!strcmp(arg, "--render-preview")) {
            render_preview = 1;
            continue;
        }
        if (!strcmp(arg, "--audio-preview")) {
            audio_preview = 1;
            continue;
        }
        if (!strcmp(arg, "--check-lyrics")) {
            lyric_check = 1;
            continue;
        }
        if (i + 1 == argc) {
            fprintf(stderr, "Missing value for %s\n", arg);
            return 2;
        }
        const char* value = argv[++i];
        if (strlen(value) >= DSE_PATH) {
            fprintf(stderr, "Value for %s is too long\n", arg);
            return 2;
        }
        if (!strcmp(arg, "--project"))
            project = value;
        else if (!strcmp(arg, "--singer"))
            singer = value;
        else if (!strcmp(arg, "--dictionary"))
            dictionary = value;
        else if (!strcmp(arg, "--language"))
            language = value;
        else if (!strcmp(arg, "--speaker"))
            speaker = value;
        else if (!strcmp(arg, "--render"))
            render = value;
        else if (!strcmp(arg, "--render-audio")) {
            render = value;
            audio_only = 1;
        } else if (!strcmp(arg, "--import-audio"))
            import_audio = value;
        else if (!strcmp(arg, "--output"))
            output = value;
        else if (!strcmp(arg, "--script"))
            input = value;
        else if (!strcmp(arg, "--state-out"))
            state = value;
        else if (!strcmp(arg, "--report-out"))
            report = value;
        else if (!strcmp(arg, "--cache-out"))
            cache = value;
        else if (!strcmp(arg, "--write-demo"))
            demo = value;
        else if (!strcmp(arg, "--engine"))
            snprintf(engine, sizeof(engine), "%s", value);
        else if (!strcmp(arg, "--frames"))
            maxframes = unsigned_option(arg, value, 1, UINT_MAX);
        else if (!strcmp(arg, "--duration")) {
            char* end;
            duration = strtod(value, &end);
            if (!*value || *end || !isfinite(duration) || duration <= 0) {
                fprintf(stderr, "--duration requires a positive number of seconds\n");
                return 2;
            }
        } else if (!strcmp(arg, "--width"))
            width = unsigned_option(arg, value, 1024, 7680);
        else if (!strcmp(arg, "--height"))
            height = unsigned_option(arg, value, 800, 4320);
        else if (!strcmp(arg, "--steps"))
            steps = unsigned_option(arg, value, 1, 100);
        else if (!strcmp(arg, "--workers"))
            workers = unsigned_option(arg, value, 1, 64);
        else {
            fprintf(stderr, "Unknown option %s\n", arg);
            return 2;
        }
    }
    if (width < 1024 || width > 7680 || height < 800 || height > 4320 || !isfinite(duration) ||
        duration < 0) {
        fprintf(stderr, "Invalid window size or duration\n");
        return 2;
    }
    DseApp* a = calloc(1, sizeof(*a));
    if (!a)
        return 1;
    a->score = calloc(1, sizeof(*a->score));
    if (!a->score)
        return 1;
    dse_score_init(a->score, !new);
    a->view.grid = 120;
    if (project) {
        if (!dse_project_load(a->score, &a->project_info, project, a->message,
                              sizeof(a->message))) {
            fprintf(stderr, "%s\n", a->message);
            return 1;
        }
        snprintf(a->path, sizeof(a->path), "%s", project);
    }
    if (import_audio && !dse_app_import_audio(a, import_audio, 0)) {
        fprintf(stderr, "%s\n", a->message);
        dse_app_cleanup(a);
        free(a);
        return 1;
    }
    if (demo) {
        int ok = dse_project_save(a->score, &a->project_info, demo, NULL, a->message,
                                  sizeof(a->message));
        if (!ok)
            fprintf(stderr, "%s\n", a->message);
        dse_app_cleanup(a);
        free(a);
        return ok ? 0 : 1;
    }
    dse_engine_default(&a->config, engine);
    if (project)
        dse_engine_load_project(&a->config, project, a->message, sizeof(a->message));
    if (project && dse_path_is_ustx(project))
        dse_engine_import(&a->config, &a->project_info);
    if (steps)
        a->config.steps = steps;
    if (workers)
        a->config.workers = workers;
    if (singer && !dse_engine_resolve(&a->config, singer, a->message, sizeof(a->message))) {
        fprintf(stderr, "%s\n", a->message);
        return 1;
    }
    if (language && !dse_engine_language(&a->config, language, a->message, sizeof(a->message))) {
        fprintf(stderr, "%s\n", a->message);
        return 1;
    }
    if (dictionary)
        snprintf(a->config.dictionary, sizeof(a->config.dictionary), "%s", dictionary);
    if (speaker) {
        if (strlen(speaker) >= 64 || strchr(speaker, '/') || strstr(speaker, "..")) {
            fprintf(stderr, "Invalid speaker name\n");
            return 2;
        }
        strcpy(a->config.speaker, speaker);
    }
    if (lyric_check) {
        int result = check_lyrics(a);
        dse_app_cleanup(a);
        free(a);
        return result;
    }
    if (render) {
        DseRender* r = calloc(1, sizeof(*r));
        if (!r)
            return 1;
        r->score = *a->score;
        r->config = a->config;
        r->audio_only = audio_only;
        if (strlen(render) >= sizeof(r->output)) {
            fprintf(stderr, "Output path too long\n");
            return 2;
        }
        strcpy(r->output, render);
        atomic_init(&r->cancel, 0);
        atomic_init(&r->done, 0);
        atomic_init(&r->progress, 0);
        int ok = dse_render_run(r);
        printf("%s\n任务目录: %s\n", r->message, r->work);
        free(r->pcm);
        free(r);
        dse_project_info_clear(&a->project_info);
        free(a->score);
        free(a);
        return ok ? 0 : 1;
    }
    a->view = (DseView){.width = width,
                        .height = height,
                        .beat_width = 64,
                        .row_height = 22,
                        .top_key = 72,
                        .tool = 1,
                        .grid = 120};
    if (a->project_info.ustx && a->score->count) {
        a->view.scroll_tick = a->score->notes[0].pos / 480 * 480;
        a->view.top_key = a->score->notes[0].key + 8;
        if (a->view.top_key > 127)
            a->view.top_key = 127;
        a->track_scroll = a->score->parts[0].track;
    }
    a->nodes = calloc(DSE_NODES, sizeof(*a->nodes));
    a->strings = malloc(1024 * 1024);
    if (!a->nodes || !a->strings)
        return 1;
    char shaders[DSE_PATH];
    snprintf(shaders, sizeof(shaders), "%s/build/shaders", DSE_FRAMEWORK_ROOT);
    const char* texts[] = {
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 "
        ".,:;/_+-−#()[]%→▶■●↶↷",
        "DiffSinger-ASM "
        "专属虚拟歌姬编辑器主唱歌声声库与合成渲染引擎未选择音符属性歌词拼音音素时长位置张力气"
        "声清浊"
        "性别"
        "速度音高参数曲线画笔擦除选择重置吸附复制粘贴导出新建打开保存撤销播放暂停就绪拖动滚轮"
        "框"
        "选未"
        "命名工程未保存等待合成已完成让世界听见你的声音第一段旋律保存并继续取消放弃修改重新合"
        "成"
        "中文"
        "输入正常测试导入原始数据保留个伴奏未播放条参数未应用音素表情待适配"};
    AsmUiDesktopConfig cfg = {.title = "DiffSinger-ASM · 虚拟歌姬编辑器",
                              .shader_dir = shaders,
                              .width = width,
                              .height = height,
                              .max_nodes = DSE_NODES,
                              .max_draws = DSE_DRAWS,
                              .headless = headless,
                              .min_width = 1024,
                              .min_height = 800,
                              .texts = texts,
                              .text_count = 2};
    a->desktop = asmui_desktop_open(&cfg);
    dse_app_layout(a);
    Script* events = calloc(2048, sizeof(*events));
    unsigned count = script_load(input, events, 2048);
    unsigned frames = 0;
    uint64_t started = SDL_GetTicksNS();
    unsigned next_event = 0;
    double script_delay = 0;
    char capture[DSE_PATH] = {0};
    int exit_status = 0;
    if (headless && !maxframes && !duration && !input && !render_preview && !audio_preview)
        maxframes = 1;
    if (render_preview) {
        dse_app_render(a, 0);
        if (!a->render) {
            fprintf(stderr, "%s\n", a->message);
            exit_status = 1;
            maxframes = 1;
        }
    } else if (audio_preview || a->score->wave_count)
        dse_app_audio_preview(a, 0);
    while (!a->quit) {
        double seconds = (SDL_GetTicksNS() - started) / 1e9;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (getenv("DIFFSINGER_EDITOR_TRACE_INPUT") &&
                (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_KEY_DOWN))
                printf("Dispatch type=%u key=%u mod=%u modal=%d\n", event.type,
                       event.type == SDL_EVENT_KEY_DOWN ? event.key.key : 0,
                       event.type == SDL_EVENT_KEY_DOWN ? event.key.mod : 0, a->modal);
            SDL_Window* window = asmui_desktop_window(a->desktop);
            if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
                event.type == SDL_EVENT_WINDOW_RESIZED) {
                float old_height = a->view.h;
                asmui_desktop_resize(a->desktop);
                asmui_desktop_size(a->desktop, &a->view.width, &a->view.height);
                dse_app_layout(a);
                a->view.top_key +=
                    (int)lroundf((a->view.h - old_height) / (2 * a->view.row_height));
                if (a->view.top_key > 127)
                    a->view.top_key = 127;
            }
            if (window &&
                (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                 event.type == SDL_EVENT_MOUSE_BUTTON_UP || event.type == SDL_EVENT_MOUSE_MOTION)) {
                int ww, hh;
                SDL_GetWindowSize(window, &ww, &hh);
                if (ww && hh) {
                    float sx = a->view.width / ww, sy = a->view.height / hh;
                    if (event.type == SDL_EVENT_MOUSE_MOTION) {
                        event.motion.x *= sx;
                        event.motion.y *= sy;
                    } else {
                        event.button.x *= sx;
                        event.button.y *= sy;
                    }
                }
            }
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F12) {
                snprintf(capture, sizeof(capture), "%s", output ? output : "diffsinger.png");
                continue;
            }
            dse_app_event(a, &event);
        }
        int was_rendering = a->render != NULL;
        dse_app_tick(a);
        if ((render_preview || audio_preview) && was_rendering && !a->render &&
            (!a->pcm || a->pcm_revision != a->score->revision))
            exit_status = 1;
        dse_app_scene(a);
        /* Resolve scripted coordinates after pending native resize events and
         * scene layout. Startup/font stalls must preserve event spacing. Each
         * queued input is consumed by the next frame's normal SDL event loop. */
        if (next_event < count && events[next_event].seconds + script_delay <= seconds) {
            script_delay = seconds - events[next_event].seconds;
            int gate = script_gate(a, &events[next_event], seconds);
            if (gate == -2) {
                exit_status = 1;
                break;
            }
            if (!gate)
                push_script(a, &events[next_event], capture, sizeof(capture));
            if (gate >= 0)
                next_event++;
        }
        int end = (maxframes && frames + 1 >= maxframes) ||
                  (duration && seconds >= duration + script_delay) ||
                  ((render_preview || audio_preview) && was_rendering && !a->render);
        if (end && output)
            snprintf(capture, sizeof(capture), "%s", output);
        int presented =
            asmui_desktop_present(a->desktop, &a->canvas, 1.f / 60.f, *capture ? capture : NULL);
        if (presented)
            capture[0] = 0;
        frames++;
        if (end && presented)
            break;
        SDL_Delay(16);
    }
    if (state && !dse_project_save(a->score, &a->project_info, state, &a->config, a->message,
                                   sizeof(a->message))) {
        fprintf(stderr, "%s\n", a->message);
        exit_status = 1;
    }
    if (cache) {
        if (!a->pcm || a->pcm_revision != a->score->revision ||
            !dse_audio_destination(a->score, cache, a->message, sizeof(a->message)) ||
            !dse_render_wav(cache, a->pcm, a->samples, 44100, a->message, sizeof(a->message)))
            exit_status = 1;
    }
    if (report) {
        json_object* r = json_object_new_object();
        json_object_object_add(r, "notes", json_object_new_int(a->score->count));
        json_object_object_add(r, "tracks", json_object_new_int(a->score->track_count));
        json_object_object_add(r, "curves", json_object_new_int(a->score->curve_count));
        json_object_object_add(r, "language", json_object_new_string(a->config.language_code));
        json_object_object_add(r, "languageId", json_object_new_int(a->config.language));
        unsigned manual_timings = 0;
        for (unsigned i = 0; i < a->score->count; i++)
            manual_timings += a->score->notes[i].timing_slot != 0;
        json_object_object_add(r, "manualTimings", json_object_new_int(manual_timings));
        json_object_object_add(r, "frontendReady", json_object_new_boolean(a->frontend != NULL));
        json_object_object_add(r, "sourceFormat",
                               json_object_new_string(a->project_info.ustx ? "USTX" : "DSE/TLP"));
        json_object_object_add(r, "waveParts", json_object_new_int(a->score->wave_count));
        json_object_object_add(r, "audioOnly", json_object_new_boolean(a->pcm_audio_only));
        json_object_object_add(r, "audioPeaksReady",
                               json_object_new_boolean(a->peaks_revision != 0));
        json_object_object_add(r, "activeWave", json_object_new_int(a->active_wave));
        json_object_object_add(r, "activeTrack", json_object_new_int(a->active_track));
        json_object_object_add(r, "arrangeTick", json_object_new_int(a->arrange_tick));
        json_object_object_add(r, "parametersNotApplied",
                               json_object_new_int(a->project_info.unsupported_curves));
        json_object_object_add(r, "modified", json_object_new_boolean(a->modified));
        json_object_object_add(r, "path", json_object_new_string(a->path));
        json_object_object_add(r, "playTick", json_object_new_double(a->play_tick));
        json_object_object_add(
            r, "rendered",
            json_object_new_boolean(a->pcm && a->pcm_revision == a->score->revision));
        json_object_object_add(r, "samples", json_object_new_int64(a->samples));
        json_object_object_add(r, "message", json_object_new_string(a->message));
        if (json_object_to_file_ext(report, r, JSON_C_TO_STRING_PRETTY))
            exit_status = 1;
        json_object_put(r);
    }
    printf("DiffSinger-ASM: %u frames, %u notes, %u tracks\n", frames, a->score->count,
           a->score->track_count);
    AsmUiDesktop* desktop = a->desktop;
    dse_app_cleanup(a);
    asmui_desktop_close(desktop);
    free(events);
    free(a);
    return exit_status;
}
