#define _POSIX_C_SOURCE 200809L
#include "engine.h"
#include "audio.h"
#include "frontend.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <json-c/json.h>
#include <math.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
extern char** environ;
static int join(char* out, size_t bytes, const char* a, const char* b) {
    size_t x = strlen(a), y = strlen(b);
    if (x + y + 2 > bytes) {
        if (bytes)
            out[0] = 0;
        return 0;
    }
    memcpy(out, a, x);
    out[x] = '/';
    memcpy(out + x + 1, b, y + 1);
    return 1;
}
static int err(char* s, size_t n, const char* v) {
    snprintf(s, n, "%s", v);
    return 0;
}
static int requires_variance(const DseEngineConfig* c) {
    char path[DSE_PATH], line[4096];
    join(path, sizeof(path), c->bundle, "acoustic/model.conf");
    FILE* f = fopen(path, "r");
    unsigned flags = 0;
    if (f) {
        while (fgets(line, sizeof(line), f))
            sscanf(line, "feature_flags=%u", &flags);
        fclose(f);
    }
    return (flags & 14u) != 0;
}
static json_object* get(json_object* o, const char* k) {
    json_object* v = NULL;
    if (!json_object_is_type(o, json_type_object))
        return NULL;
    json_object_object_get_ex(o, k, &v);
    return v;
}
static int pick_speaker(const DseEngineConfig* c, const char* after, char out[64]) {
    char path[DSE_PATH], first[64] = "", following[64] = "";
    if (!join(path, sizeof(path), c->bundle, "acoustic"))
        return 0;
    DIR* dir = opendir(path);
    if (!dir)
        return 0;
    struct dirent* entry;
    while ((entry = readdir(dir))) {
        size_t len = strlen(entry->d_name);
        if (len <= 4 || len >= 68 || strcmp(entry->d_name + len - 4, ".emb") ||
            strstr(entry->d_name, ".."))
            continue;
        char name[64];
        memcpy(name, entry->d_name, len - 4);
        name[len - 4] = 0;
        if (!*first || strcmp(name, first) < 0)
            strcpy(first, name);
        if (strcmp(name, after) > 0 && (!*following || strcmp(name, following) < 0))
            strcpy(following, name);
    }
    closedir(dir);
    if (!*first)
        return 0;
    strcpy(out, *following ? following : first);
    return 1;
}
int dse_engine_cycle_speaker(DseEngineConfig* c, char* msg, size_t n) {
    char next[64];
    if (!pick_speaker(c, c->speaker, next))
        return err(msg, n, "声库没有可选择的 .emb 声线");
    if (!strcmp(next, c->speaker))
        return err(msg, n, "声库只有一条可用声线");
    strcpy(c->speaker, next);
    snprintf(msg, n, "已选择声线 %s", next);
    return 1;
}
int dse_engine_load_project(DseEngineConfig* c, const char* path, char* msg, size_t n) {
    json_object *root = json_object_from_file(path), *settings = get(root, "asmui");
    if (!settings) {
        if (root)
            json_object_put(root);
        return 1;
    }
    if (!json_object_is_type(settings, json_type_object)) {
        json_object_put(root);
        c->bundle[0] = 0;
        return err(msg, n, "工程声库配置必须为对象");
    }
    const char* keys[] = {"singer", "dictionary", "speaker", "name", "bank", "languageCode"};
    char* fields[] = {c->bundle, c->dictionary, c->speaker, c->name, c->bank, c->language_code};
    size_t sizes[] = {sizeof(c->bundle), sizeof(c->dictionary), sizeof(c->speaker),
                      sizeof(c->name),   sizeof(c->bank),       sizeof(c->language_code)};
    for (unsigned i = 0; i < 6; i++) {
        json_object* v = get(settings, keys[i]);
        const char* text = v ? json_object_get_string(v) : "";
        if ((v && !json_object_is_type(v, json_type_string)) || !text || strlen(text) >= sizes[i]) {
            json_object_put(root);
            c->bundle[0] = 0;
            return err(msg, n, "工程声库配置字段无效");
        }
        strcpy(fields[i], text);
    }
    if (strchr(c->speaker, '/') || strstr(c->speaker, "..")) {
        json_object_put(root);
        c->bundle[0] = 0;
        return err(msg, n, "工程声线名称无效");
    }
    c->steps = (unsigned)json_object_get_int(get(settings, "steps"));
    c->workers = (unsigned)json_object_get_int(get(settings, "workers"));
    c->language = (unsigned)json_object_get_int(get(settings, "language"));
    c->depth = (float)json_object_get_double(get(settings, "depth"));
    /* Older native saves stored language=0 even for a multilingual bank.
     * Recover only from actual metadata and the selected dictionary. */
    if ((!*c->language_code || !*c->bank) && *c->bundle) {
        DseEngineConfig fixed = *c;
        const char* code = strstr(c->dictionary, "dsdict-ja") ? "ja" : "zh";
        char unused[512];
        if (dse_engine_language(&fixed, code, unused, sizeof(unused))) {
            strcpy(c->bank, fixed.bank);
            strcpy(c->language_code, fixed.language_code);
            c->language = fixed.language;
        }
    }
    int valid = json_object_is_type(get(settings, "steps"), json_type_int) &&
                json_object_is_type(get(settings, "workers"), json_type_int) &&
                json_object_is_type(get(settings, "language"), json_type_int) && c->steps >= 1 &&
                c->steps <= 100 && c->workers >= 1 && c->workers <= 64 && c->language <= 1024 &&
                isfinite(c->depth) && c->depth > 0 && c->depth <= 1;
    json_object_put(root);
    if (!valid) {
        c->bundle[0] = 0;
        return err(msg, n, "工程合成参数无效");
    }
    return 1;
}
void dse_engine_default(DseEngineConfig* c, const char* root) {
    memset(c, 0, sizeof(*c));
    snprintf(c->engine, sizeof(c->engine), "%s", root);
    strcpy(c->speaker, "Normal");
    strcpy(c->name, "未选择声库");
    c->steps = 20;
    c->workers = 8;
    c->depth = .6f;
    const char* env = getenv("DIFFSINGER_EDITOR_SINGER");
    char message[512];
    if (env && *env)
        dse_engine_resolve(c, env, message, sizeof(message));
    else
        dse_engine_resolve(c, "/home/fuurin/.local/share/OpenUtau/Singers/yousaV1.56", message,
                           sizeof(message));
    env = getenv("DIFFSINGER_EDITOR_DICTIONARY");
    if (env)
        snprintf(c->dictionary, sizeof(c->dictionary), "%s", env);
}
int dse_engine_resolve(DseEngineConfig* c, const char* folder, char* msg, size_t n) {
    DseEngineConfig next = *c;
    char path[DSE_PATH];
    if (strlen(folder) >= sizeof(next.bundle))
        return err(msg, n, "声库路径过长");
    strcpy(next.bundle, folder);
    strcpy(next.bank, folder);
    char* generation_root = strstr(next.bank, "/dsasm/generations/");
    if (generation_root)
        *generation_root = 0;
    join(path, sizeof(path), folder, "acoustic/fs2_acoustic.dsfs");
    if (access(path, R_OK)) {
        /* Existing generation directories are model data only. No consumer
         * ABI, protocol or OpenUtau runtime is loaded by this editor. */
        join(path, sizeof(path), folder, "dsasm/current.json");
        json_object* current = json_object_from_file(path);
        json_object* generation = get(current, "generation");
        const char* id = generation ? json_object_get_string(generation) : NULL;
        if (!id || strchr(id, '/') || strstr(id, "..")) {
            if (current)
                json_object_put(current);
            return err(msg, n, "请选择包含 acoustic 和 vocoder 的已打包声库");
        }
        char suffix[256];
        snprintf(suffix, sizeof(suffix), "dsasm/generations/%s", id);
        int valid = join(next.bundle, sizeof(next.bundle), folder, suffix);
        json_object_put(current);
        if (!valid)
            return err(msg, n, "声库路径过长");
    }
    join(path, sizeof(path), next.bank, "dsdur/dsdict-zh.yaml");
    if (!access(path, R_OK))
        strcpy(next.dictionary, path);
    else {
        join(path, sizeof(path), next.bank, "dsdict.yaml");
        if (!access(path, R_OK))
            strcpy(next.dictionary, path);
        else
            next.dictionary[0] = 0;
    }
    char unused[512];
    const char* preferred = *c->language_code ? c->language_code : "zh";
    next.language_code[0] = 0;
    next.language = 0;
    if (!dse_engine_language(&next, preferred, unused, sizeof(unused)) &&
        !dse_engine_language(&next, "zh", unused, sizeof(unused)))
        dse_engine_cycle_language(&next, unused, sizeof(unused));
    const char* base = strrchr(folder, '/');
    snprintf(next.name, sizeof(next.name), "%s", base ? base + 1 : folder);
    char embedding[128];
    snprintf(embedding, sizeof(embedding), "acoustic/%s.emb", next.speaker);
    join(path, sizeof(path), next.bundle, embedding);
    if (access(path, R_OK))
        pick_speaker(&next, "", next.speaker);
    if (!dse_engine_check(&next, msg, n))
        return 0;
    *c = next;
    return 1;
}
int dse_engine_check(const DseEngineConfig* c, char* msg, size_t n) {
    if (!*c->bundle)
        return err(msg, n, "请先选择已打包的 DiffSinger 声库");
    const char* files[] = {"acoustic/fs2_acoustic.dsfs",
                           "acoustic/aux_convnext.dsa",
                           "acoustic/lynxnet2.dsn",
                           "acoustic/model.conf",
                           "acoustic/phonemes.json",
                           "vocoder/384.dsv35",
                           "vocoder/384.json"};
    char path[DSE_PATH];
    for (size_t i = 0; i < sizeof(files) / sizeof(*files); i++)
        if (!join(path, sizeof(path), c->bundle, files[i]) || access(path, R_OK)) {
            snprintf(msg, n, "声库缺少 %s", files[i]);
            return 0;
        }
    const char* bins[] = {"build/dsasm-acoustic", "build/dsasm-vocoder-m40"};
    for (unsigned i = 0; i < 2; i++)
        if (!join(path, sizeof(path), c->engine, bins[i]) || access(path, X_OK)) {
            snprintf(msg, n, "引擎未构建：%s", bins[i]);
            return 0;
        }
    if (!c->steps || c->steps > 100 || !c->workers || c->workers > 64 || !isfinite(c->depth) ||
        c->depth <= 0 || c->depth > 1)
        return err(msg, n, "引擎参数无效");
    join(path, sizeof(path), c->bundle, "acoustic/model.conf");
    FILE* file = fopen(path, "r");
    unsigned rate = 0, hop = 0, mel = 0, flags = 0;
    char line[4096];
    if (!file)
        return err(msg, n, "无法读取模型配置");
    while (fgets(line, sizeof(line), file)) {
        sscanf(line, "sample_rate=%u", &rate);
        sscanf(line, "hop_size=%u", &hop);
        sscanf(line, "mel_bins=%u", &mel);
        sscanf(line, "feature_flags=%u", &flags);
    }
    fclose(file);
    if (rate != 44100 || hop != 512 || mel != 128)
        return err(msg, n, "当前 CLI 适配要求 44100 Hz / hop 512 / 128 mel");
    if (flags & 14u) {
        const char* variance_files[] = {"linguistic.dsling", "lynxnet2.dsn", "condition.dsvc",
                                        "phonemes.json", "languages.json"};
        char root[DSE_PATH];
        if (!*c->bank || !join(root, sizeof(root), c->bank, "dsasm/variance"))
            return err(msg, n, "此声库需要表现参数预测，请选择原始声库目录");
        for (unsigned i = 0; i < sizeof(variance_files) / sizeof(*variance_files); i++)
            if (!join(path, sizeof(path), root, variance_files[i]) || access(path, R_OK)) {
                snprintf(msg, n, "声库缺少原生表现预测模型 %s，请运行 variance 模型转换",
                         variance_files[i]);
                return 0;
            }
        char embedding[128];
        snprintf(embedding, sizeof(embedding), "%s.emb", c->speaker);
        if (!join(path, sizeof(path), root, embedding) || access(path, R_OK))
            return err(msg, n, "表现预测模型缺少所选声线的 .emb 文件");
        if (!join(path, sizeof(path), c->engine, "build/dsasm-variance") || access(path, X_OK))
            return err(msg, n, "引擎未构建：build/dsasm-variance");
    }
    if (strchr(c->speaker, '/') || strstr(c->speaker, ".."))
        return err(msg, n, "声线名称无效");
    if (flags & (1u << 6)) {
        char embedding[128];
        snprintf(embedding, sizeof(embedding), "acoustic/%s.emb", c->speaker);
        if (!join(path, sizeof(path), c->bundle, embedding) || access(path, R_OK))
            return err(msg, n, "声库缺少所选声线的 .emb 文件");
    }
    join(path, sizeof(path), c->bundle, "vocoder/384.json");
    json_object* v = json_object_from_file(path);
    int valid = v && json_object_get_int(get(v, "frames")) == 384 &&
                json_object_get_int(get(v, "samples")) == 196608 &&
                json_object_get_int(get(v, "mel_bins")) == 128;
    if (v)
        json_object_put(v);
    if (!valid)
        return err(msg, n, "声码器元数据与 384 帧契约不一致");
    return 1;
}
static int spawn_job(DseRender* r, char* const argv[], const char* log) {
    if (atomic_load(&r->cancel))
        return err(r->message, sizeof(r->message), "合成已取消");
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, log, O_WRONLY | O_CREAT | O_APPEND,
                                     0600);
    posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);
    pid_t pid;
    int rc = posix_spawn(&pid, argv[0], &actions, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    if (rc) {
        snprintf(r->message, sizeof(r->message), "无法启动引擎：%s", strerror(rc));
        return 0;
    }
    struct timespec delay = {0, 20000000};
    unsigned polls = 0;
    int status = 0;
    for (;;) {
        pid_t got = waitpid(pid, &status, WNOHANG);
        if (got == pid)
            break;
        if (got < 0 && errno != EINTR) {
            kill(pid, SIGTERM);
            waitpid(pid, NULL, 0);
            return err(r->message, sizeof(r->message), "引擎等待失败");
        }
        if (atomic_load(&r->cancel) || ++polls > 45000) {
            kill(pid, SIGTERM);
            for (unsigned k = 0; k < 50; k++) {
                if (waitpid(pid, &status, WNOHANG) == pid)
                    goto cancelled;
                nanosleep(&delay, NULL);
            }
            kill(pid, SIGKILL);
            while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
            }
        cancelled:
            return err(r->message, sizeof(r->message),
                       atomic_load(&r->cancel) ? "合成已取消" : "引擎超时（15 分钟）");
        }
        nanosleep(&delay, NULL);
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        snprintf(r->message, sizeof(r->message),
                 "DiffSinger 合成失败（状态 %d），详见任务目录 engine.log", status);
        return 0;
    }
    return 1;
}
static int write_raw(const char* path, const float* data, size_t count) {
    FILE* f = fopen(path, "wb");
    if (!f)
        return 0;
    int ok = fwrite(data, sizeof(float), count, f) == count;
    if (fclose(f))
        ok = 0;
    return ok;
}
static float* read_raw(const char* path, size_t count) {
    struct stat st;
    if (stat(path, &st) || (uint64_t)st.st_size != count * sizeof(float))
        return NULL;
    FILE* f = fopen(path, "rb");
    if (!f)
        return NULL;
    float* v = malloc(count * sizeof(float));
    if (v && fread(v, sizeof(float), count, f) != count) {
        free(v);
        v = NULL;
    }
    fclose(f);
    return v;
}
static int prepare(DseRender* r, unsigned part, DseFrontend* frontend, unsigned* frames) {
    DsePartPlan plan;
    if (!dse_frontend_part(frontend, &r->score, part, &plan, r->message, sizeof(r->message)))
        return 0;
    *frames = plan.frames;
    if (!*frames) {
        dse_part_plan_clear(&plan);
        return 1;
    }
    char tokens[DSE_PATH], durations[DSE_PATH], languages[DSE_PATH], symbols[DSE_PATH],
        path[DSE_PATH];
    join(tokens, sizeof(tokens), r->work, "tokens.txt");
    join(durations, sizeof(durations), r->work, "durations.txt");
    join(languages, sizeof(languages), r->work, "languages.txt");
    join(symbols, sizeof(symbols), r->work, "phonemes.txt");
    FILE *tok = fopen(tokens, "wb"), *dur = fopen(durations, "wb"), *lang = fopen(languages, "wb"),
         *sym = fopen(symbols, "wb");
    int ok = tok && dur && lang && sym;
    for (unsigned i = 0; ok && i < plan.count; i++) {
        const DsePhone* phone = &plan.phones[i];
        ok = fprintf(tok, "%d\n", phone->token) > 0 && fprintf(dur, "%d\n", phone->frames) > 0 &&
             fprintf(lang, "%d\n", phone->language) > 0 && fprintf(sym, "%s\n", phone->symbol) > 0;
    }
    if (requires_variance(&r->config)) {
        char variance[DSE_PATH], vt[DSE_PATH], vl[DSE_PATH], wd[DSE_PATH], wt[DSE_PATH];
        join(variance, sizeof(variance), r->config.bank, "dsasm/variance");
        join(path, sizeof(path), variance, "phonemes.json");
        json_object* vocab = json_object_from_file(path);
        join(path, sizeof(path), variance, "languages.json");
        json_object* lang_ids = json_object_from_file(path);
        join(vt, sizeof(vt), r->work, "variance-tokens.txt");
        join(vl, sizeof(vl), r->work, "variance-languages.txt");
        join(wd, sizeof(wd), r->work, "word-div.txt");
        join(wt, sizeof(wt), r->work, "word-dur.txt");
        FILE *vtoken = fopen(vt, "wb"), *vlang = fopen(vl, "wb"), *word_div = fopen(wd, "wb"),
             *word_dur = fopen(wt, "wb");
        ok = ok && vocab && lang_ids && vtoken && vlang && word_div && word_dur;
        unsigned begin = 0;
        int sum = 0;
        for (unsigned i = 0; ok && i < plan.count; i++) {
            const DsePhone* phone = &plan.phones[i];
            json_object* id = get(vocab, phone->symbol);
            char code[64];
            const char* slash = strchr(phone->symbol, '/');
            size_t len = slash ? (size_t)(slash - phone->symbol) : strlen(r->config.language_code);
            if (len >= sizeof(code)) {
                ok = 0;
                break;
            }
            memcpy(code, slash ? phone->symbol : r->config.language_code, len);
            code[len] = 0;
            json_object* lid = get(lang_ids, code);
            int silence = !strcmp(phone->symbol, "SP") || !strcmp(phone->symbol, "AP") ||
                          !strcmp(phone->symbol, "EP");
            if (!json_object_is_type(id, json_type_int) || (!silence && !lid)) {
                snprintf(r->message, sizeof(r->message), "表现预测模型不支持音素 %s / 语言 %s",
                         phone->symbol, code);
                ok = 0;
                break;
            }
            ok = fprintf(vtoken, "%d\n", json_object_get_int(id)) > 0 &&
                 fprintf(vlang, "%d\n", silence ? 0 : json_object_get_int(lid)) > 0;
            /* Word-conditioned linguistic exports start each group at a vowel.
             * The initial consonants/silence form the preceding group. */
            if (phone->vowel && i > begin) {
                ok = ok && fprintf(word_div, "%u\n", i - begin) > 0 &&
                     fprintf(word_dur, "%d\n", sum) > 0;
                begin = i;
                sum = 0;
            }
            sum += phone->frames;
        }
        if (ok)
            ok = fprintf(word_div, "%u\n", plan.count - begin) > 0 &&
                 fprintf(word_dur, "%d\n", sum) > 0;
        if (vtoken && fclose(vtoken))
            ok = 0;
        if (vlang && fclose(vlang))
            ok = 0;
        if (word_div && fclose(word_div))
            ok = 0;
        if (word_dur && fclose(word_dur))
            ok = 0;
        if (vocab)
            json_object_put(vocab);
        if (lang_ids)
            json_object_put(lang_ids);
    }
    if (tok && fclose(tok))
        ok = 0;
    if (dur && fclose(dur))
        ok = 0;
    if (lang && fclose(lang))
        ok = 0;
    if (sym && fclose(sym))
        ok = 0;
    dse_part_plan_clear(&plan);
    if (!ok && !*r->message)
        return err(r->message, sizeof(r->message), "音素、时长或语言文件写入失败");
    if (!ok)
        return 0;
    float* f0 = malloc(*frames * sizeof(float));
    if (!f0)
        return err(r->message, sizeof(r->message), "F0 分配失败");
    dse_fill_f0(&r->score, (int)part, f0, *frames, 44100, 512);
    join(path, sizeof(path), r->work, "f0.f32");
    ok = write_raw(path, f0, *frames);
    join(path, sizeof(path), r->work, "f0.txt");
    FILE* f = fopen(path, "wb");
    if (!f)
        ok = 0;
    if (f) {
        for (unsigned i = 0; i < *frames; i++)
            if (fprintf(f, "%.9g\n", f0[i]) < 0)
                ok = 0;
        if (fclose(f))
            ok = 0;
    }
    join(path, sizeof(path), r->work, "variance-pitch.txt");
    FILE* pitch = fopen(path, "wb");
    float previous = 60;
    for (unsigned i = 0; i < *frames; i++)
        if (f0[i] > 0) {
            previous = 69 + 12 * log2f(f0[i] / 440);
            break;
        }
    if (!pitch)
        ok = 0;
    if (pitch) {
        for (unsigned i = 0; i < *frames; i++) {
            if (f0[i] > 0)
                previous = 69 + 12 * log2f(f0[i] / 440);
            if (fprintf(pitch, "%.9g\n", previous) < 0)
                ok = 0;
        }
        if (fclose(pitch))
            ok = 0;
    }
    free(f0);
    for (int kind = 1; ok && kind <= 5; kind++) {
        const char* names[] = {"",
                               "breathiness-control.txt",
                               "tension-control.txt",
                               "voicing-control.txt",
                               "gender.txt",
                               "velocity.txt"};
        join(path, sizeof(path), r->work, names[kind]);
        f = fopen(path, "wb");
        if (!f) {
            ok = 0;
            break;
        }
        for (unsigned i = 0; i < *frames; i++) {
            float tick = (float)((double)i * 512 * DSE_PPQ * r->score.bpm / (60 * 44100));
            float value = dse_curve_value(&r->score, (int)part, kind, tick, kind == 5 ? 1 : 0);
            if (fprintf(f, "%.9g\n", value) < 0)
                ok = 0;
        }
        if (fclose(f))
            ok = 0;
    }
    if (!ok)
        return err(r->message, sizeof(r->message), "F0 或参数文件写入失败");
    return 1;
}
static int predict_variance(DseRender* r, unsigned part, unsigned frames) {
    if (!requires_variance(&r->config))
        return 1;
    char exe[DSE_PATH], model[DSE_PATH], tokens[DSE_PATH], languages[DSE_PATH], durations[DSE_PATH],
        pitch[DSE_PATH], words[DSE_PATH], word_dur[DSE_PATH], speaker[DSE_PATH], log[DSE_PATH],
        raw[DSE_PATH];
    join(exe, sizeof(exe), r->config.engine, "build/dsasm-variance");
    join(model, sizeof(model), r->config.bank, "dsasm/variance");
    char embedding[128];
    snprintf(embedding, sizeof(embedding), "%s.emb", r->config.speaker);
    join(speaker, sizeof(speaker), model, embedding);
    join(tokens, sizeof(tokens), r->work, "variance-tokens.txt");
    join(languages, sizeof(languages), r->work, "variance-languages.txt");
    join(durations, sizeof(durations), r->work, "durations.txt");
    join(pitch, sizeof(pitch), r->work, "variance-pitch.txt");
    join(words, sizeof(words), r->work, "word-div.txt");
    join(word_dur, sizeof(word_dur), r->work, "word-dur.txt");
    join(log, sizeof(log), r->work, "engine.log");
    char workers[32];
    snprintf(workers, sizeof(workers), "%u", r->config.workers);
    char* args[] = {exe,       "infer",       model,     "--tokens",      tokens,  "--languages",
                    languages, "--durations", durations, "--pitch",       pitch,   "--word-div",
                    words,     "--word-dur",  word_dur,  "--speaker-emb", speaker, "--out",
                    r->work,   "--steps",     "20",      "--threads",     workers, "--seed",
                    "42",      NULL};
    if (!spawn_job(r, args, log))
        return 0;
    join(raw, sizeof(raw), r->work, "predicted-variance.f32");
    float* predicted = read_raw(raw, (size_t)frames * 3);
    if (!predicted)
        return err(r->message, sizeof(r->message), "表现预测参数尺寸错误");
    const char* names[] = {"breathiness.txt", "voicing.txt", "tension.txt"};
    const int kinds[] = {1, 3, 2};
    int ok = 1;
    for (unsigned k = 0; ok && k < 3; k++) {
        join(raw, sizeof(raw), r->work, names[k]);
        FILE* f = fopen(raw, "wb");
        if (!f) {
            ok = 0;
            break;
        }
        for (unsigned i = 0; ok && i < frames; i++) {
            float tick = (float)((double)i * 512 * DSE_PPQ * r->score.bpm / (60 * 44100));
            float adjustment = dse_curve_value(&r->score, (int)part, kinds[k], tick, 0);
            float value = predicted[(size_t)i * 3 + k] + adjustment;
            float lo = k == 2 ? -10 : -96, hi = k == 2 ? 10 : 0;
            if (!isfinite(value)) {
                ok = 0;
                break;
            }
            value = fmaxf(lo, fminf(hi, value));
            ok = fprintf(f, "%.9g\n", value) > 0;
        }
        if (fclose(f))
            ok = 0;
    }
    free(predicted);
    return ok ? 1 : err(r->message, sizeof(r->message), "表现参数曲线写入失败");
}
static int render_part(DseRender* r, unsigned part, DseFrontend* frontend, float** pcm,
                       size_t* samples) {
    unsigned frames;
    if (!prepare(r, part, frontend, &frames))
        return 0;
    if (!frames) {
        *pcm = NULL;
        *samples = 0;
        return 1;
    }
    if (!predict_variance(r, part, frames))
        return 0;
    char exe[DSE_PATH], model[DSE_PATH], vocoder[DSE_PATH], tok[DSE_PATH], dur[DSE_PATH],
        f0txt[DSE_PATH], melp[DSE_PATH], f0p[DSE_PATH], wavep[DSE_PATH], log[DSE_PATH],
        spk[DSE_PATH];
    char breath[DSE_PATH], tension[DSE_PATH], voice[DSE_PATH], gender[DSE_PATH], velocity[DSE_PATH],
        languages[DSE_PATH];
#define WORK(v, name) join(v, sizeof(v), r->work, name)
    WORK(tok, "tokens.txt");
    WORK(dur, "durations.txt");
    WORK(f0txt, "f0.txt");
    WORK(melp, "mel.f32");
    WORK(f0p, "f0.f32");
    WORK(wavep, "block-wave.f32");
    WORK(log, "engine.log");
    WORK(breath, "breathiness.txt");
    WORK(tension, "tension.txt");
    WORK(voice, "voicing.txt");
    WORK(gender, "gender.txt");
    WORK(velocity, "velocity.txt");
    WORK(languages, "languages.txt");
    join(exe, sizeof(exe), r->config.engine, "build/dsasm-acoustic");
    join(model, sizeof(model), r->config.bundle, "acoustic");
    char steps[32], workers[32], depth[32];
    snprintf(steps, sizeof(steps), "%u", r->config.steps);
    snprintf(workers, sizeof(workers), "%u", r->config.workers);
    snprintf(depth, sizeof(depth), "%g", r->config.depth);
    char* args[48] = {exe,    "infer",      model,    "--tokens",    tok,       "--durations",
                      dur,    "--f0",       f0txt,    "--out",       melp,      "--steps",
                      steps,  "--depth",    depth,    "--threads",   workers,   "--breathiness",
                      breath, "--tension",  tension,  "--voicing",   voice,     "--gender",
                      gender, "--velocity", velocity, "--languages", languages, "--seed",
                      "42",   NULL};
    char embedding[128];
    snprintf(embedding, sizeof(embedding), "acoustic/%s.emb", r->config.speaker);
    join(spk, sizeof(spk), r->config.bundle, embedding);
    if (!access(spk, R_OK)) {
        args[31] = "--speaker-emb";
        args[32] = spk;
        args[33] = NULL;
    }
    if (!spawn_job(r, args, log))
        return 0;
    float *mel = read_raw(melp, (size_t)frames * 128), *f0 = read_raw(f0p, frames);
    if (!mel || !f0) {
        free(mel);
        free(f0);
        return err(r->message, sizeof(r->message), "引擎输出 mel/F0 的尺寸错误");
    }
    float *wave = calloc((size_t)frames * 512, sizeof(float)),
          *weights = calloc((size_t)frames * 512, sizeof(float));
    float *block = malloc(384 * 128 * sizeof(float)), *blockf0 = malloc(384 * sizeof(float));
    int ok = wave && weights && block && blockf0;
    char blockmel[DSE_PATH], blockpitch[DSE_PATH];
    WORK(blockmel, "block-mel.f32");
    WORK(blockpitch, "block-f0.f32");
    join(exe, sizeof(exe), r->config.engine, "build/dsasm-vocoder-m40");
    join(vocoder, sizeof(vocoder), r->config.bundle, "vocoder/384.dsv35");
    unsigned blocks = frames <= 384 ? 1 : (frames - 384 + 319) / 320 + 1;
    for (unsigned b = 0; ok && b < blocks; b++) {
        if (atomic_load(&r->cancel)) {
            ok = err(r->message, sizeof(r->message), "合成已取消");
            break;
        }
        unsigned start = b * 320, valid = frames - start;
        if (valid > 384)
            valid = 384;
        for (unsigned i = 0; i < 384; i++) {
            blockf0[i] = i < valid ? f0[start + i] : 0;
            for (unsigned m = 0; m < 128; m++)
                block[(size_t)i * 128 + m] = i < valid ? mel[(size_t)(start + i) * 128 + m] : -12;
        }
        if (!write_raw(blockmel, block, 384 * 128) || !write_raw(blockpitch, blockf0, 384)) {
            ok = err(r->message, sizeof(r->message), "声码器张量写入失败");
            break;
        }
        char* vocargs[] = {exe,     "infer", vocoder,     "--mel", blockmel,   "--f0", blockpitch,
                           "--out", wavep,   "--workers", workers, "--rounds", "1",    NULL};
        if (!spawn_job(r, vocargs, log)) {
            ok = 0;
            break;
        }
        float* out = read_raw(wavep, 384 * 512);
        if (!out) {
            ok = err(r->message, sizeof(r->message), "声码器 PCM 尺寸错误");
            break;
        }
        for (unsigned i = 0; i < valid * 512; i++) {
            float w = 1;
            if (b && i < 64 * 512)
                w = (float)(i + 1) / (64 * 512);
            if (b + 1 < blocks && i >= 320 * 512)
                w = (float)(384 * 512 - i) / (64 * 512);
            size_t dst = (size_t)start * 512 + i;
            wave[dst] += out[i] * w;
            weights[dst] += w;
        }
        free(out);
        atomic_store(&r->progress,
                     10 + (int)((part + (b + 1.0) / blocks) * 85 / r->score.part_count));
    }
    if (ok)
        for (size_t i = 0; i < (size_t)frames * 512; i++) {
            wave[i] = weights[i] ? wave[i] / weights[i] : 0;
            if (!isfinite(wave[i])) {
                ok = err(r->message, sizeof(r->message), "引擎输出了非有限 PCM");
                break;
            }
        }
    free(weights);
    free(block);
    free(blockf0);
    free(mel);
    free(f0);
    if (!ok) {
        free(wave);
        if (!*r->message)
            err(r->message, sizeof(r->message), "声码器内存分配失败");
        return 0;
    }
    *pcm = wave;
    *samples = (size_t)frames * 512;
    return 1;
#undef WORK
}
int dse_render_wav(const char* path, const float* pcm, size_t frames, unsigned rate, char* msg,
                   size_t n) {
    if (frames > (UINT32_MAX - 44) / 4)
        return err(msg, n, "WAV 长度超过 RIFF 限制");
    size_t bytes = strlen(path) + 16;
    char* tmp = malloc(bytes);
    if (!tmp)
        return err(msg, n, "WAV 路径分配失败");
    snprintf(tmp, bytes, "%s.tmp.XXXXXX", path);
    int fd = mkstemp(tmp);
    FILE* f = fd >= 0 ? fdopen(fd, "wb") : NULL;
    int ok = f != NULL;
    uint32_t data = (uint32_t)frames * 4, riff = 36 + data, size = 16, byte_rate = rate * 4;
    uint16_t format = 1, channels = 2, align = 4, bits = 16;
#define PUT(v)                                                                                     \
    do {                                                                                           \
        if (fwrite(&(v), sizeof(v), 1, f) != 1)                                                    \
            ok = 0;                                                                                \
    } while (0)
    if (f) {
        ok = fwrite("RIFF", 1, 4, f) == 4;
        PUT(riff);
        if (fwrite("WAVEfmt ", 1, 8, f) != 8)
            ok = 0;
        PUT(size);
        PUT(format);
        PUT(channels);
        PUT(rate);
        PUT(byte_rate);
        PUT(align);
        PUT(bits);
        if (fwrite("data", 1, 4, f) != 4)
            ok = 0;
        PUT(data);
        for (size_t i = 0; i < frames * 2; i++) {
            float value = fmaxf(-1, fminf(1, pcm[i]));
            int16_t sample = (int16_t)lrintf(value * 32767);
            PUT(sample);
        }
        if (fflush(f) || fsync(fd))
            ok = 0;
        if (fclose(f))
            ok = 0;
        if (ok && rename(tmp, path))
            ok = 0;
    } else if (fd >= 0)
        close(fd);
    if (!ok)
        unlink(tmp);
    free(tmp);
    if (!ok)
        return err(msg, n, "WAV 写入失败，原文件已保留");
    return 1;
#undef PUT
}
int dse_render_run(DseRender* r) {
    r->message[0] = 0;
    r->pcm = NULL;
    r->samples = 0;
    r->sample_rate = 44100;
    r->success = 0;
    if (!dse_score_validate(&r->score, r->message, sizeof(r->message)))
        goto done;
    size_t capacity = 0;
    int anysolo = 0, has_voice = 0;
    for (unsigned t = 0; t < r->score.track_count; t++)
        anysolo |= r->score.tracks[t].solo;
    for (unsigned i = 0; i < r->score.count; i++) {
        const DseNote* note = &r->score.notes[i];
        const DseTrack* track = &r->score.tracks[r->score.parts[note->part].track];
        has_voice |= !track->mute && (!anysolo || track->solo);
    }
    if (r->audio_only || !has_voice)
        goto audio;
    if (!dse_engine_check(&r->config, r->message, sizeof(r->message)))
        goto done;
    strcpy(r->work, "/tmp/diffsinger-render-XXXXXX");
    if (!mkdtemp(r->work)) {
        err(r->message, sizeof(r->message), "无法建立渲染任务目录");
        goto done;
    }
    DseFrontend* frontend =
        dse_frontend_open_cancel(&r->config, &r->cancel, r->message, sizeof(r->message));
    if (!frontend)
        goto done;
    /* Check every audible part before starting any expensive inference. */
    for (unsigned p = 0; p < r->score.part_count; p++) {
        const DseTrack* track = &r->score.tracks[r->score.parts[p].track];
        if (track->mute || (anysolo && !track->solo))
            continue;
        DsePartPlan plan;
        if (atomic_load(&r->cancel)) {
            dse_frontend_close(frontend);
            err(r->message, sizeof(r->message), "合成已取消");
            goto done;
        }
        if (!dse_frontend_part(frontend, &r->score, p, &plan, r->message, sizeof(r->message))) {
            dse_frontend_close(frontend);
            goto done;
        }
        dse_part_plan_clear(&plan);
    }
    for (unsigned p = 0; p < r->score.part_count; p++) {
        const DsePart* part = &r->score.parts[p];
        const DseTrack* track = &r->score.tracks[part->track];
        if (track->mute || (anysolo && !track->solo))
            continue;
        float* wave = NULL;
        size_t frames = 0;
        if (!render_part(r, p, frontend, &wave, &frames)) {
            free(wave);
            dse_frontend_close(frontend);
            goto done;
        }
        if (!frames)
            continue;
        size_t offset = (size_t)lround((double)part->pos * 60 * 44100 / (r->score.bpm * DSE_PPQ)),
               end = offset + frames;
        if (end > 44100 * 60 * 30) {
            free(wave);
            dse_frontend_close(frontend);
            err(r->message, sizeof(r->message), "工程输出最长支持 30 分钟");
            goto done;
        }
        if (end > capacity) {
            float* next = realloc(r->pcm, end * 2 * sizeof(float));
            if (!next) {
                free(wave);
                dse_frontend_close(frontend);
                err(r->message, sizeof(r->message), "混音内存分配失败");
                goto done;
            }
            r->pcm = next;
            memset(next + capacity * 2, 0, (end - capacity) * 2 * sizeof(float));
            capacity = end;
        }
        float gain = powf(10, track->gain / 20), left = gain * (1 - fmaxf(0, track->pan)),
              right = gain * (1 + fminf(0, track->pan));
        for (size_t i = 0; i < frames; i++) {
            r->pcm[(offset + i) * 2] += wave[i] * left;
            r->pcm[(offset + i) * 2 + 1] += wave[i] * right;
        }
        free(wave);
        r->samples = capacity;
    }
    dse_frontend_close(frontend);
audio:
    if (!dse_audio_mix(r, &capacity))
        goto done;
    if (!r->samples) {
        err(r->message, sizeof(r->message), "没有可播放的音符或音频，请检查静音和独奏状态");
        goto done;
    }
    for (size_t i = 0; i < r->samples * 2; i++)
        if (!isfinite(r->pcm[i])) {
            err(r->message, sizeof(r->message), "混音产生无效采样值，请检查源音频与增益");
            goto done;
        }
    if (atomic_load(&r->cancel)) {
        err(r->message, sizeof(r->message), "合成已取消");
        goto done;
    }
    if (*r->output &&
        (!dse_audio_destination(&r->score, r->output, r->message, sizeof(r->message)) ||
         !dse_render_wav(r->output, r->pcm, r->samples, 44100, r->message, sizeof(r->message))))
        goto done;
    r->success = 1;
    atomic_store(&r->progress, 100);
    snprintf(r->message, sizeof(r->message), "%s · %.2f 秒 · 44100 Hz",
             r->audio_only ? "伴奏准备完成" : "工程混音完成", r->samples / 44100.0);
done:
    if (!r->success) {
        free(r->pcm);
        r->pcm = NULL;
        r->samples = 0;
    }
    atomic_store_explicit(&r->done, 1, memory_order_release);
    return r->success;
}
