#define _POSIX_C_SOURCE 200809L
#include "project.h"
#include "audio.h"
#include "editor.h"
#include "engine.h"
#include <errno.h>
#include <json-c/json.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int error(char* msg, size_t n, const char* s) {
    snprintf(msg, n, "%s", s);
    return 0;
}
static json_object* get(json_object* o, const char* key) {
    json_object* v = NULL;
    if (!json_object_is_type(o, json_type_object))
        return NULL;
    json_object_object_get_ex(o, key, &v);
    return v;
}
static size_t alen(json_object* o) {
    return json_object_is_type(o, json_type_array) ? json_object_array_length(o) : 0;
}
static size_t olen(json_object* o) {
    return json_object_is_type(o, json_type_object) ? json_object_object_length(o) : 0;
}
static double num(json_object* o, const char* key, double fallback) {
    json_object* v = get(o, key);
    return !v ? fallback
           : json_object_is_type(v, json_type_int) || json_object_is_type(v, json_type_double)
               ? json_object_get_double(v)
               : NAN;
}
static const char* str(json_object* o, const char* key, const char* fallback) {
    json_object* v = get(o, key);
    return v ? json_object_get_string(v) : fallback;
}
static int text_copy(char* out, size_t n, const char* s) {
    if (!s || strlen(s) >= n)
        return 0;
    memcpy(out, s, strlen(s) + 1);
    return 1;
}
static int integer(double x) {
    return isfinite(x) && x >= 0 && x <= DSE_TICK_LIMIT && x == floor(x);
}
static const char* lanes[] = {"pitch", "breathiness", "tension", "voicing", "gender", "velocity"};
void dse_score_init(DseScore* s, int demo) {
    memset(s, 0, sizeof(*s));
    s->next_id = 1;
    s->bpm = 120;
    s->numerator = s->denominator = 4;
    s->track_count = s->part_count = 1;
    strcpy(s->tracks[0].name, "主唱 · Vocal");
    s->tracks[0].color = 0x68d9c0ff;
    strcpy(s->parts[0].name, "第一段旋律");
    s->parts[0].len = 7680;
    if (demo) {
        const int keys[] = {60, 62, 64, 67, 67, 65, 64, 62, 60, 64, 67, 69};
        const char* words[] = {"让", "世", "界", "听", "见", "你",
                               "的", "声", "音", "啊", "啊", "啊"};
        for (unsigned i = 0; i < 12; i++) {
            int j = dse_add_note(s, 0, (int)i * 480, i == 11 ? 960 : 420, keys[i]);
            strcpy(s->notes[j].lyric, words[i]);
            s->notes[j].phonemes[0] = 0;
            s->notes[j].selected = 0;
        }
        dse_curve_set(s, 0, 0, 1920, 67);
        dse_curve_set(s, 0, 0, 2160, 67.25f);
        dse_curve_set(s, 0, 0, 2340, 67);
    }
    s->revision = 1;
    s->curve_sorted = 1;
}
static int curve_order(const void* a, const void* b) {
    const DseCurve *x = a, *y = b;
    if (x->part != y->part)
        return (x->part > y->part) - (x->part < y->part);
    if (x->kind != y->kind)
        return (x->kind > y->kind) - (x->kind < y->kind);
    return (x->tick > y->tick) - (x->tick < y->tick);
}
void dse_curve_sort(DseScore* s) {
    qsort(s->curves, s->curve_count, sizeof(*s->curves), curve_order);
    s->curve_sorted = 1;
}
const DseTiming* dse_timing_get(const DseScore* s, const DseNote* note) {
    return note->timing_slot && note->timing_slot <= DSE_MAX_NOTES
               ? &s->timings[note->timing_slot - 1]
               : NULL;
}
int dse_timing_valid(const DseTiming* timing, int len) {
    if (!timing->count || timing->count > DSE_MAX_PHONEMES || !timing->signature)
        return 0;
    int64_t sum = 0;
    unsigned automatic = 0;
    for (unsigned i = 0; i < timing->count; i++) {
        if (timing->ticks[i] < 0 || timing->ticks[i] > DSE_TICK_LIMIT)
            return 0;
        automatic += !timing->ticks[i];
        sum += timing->ticks[i];
    }
    return automatic == 1 && sum < len;
}
int dse_timing_set(DseScore* s, unsigned index, const DseTiming* timing) {
    if (index >= s->count ||
        (timing && timing->count && !dse_timing_valid(timing, s->notes[index].len)))
        return 0;
    DseNote* note = &s->notes[index];
    if (!timing || !timing->count) {
        if (note->timing_slot) {
            note->timing_slot = 0;
            s->revision++;
        }
        return 1;
    }
    unsigned used[DSE_MAX_NOTES] = {0};
    for (unsigned i = 0; i < s->count; i++)
        if (s->notes[i].timing_slot && s->notes[i].timing_slot <= DSE_MAX_NOTES)
            used[s->notes[i].timing_slot - 1]++;
    unsigned slot = note->timing_slot;
    if (!slot || slot > DSE_MAX_NOTES || used[slot - 1] > 1) {
        for (slot = 1; slot <= DSE_MAX_NOTES; slot++)
            if (!used[slot - 1])
                break;
    }
    if (slot > DSE_MAX_NOTES)
        return 0;
    if (note->timing_slot != slot || memcmp(&s->timings[slot - 1], timing, sizeof(*timing))) {
        s->timings[slot - 1] = *timing;
        note->timing_slot = slot;
        s->revision++;
    }
    return 1;
}
int dse_curve_set(DseScore* s, int part, int kind, int tick, float value) {
    if (part < 0 || (unsigned)part >= s->part_count || kind < 0 || kind > 5 || tick < 0 ||
        tick > DSE_TICK_LIMIT || !isfinite(value))
        return 0;
    if (!s->curve_sorted)
        dse_curve_sort(s);
    DseCurve point = {part, tick, value, (unsigned)kind};
    unsigned lo = 0, hi = s->curve_count;
    while (lo < hi) {
        unsigned mid = lo + (hi - lo) / 2;
        if (curve_order(&s->curves[mid], &point) < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo < s->curve_count && !curve_order(&s->curves[lo], &point)) {
        s->curves[lo].value = value;
        s->revision++;
        return 1;
    }
    if (s->curve_count == DSE_MAX_CURVES)
        return 0;
    memmove(s->curves + lo + 1, s->curves + lo, (s->curve_count - lo) * sizeof(*s->curves));
    s->curves[lo] = point;
    s->curve_count++;
    s->revision++;
    return 1;
}
int dse_score_validate(const DseScore* s, char* msg, size_t n) {
    if (!s->track_count || s->track_count > DSE_MAX_TRACKS || !s->part_count ||
        s->part_count > DSE_MAX_PARTS || s->count > DSE_MAX_NOTES ||
        s->curve_count > DSE_MAX_CURVES || s->wave_count > DSE_MAX_WAVES)
        return error(msg, n, "工程容量超出限制");
    if (!isfinite(s->bpm) || s->bpm < 20 || s->bpm > 400 || !s->numerator || s->numerator > 16 ||
        !s->denominator || s->denominator > 16 || (s->denominator & (s->denominator - 1)))
        return error(msg, n, "速度或拍号无效");
    for (unsigned i = 0; i < s->track_count; i++)
        if (!memchr(s->tracks[i].name, 0, 64) || !isfinite(s->tracks[i].gain) ||
            fabsf(s->tracks[i].gain) > 60 || !isfinite(s->tracks[i].pan) ||
            fabsf(s->tracks[i].pan) > 1)
            return error(msg, n, "音轨字段无效");
    for (unsigned i = 0; i < s->part_count; i++)
        if (s->parts[i].track < 0 || (unsigned)s->parts[i].track >= s->track_count ||
            s->parts[i].pos < 0 || s->parts[i].len < 1 ||
            (int64_t)s->parts[i].pos + s->parts[i].len > DSE_TICK_LIMIT ||
            !memchr(s->parts[i].name, 0, 64))
            return error(msg, n, "片段位置或音轨无效");
    for (unsigned i = 0; i < s->count; i++) {
        const DseNote* v = &s->notes[i];
        if (v->part < 0 || (unsigned)v->part >= s->part_count || v->pos < 0 || v->len < 1 ||
            v->key < 0 || v->key > 127 || (int64_t)v->pos + v->len > DSE_TICK_LIMIT ||
            !memchr(v->lyric, 0, 64) || !memchr(v->phonemes, 0, 64))
            return error(msg, n, "音符字段无效");
        if (v->timing_slot > DSE_MAX_NOTES ||
            (v->timing_slot && !dse_timing_valid(dse_timing_get(s, v), v->len)))
            return error(msg, n, "手工音素时长无效或超过音符时长");
    }
    for (unsigned i = 0; i < s->curve_count; i++) {
        const DseCurve* v = &s->curves[i];
        if (v->part < 0 || (unsigned)v->part >= s->part_count || v->tick < 0 ||
            v->tick > DSE_TICK_LIMIT || v->kind > 5 || !isfinite(v->value) ||
            (v->kind == 0 && (v->value < 0 || v->value > 127)))
            return error(msg, n, "参数曲线无效");
    }
    for (unsigned i = 0; i < s->wave_count; i++) {
        const DseWave* wave = &s->waves[i];
        if (!memchr(wave->name, 0, sizeof(wave->name)) ||
            !memchr(wave->path, 0, sizeof(wave->path)) || wave->path[0] != '/' || wave->track < 0 ||
            (unsigned)wave->track >= s->track_count || wave->pos < 0 ||
            wave->pos > DSE_TICK_LIMIT || wave->skip < 0 || wave->trim < 0 || wave->fade_in < 0 ||
            wave->fade_out < 0 || wave->skip > DSE_TICK_LIMIT || wave->trim > DSE_TICK_LIMIT ||
            wave->fade_in > DSE_TICK_LIMIT || wave->fade_out > DSE_TICK_LIMIT ||
            !isfinite(wave->duration_ms) || wave->duration_ms <= 0 ||
            wave->duration_ms > 30 * 60 * 1000 || !isfinite(wave->gain) || fabsf(wave->gain) > 60 ||
            dse_wave_ticks(wave, s->bpm) <= 0 ||
            wave->pos + dse_wave_ticks(wave, s->bpm) > DSE_TICK_LIMIT)
            return error(msg, n, "音频片段路径、位置、时长或裁剪无效");
    }
    return 1;
}
static int load_tlp(DseScore* out, const char* path, char* msg, size_t n) {
    struct stat st;
    if (stat(path, &st) || st.st_size > 64 * 1024 * 1024)
        return error(msg, n, "无法读取工程，或文件超过 64 MiB");
    json_object* root = json_object_from_file(path);
    if (!root)
        return error(msg, n, "工程 JSON 无效");
    if (!json_object_is_type(root, json_type_object)) {
        json_object_put(root);
        return error(msg, n, "工程根节点必须是对象");
    }
    DseScore* s = calloc(1, sizeof(*s));
    int ok = 0;
    if (!s) {
        json_object_put(root);
        return error(msg, n, "内存分配失败");
    }
#define FAIL(s)                                                                                    \
    do {                                                                                           \
        error(msg, n, (s));                                                                        \
        goto done;                                                                                 \
    } while (0)
    json_object *tempos = get(root, "tempos"), *ts = get(root, "timeSignatures"),
                *tracks = get(root, "tracks");
    if (num(root, "version", -1) != 0 && num(root, "version", -1) != 1)
        FAIL("仅支持 TLP JSON 版本 0 / 1");
    if (!json_object_is_type(tempos, json_type_array) || alen(tempos) != 1 ||
        num(json_object_array_get_idx(tempos, 0), "pos", -1) != 0)
        FAIL("当前仅支持从 tick 0 开始的单一速度");
    if (!json_object_is_type(ts, json_type_array) || alen(ts) != 1 ||
        num(json_object_array_get_idx(ts, 0), "barIndex", -1) != 0)
        FAIL("当前仅支持单一拍号");
    s->bpm = (float)num(json_object_array_get_idx(tempos, 0), "bpm", 0);
    double numerator = num(json_object_array_get_idx(ts, 0), "numerator", 0),
           denominator = num(json_object_array_get_idx(ts, 0), "denominator", 0);
    if (!integer(numerator) || !integer(denominator) || numerator > 16 || denominator > 16)
        FAIL("拍号必须为 1 到 16 之间的整数");
    s->numerator = (uint32_t)numerator;
    s->denominator = (uint32_t)denominator;
    s->next_id = 1;
    if (!json_object_is_type(tracks, json_type_array) || alen(tracks) > DSE_MAX_TRACKS)
        FAIL("最多支持 8 条音轨");
    s->track_count = (unsigned)alen(tracks);
    for (unsigned t = 0; t < s->track_count; t++) {
        json_object *track = json_object_array_get_idx(tracks, t), *parts = get(track, "parts");
        DseTrack* tr = &s->tracks[t];
        if (!text_copy(tr->name, 64, str(track, "name", "Vocal")))
            FAIL("音轨名称超过 63 字节");
        tr->gain = (float)num(track, "gain", 0);
        tr->pan = (float)num(track, "pan", 0);
        tr->mute = json_object_get_boolean(get(track, "mute"));
        tr->solo = json_object_get_boolean(get(track, "solo"));
        tr->color = 0x68d9c0ff;
        json_object* color = get(track, "color");
        if (color) {
            const char* hex = json_object_get_string(color);
            char* end;
            if (!json_object_is_type(color, json_type_string) || strlen(hex) != 7 || *hex != '#')
                FAIL("音轨颜色必须为 #RRGGBB");
            unsigned long rgb = strtoul(hex + 1, &end, 16);
            if (*end || rgb > 0xffffff)
                FAIL("音轨颜色必须为 #RRGGBB");
            tr->color = (uint32_t)(rgb << 8) | 255;
        }
        if (!json_object_is_type(parts, json_type_array))
            FAIL("缺少片段列表");
        for (size_t p = 0; p < alen(parts); p++) {
            json_object *part = json_object_array_get_idx(parts, p), *notes = get(part, "notes"),
                        *pitch = get(part, "pitch");
            if (!strcmp(str(part, "type", ""), "audio")) {
                if (s->wave_count == DSE_MAX_WAVES)
                    FAIL("最多支持 64 个音频片段");
                DseWave* wave = &s->waves[s->wave_count++];
                wave->track = (int)t;
                const char* file = str(part, "file", "");
                if (!text_copy(wave->name, sizeof(wave->name), str(part, "name", "Audio")) ||
                    !dse_audio_path(wave->path, sizeof(wave->path), path, file))
                    FAIL("音频片段名称或路径无效");
                const char* keys[] = {"pos", "skip", "trim", "fadeIn", "fadeOut"};
                int32_t* fields[] = {&wave->pos, &wave->skip, &wave->trim, &wave->fade_in,
                                     &wave->fade_out};
                for (unsigned k = 0; k < 5; k++) {
                    double value = num(part, keys[k], 0);
                    if (!isfinite(value) || value < 0 || value > DSE_TICK_LIMIT ||
                        trunc(value) != value)
                        FAIL("音频片段位置或裁剪无效");
                    *fields[k] = (int)value;
                }
                wave->duration_ms = num(part, "durationMs", NAN);
                wave->gain = (float)num(part, "gain", 0);
                continue;
            }
            if (s->part_count == DSE_MAX_PARTS)
                FAIL("最多支持 32 个歌声片段");
            if (strcmp(str(part, "type", ""), "midi"))
                FAIL("未知片段类型；支持歌声 MIDI 和原生音频片段");
            if (num(part, "startOffset", 0) != 0 || num(part, "gain", 0) != 0 ||
                alen(get(part, "vibratos")) || alen(get(part, "effects")) ||
                olen(get(part, "piecewiseAutomations")) || olen(get(part, "properties")))
                FAIL("工程包含尚未支持的裁剪、片段增益、颤音、效果器或自定义属性");
            json_object* voice = get(part, "voice");
            const char* type = str(voice, "type", "");
            if (*type && strcmp(type, "DiffSinger-ASM"))
                FAIL("工程使用其他引擎；请先导出为仅含音符和曲线的 MIDI 片段");
            const char* voice_id = str(voice, "id", "");
            if (*voice_id && strcmp(voice_id, "local"))
                FAIL("当前每个工程使用一套本地声库；不支持逐片段声库配置");
            unsigned id = s->part_count++;
            DsePart* pa = &s->parts[id];
            pa->track = (int)t;
            double pos = num(part, "pos", 0), len = num(part, "endOffset", num(part, "dur", 7680));
            if (!integer(pos) || !integer(len) || len < 1)
                FAIL("片段必须使用非负整数 tick");
            pa->pos = (int)pos;
            pa->len = (int)len;
            if (!text_copy(pa->name, 64, str(part, "name", "Vocal part")))
                FAIL("片段名称超过 63 字节");
            if (!json_object_is_type(notes, json_type_array))
                FAIL("缺少音符列表");
            for (size_t j = 0; j < alen(notes); j++) {
                json_object* note = json_object_array_get_idx(notes, j);
                double np = num(note, "pos", -1), nd = num(note, "dur", -1),
                       nk = num(note, "pitch", -1);
                if (!integer(np) || !integer(nd) || !integer(nk) || nk > 127)
                    FAIL("音符必须使用整数 tick 和 MIDI 音高");
                if (np + nd > len)
                    FAIL("暂不支持片段末尾裁剪，请让 endOffset 覆盖全部音符");
                if (alen(get(note, "phonemes")) || alen(get(note, "leadingPhonemes")) ||
                    alen(get(note, "bodyPhonemes")) || olen(get(note, "properties")))
                    FAIL("暂不支持 TLP 钉死音素时长或音符自定义属性");
                int index = dse_add_note(s, (int)id, (int)np, (int)nd, (int)nk);
                if (index < 0)
                    FAIL("音符容量或时间范围超出限制");
                if (!text_copy(s->notes[index].lyric, 64, str(note, "lyric", "a")) ||
                    !text_copy(s->notes[index].phonemes, 64, str(note, "pronunciation", "")))
                    FAIL("歌词或音素超过 63 字节");
                s->notes[index].selected = 0;
                json_object* saved = get(note, "phonemeTiming");
                if (saved) {
                    json_object* ticks = get(saved, "ticks");
                    const char* hash = str(saved, "signature", "");
                    DseTiming timing = {0};
                    if (!json_object_is_type(saved, json_type_object) ||
                        !json_object_is_type(get(saved, "signature"), json_type_string) ||
                        !json_object_is_type(ticks, json_type_array) || !alen(ticks) ||
                        alen(ticks) > DSE_MAX_PHONEMES || strlen(hash) != 16)
                        FAIL("音素时长记录无效");
                    for (unsigned h = 0; h < 16; h++)
                        if (!((hash[h] >= '0' && hash[h] <= '9') ||
                              (hash[h] >= 'a' && hash[h] <= 'f')))
                            FAIL("音素时长签名无效");
                    timing.signature = strtoull(hash, NULL, 16);
                    timing.count = (unsigned)alen(ticks);
                    for (unsigned h = 0; h < timing.count; h++) {
                        json_object* value = json_object_array_get_idx(ticks, h);
                        int64_t tick = json_object_get_int64(value);
                        if (!json_object_is_type(value, json_type_int) || tick < 0 ||
                            tick > DSE_TICK_LIMIT)
                            FAIL("音素时长需为非负整数 tick");
                        timing.ticks[h] = (int)tick;
                    }
                    if (!dse_timing_set(s, (unsigned)index, &timing))
                        FAIL("音素时长需含一个自动余量，合计须小于音符时长");
                }
            }
            if (pitch) {
                if (!json_object_is_type(pitch, json_type_array) || alen(pitch) > 1)
                    FAIL("当前仅支持每片段一段连续音高曲线");
                if (alen(pitch)) {
                    json_object* line = json_object_array_get_idx(pitch, 0);
                    if (!json_object_is_type(line, json_type_array))
                        FAIL("音高曲线必须为坐标数组");
                    size_t count = alen(line);
                    if (count % 2)
                        FAIL("音高曲线坐标不成对");
                    for (size_t k = 0; k < count; k += 2) {
                        double tick = json_object_get_double(json_object_array_get_idx(line, k));
                        if (!integer(tick) ||
                            !dse_curve_set(s, (int)id, 0, (int)tick,
                                           (float)json_object_get_double(
                                               json_object_array_get_idx(line, k + 1))))
                            FAIL("音高曲线容量或坐标无效");
                    }
                }
            }
            json_object* autom = get(part, "automations");
            if (autom) {
                if (!json_object_is_type(autom, json_type_object))
                    FAIL("自动化字段必须是对象");
                json_object_object_foreach(autom, key, value) {
                    int kind = 0;
                    for (int k = 1; k <= 5; k++)
                        if (!strcmp(lanes[k], key))
                            kind = k;
                    if (!kind ||
                        num(value, "defaultValue", kind == 5 ? 1 : 0) != (kind == 5 ? 1 : 0))
                        FAIL("工程包含尚未支持的自动化参数或默认值");
                    json_object* points = get(value, "points");
                    if (!json_object_is_type(points, json_type_array))
                        FAIL("参数曲线必须为坐标数组");
                    size_t count = alen(points);
                    if (count % 2)
                        FAIL("参数曲线坐标不成对");
                    for (size_t k = 0; k < count; k += 2) {
                        double tick = json_object_get_double(json_object_array_get_idx(points, k));
                        if (!integer(tick) ||
                            !dse_curve_set(s, (int)id, kind, (int)tick,
                                           (float)json_object_get_double(
                                               json_object_array_get_idx(points, k + 1))))
                            FAIL("参数曲线容量或坐标无效");
                    }
                }
            }
        }
    }
    if (!dse_score_validate(s, msg, n))
        goto done;
    s->revision = out->revision + 1;
    *out = *s;
    ok = 1;
done:
    free(s);
    json_object_put(root);
    return ok;
#undef FAIL
}
static void addi(json_object* o, const char* k, int64_t v) {
    json_object_object_add(o, k, json_object_new_int64(v));
}
static void addf(json_object* o, const char* k, double v) {
    json_object_object_add(o, k, json_object_new_double(v));
}
static void adds(json_object* o, const char* k, const char* v) {
    json_object_object_add(o, k, json_object_new_string(v));
}
static int compare_curve(const void* a, const void* b) {
    const DseCurve *x = a, *y = b;
    return (x->tick > y->tick) - (x->tick < y->tick);
}
int dse_project_save(const DseScore* s, const DseProjectInfo* info, const char* path,
                     const DseEngineConfig* config, char* msg, size_t n) {
    if (dse_path_is_ustx(path))
        return error(msg, n, "USTX 是导入格式，请另存为 .dse 工程");
    if (!dse_score_validate(s, msg, n))
        return 0;
    if (!dse_audio_destination(s, path, msg, n))
        return 0;
    json_object *root = json_object_new_object(), *tempos = json_object_new_array(),
                *time = json_object_new_array(), *tracks = json_object_new_array();
    addi(root, "version", 1);
    adds(root, "application", "DiffSinger-ASM");
    json_object* v = json_object_new_object();
    addi(v, "pos", 0);
    addf(v, "bpm", s->bpm);
    json_object_array_add(tempos, v);
    json_object_object_add(root, "tempos", tempos);
    v = json_object_new_object();
    addi(v, "barIndex", 0);
    addi(v, "numerator", s->numerator);
    addi(v, "denominator", s->denominator);
    json_object_array_add(time, v);
    json_object_object_add(root, "timeSignatures", time);
    for (unsigned t = 0; t < s->track_count; t++) {
        json_object *track = json_object_new_object(), *parts = json_object_new_array();
        const DseTrack* tr = &s->tracks[t];
        adds(track, "name", tr->name);
        addf(track, "gain", tr->gain);
        addf(track, "pan", tr->pan);
        json_object_object_add(track, "mute", json_object_new_boolean(tr->mute));
        json_object_object_add(track, "solo", json_object_new_boolean(tr->solo));
        json_object_object_add(track, "asRefer", json_object_new_boolean(1));
        char color[16];
        snprintf(color, sizeof(color), "#%06x", tr->color >> 8);
        adds(track, "color", color);
        for (unsigned p = 0; p < s->part_count; p++)
            if (s->parts[p].track == (int)t) {
                json_object *part = json_object_new_object(), *notes = json_object_new_array(),
                            *pitch = json_object_new_array(), *autom = json_object_new_object();
                adds(part, "type", "midi");
                adds(part, "name", s->parts[p].name);
                addi(part, "pos", s->parts[p].pos);
                addi(part, "startOffset", 0);
                int end = s->parts[p].len;
                for (unsigned i = 0; i < s->count; i++)
                    if (s->notes[i].part == (int)p) {
                        const DseNote* note = &s->notes[i];
                        v = json_object_new_object();
                        addi(v, "pos", note->pos);
                        addi(v, "dur", note->len);
                        addi(v, "pitch", note->key);
                        adds(v, "lyric", note->lyric);
                        adds(v, "pronunciation", note->phonemes);
                        const DseTiming* timing = dse_timing_get(s, note);
                        if (timing) {
                            json_object *saved = json_object_new_object(),
                                        *ticks = json_object_new_array();
                            char hash[17];
                            snprintf(hash, sizeof(hash), "%016llx",
                                     (unsigned long long)timing->signature);
                            adds(saved, "signature", hash);
                            for (unsigned k = 0; k < timing->count; k++)
                                json_object_array_add(ticks, json_object_new_int(timing->ticks[k]));
                            json_object_object_add(saved, "ticks", ticks);
                            json_object_object_add(v, "phonemeTiming", saved);
                        }
                        json_object_object_add(v, "properties", json_object_new_object());
                        json_object_array_add(notes, v);
                        if (note->pos + note->len > end)
                            end = note->pos + note->len;
                    }
                addi(part, "endOffset", end);
                v = json_object_new_object();
                adds(v, "type", "DiffSinger-ASM");
                adds(v, "id", "local");
                adds(v, "kind", "voice");
                json_object_object_add(part, "voice", v);
                json_object_object_add(part, "notes", notes);
                json_object_object_add(part, "properties", json_object_new_object());
                for (int kind = 0; kind <= 5; kind++) {
                    DseCurve* points = malloc(s->curve_count * sizeof(*points));
                    size_t count = 0;
                    if (!points && s->curve_count) {
                        json_object_put(root);
                        json_object_put(track);
                        return error(msg, n, "曲线内存分配失败");
                    }
                    for (unsigned i = 0; i < s->curve_count; i++)
                        if (s->curves[i].part == (int)p && s->curves[i].kind == (unsigned)kind)
                            points[count++] = s->curves[i];
                    qsort(points, count, sizeof(*points), compare_curve);
                    json_object* line = json_object_new_array();
                    for (size_t i = 0; i < count; i++) {
                        json_object_array_add(line, json_object_new_int(points[i].tick));
                        json_object_array_add(line, json_object_new_double(points[i].value));
                    }
                    free(points);
                    if (kind == 0) {
                        if (count)
                            json_object_array_add(pitch, line);
                        else
                            json_object_put(line);
                    } else {
                        v = json_object_new_object();
                        addf(v, "defaultValue", kind == 5 ? 1 : 0);
                        json_object_object_add(v, "points", line);
                        json_object_object_add(autom, lanes[kind], v);
                    }
                }
                json_object_object_add(part, "pitch", pitch);
                json_object_object_add(part, "automations", autom);
                json_object_object_add(part, "vibratos", json_object_new_array());
                json_object_array_add(parts, part);
            }
        for (unsigned w = 0; w < s->wave_count; w++)
            if (s->waves[w].track == (int)t) {
                const DseWave* wave = &s->waves[w];
                json_object* part = json_object_new_object();
                adds(part, "type", "audio");
                adds(part, "name", wave->name);
                adds(part, "file", wave->path);
                addi(part, "pos", wave->pos);
                addi(part, "skip", wave->skip);
                addi(part, "trim", wave->trim);
                addi(part, "fadeIn", wave->fade_in);
                addi(part, "fadeOut", wave->fade_out);
                addf(part, "durationMs", wave->duration_ms);
                addf(part, "gain", wave->gain);
                json_object_array_add(parts, part);
            }
        json_object_object_add(track, "parts", parts);
        json_object_array_add(tracks, track);
    }
    json_object_object_add(root, "tracks", tracks);
    if (info && info->ustx) {
        json_object* origin = json_object_new_object();
        adds(origin, "source", info->source);
        adds(origin, "ustx", info->ustx);
        adds(origin, "singer", info->singer);
        adds(origin, "phonemizer", info->phonemizer);
        addi(origin, "waveParts", info->wave_parts);
        addi(origin, "unsupportedCurves", info->unsupported_curves);
        addi(origin, "expressions", info->expressions);
        addi(origin, "overrides", info->overrides);
        addi(origin, "mixedSingers", info->mixed_singers);
        json_object_object_add(root, "openutau", origin);
    }
    if (config) {
        json_object* settings = json_object_new_object();
        adds(settings, "singer", config->bundle);
        adds(settings, "dictionary", config->dictionary);
        adds(settings, "speaker", config->speaker);
        adds(settings, "name", config->name);
        adds(settings, "bank", config->bank);
        adds(settings, "languageCode", config->language_code);
        addi(settings, "steps", config->steps);
        addi(settings, "workers", config->workers);
        addi(settings, "language", config->language);
        addf(settings, "depth", config->depth);
        json_object_object_add(root, "asmui", settings);
    }
    size_t bytes = strlen(path) + 16;
    char* tmp = malloc(bytes);
    int ok = 0;
    if (tmp) {
        snprintf(tmp, bytes, "%s.tmp.XXXXXX", path);
        int fd = mkstemp(tmp);
        if (fd >= 0) {
            FILE* file = fdopen(fd, "wb");
            if (file) {
                const char* text = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PRETTY);
                ok = fputs(text, file) >= 0 && fputc('\n', file) != EOF && fflush(file) == 0 &&
                     fsync(fd) == 0;
                if (fclose(file))
                    ok = 0;
                if (ok && rename(tmp, path))
                    ok = 0;
            } else
                close(fd);
            if (!ok)
                unlink(tmp);
        }
        free(tmp);
    }
    json_object_put(root);
    if (!ok)
        return error(msg, n, "工程写入失败，原文件已保留");
    return 1;
}
int dse_score_save(const DseScore* s, const char* path, char* msg, size_t n) {
    return dse_score_save_config(s, path, NULL, msg, n);
}
int dse_score_save_config(const DseScore* s, const char* path, const DseEngineConfig* config,
                          char* msg, size_t n) {
    return dse_project_save(s, NULL, path, config, msg, n);
}
int dse_project_load(DseScore* out, DseProjectInfo* info, const char* path, char* msg, size_t n) {
    if (dse_path_is_ustx(path))
        return dse_ustx_load(out, info, path, msg, n);
    DseScore* staged = calloc(1, sizeof(*staged));
    DseProjectInfo next = {0};
    if (!staged)
        return error(msg, n, "内存分配失败");
    staged->revision = out->revision;
    int ok = load_tlp(staged, path, msg, n);
    if (ok && info) {
        json_object* root = json_object_from_file(path);
        json_object* origin = get(root, "openutau");
        if (origin) {
            json_object* text = get(origin, "ustx");
            ok = json_object_is_type(origin, json_type_object) &&
                 json_object_is_type(text, json_type_string) &&
                 json_object_get_string_len(text) <= 16 * 1024 * 1024 &&
                 text_copy(next.source, sizeof(next.source), str(origin, "source", "")) &&
                 text_copy(next.singer, sizeof(next.singer), str(origin, "singer", "")) &&
                 text_copy(next.phonemizer, sizeof(next.phonemizer), str(origin, "phonemizer", ""));
            if (ok) {
                next.ustx = strdup(json_object_get_string(text));
                ok = next.ustx != NULL;
                next.wave_parts = (unsigned)json_object_get_int(get(origin, "waveParts"));
                next.unsupported_curves =
                    (unsigned)json_object_get_int(get(origin, "unsupportedCurves"));
                next.expressions = (unsigned)json_object_get_int(get(origin, "expressions"));
                next.overrides = (unsigned)json_object_get_int(get(origin, "overrides"));
                next.mixed_singers = (unsigned)json_object_get_int(get(origin, "mixedSingers"));
            }
            if (!ok)
                error(msg, n, "OpenUtau 来源数据无效");
        }
        if (root)
            json_object_put(root);
    }
    if (ok) {
        *out = *staged;
        if (info) {
            dse_project_info_clear(info);
            *info = next;
            memset(&next, 0, sizeof(next));
            if (info->ustx)
                dse_project_notice(info, msg, n);
        }
    }
    dse_project_info_clear(&next);
    free(staged);
    return ok;
}
int dse_score_load(DseScore* out, const char* path, char* msg, size_t n) {
    return dse_project_load(out, NULL, path, msg, n);
}
