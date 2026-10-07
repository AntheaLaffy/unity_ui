#define _POSIX_C_SOURCE 200809L
#include "audio.h"
#include "project.h"
#include <errno.h>
#include <json-c/json.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <yaml.h>

#define PI 3.14159265358979323846

int dse_path_is_ustx(const char* path) {
    const char* dot = strrchr(path, '.');
    return dot && !strcasecmp(dot, ".ustx");
}
void dse_project_info_clear(DseProjectInfo* p) {
    if (p) {
        free(p->ustx);
        memset(p, 0, sizeof(*p));
    }
}
void dse_project_notice(const DseProjectInfo* p, char* msg, size_t bytes) {
    snprintf(msg, bytes,
             "USTX 已导入 · %u 个音频片段可试听 · %u 条参数未应用 / %u 个音素表情待适配",
             p->wave_parts, p->unsupported_curves, p->expressions + p->overrides);
}
static int err(char* msg, size_t n, const char* text) {
    snprintf(msg, n, "%s", text);
    return 0;
}
static json_object* get(json_object* o, const char* key) {
    json_object* v = NULL;
    if (json_object_is_type(o, json_type_object))
        json_object_object_get_ex(o, key, &v);
    return v;
}
static size_t len(json_object* o) {
    return json_object_is_type(o, json_type_array) ? json_object_array_length(o) : 0;
}
static const char* text(json_object* o, const char* k, const char* fallback) {
    json_object* v = get(o, k);
    return json_object_is_type(v, json_type_string) ? json_object_get_string(v) : fallback;
}
static double number(json_object* o, const char* k, double fallback) {
    json_object* v = get(o, k);
    if (!v)
        return fallback;
    const char* s = json_object_get_string(v);
    char* end;
    errno = 0;
    double x = s ? strtod(s, &end) : NAN;
    return !s || !*s || *end || errno || !isfinite(x) ? NAN : x;
}
static int boolean(json_object* o, const char* k, int fallback) {
    const char* s = text(o, k, fallback ? "true" : "false");
    return !strcasecmp(s, "true") ? 1 : !strcasecmp(s, "false") ? 0 : -1;
}
static int copy(char* dst, size_t size, const char* src) {
    if (strlen(src) >= size)
        return 0;
    memcpy(dst, src, strlen(src) + 1);
    return 1;
}
static int whole(double x, int low, int high) {
    return isfinite(x) && x >= low && x <= high && x == floor(x);
}
/* Scalars stay strings: a Japanese lyric "no" must never become YAML 1.1 false.
 * The immutable source text is retained independently for unmodeled fields. */
static json_object* convert(yaml_document_t* doc, int id, unsigned char* seen, unsigned* budget,
                            unsigned depth) {
    yaml_node_t* node = yaml_document_get_node(doc, id);
    if (!node || depth > 64 || !*budget || seen[id])
        return NULL;
    seen[id] = 1;
    --*budget;
    if (node->type == YAML_SCALAR_NODE)
        return json_object_new_string_len((const char*)node->data.scalar.value,
                                          (int)node->data.scalar.length);
    json_object* out = node->type == YAML_SEQUENCE_NODE  ? json_object_new_array()
                       : node->type == YAML_MAPPING_NODE ? json_object_new_object()
                                                         : NULL;
    if (!out)
        return NULL;
    if (node->type == YAML_SEQUENCE_NODE) {
        for (yaml_node_item_t* i = node->data.sequence.items.start;
             i < node->data.sequence.items.top; i++) {
            json_object* child = convert(doc, *i, seen, budget, depth + 1);
            if (!child)
                goto fail;
            json_object_array_add(out, child);
        }
    } else {
        for (yaml_node_pair_t* p = node->data.mapping.pairs.start; p < node->data.mapping.pairs.top;
             p++) {
            yaml_node_t* key = yaml_document_get_node(doc, p->key);
            json_object* duplicate;
            if (!key || key->type != YAML_SCALAR_NODE || key->data.scalar.length > 255 ||
                json_object_object_get_ex(out, (const char*)key->data.scalar.value, &duplicate))
                goto fail;
            json_object* child = convert(doc, p->value, seen, budget, depth + 1);
            if (!child)
                goto fail;
            json_object_object_add(out, (const char*)key->data.scalar.value, child);
        }
    }
    return out;
fail:
    json_object_put(out);
    return NULL;
}
static int curve_read(json_object* curve, int** xs, float** ys, size_t* count) {
    json_object *x = get(curve, "xs"), *y = get(curve, "ys");
    *count = len(x);
    *xs = NULL;
    *ys = NULL;
    if (!json_object_is_type(x, json_type_array) || !json_object_is_type(y, json_type_array) ||
        *count != len(y) || *count > DSE_MAX_CURVES)
        return 0;
    if (!*count)
        return 1;
    *xs = malloc(*count * sizeof(**xs));
    *ys = malloc(*count * sizeof(**ys));
    if (!*xs || !*ys)
        return 0;
    for (size_t i = 0; i < *count; i++) {
        json_object* pair = json_object_new_object();
        json_object_object_add(pair, "x", json_object_get(json_object_array_get_idx(x, i)));
        json_object_object_add(pair, "y", json_object_get(json_object_array_get_idx(y, i)));
        double a = number(pair, "x", NAN), b = number(pair, "y", NAN);
        json_object_put(pair);
        if (!whole(a, -DSE_TICK_LIMIT, DSE_TICK_LIMIT) || !isfinite(b) || fabs(b) > 100000 ||
            (i && a <= (*xs)[i - 1]))
            return 0;
        (*xs)[i] = (int)a;
        (*ys)[i] = (float)b;
    }
    return 1;
}
static float sample(const int* xs, const float* ys, size_t count, int tick, float fallback) {
    size_t lo = 0, hi = count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (xs[mid] < tick)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo < count && xs[lo] == tick)
        return ys[lo];
    if (!lo || lo == count)
        return fallback;
    return (float)nearbyint(ys[lo - 1] + (ys[lo] - ys[lo - 1]) * (tick - xs[lo - 1]) /
                                             (double)(xs[lo] - xs[lo - 1]));
}
static int append(DseScore* s, unsigned part, unsigned kind, int tick, float value) {
    if (s->curve_count == DSE_MAX_CURVES || !isfinite(value) ||
        (kind == 0 && (value < 0 || value > 127)))
        return 0;
    s->curves[s->curve_count++] = (DseCurve){(int)part, tick, value, kind};
    return 1;
}
typedef struct Point {
    double x, y;
    int shape;
} Point;
static double ease(double x, int shape) {
    return shape == 1   ? (1 - cos(PI * x)) / 2
           : shape == 2 ? 1 - cos(PI * x / 2)
           : shape == 3 ? sin(PI * x / 2)
                        : x;
}
static int pitch_part(DseScore* s, unsigned part, json_object* notes, const int* xs,
                      const float* ys, size_t count, char* msg, size_t bytes) {
    int end = 0;
    for (size_t i = 0; i < len(notes); i++) {
        json_object* n = json_object_array_get_idx(notes, i);
        int e = (int)number(n, "position", 0) + (int)number(n, "duration", 0);
        if (e > end)
            end = e;
    }
    if (!end)
        return 1;
    if (end * 60.0 / (s->bpm * DSE_PPQ) > 600)
        return err(msg, bytes, "歌声片段超过 10 分钟的当前合成边界");
    size_t frames = (size_t)(end + 4) / 5 + 1;
    float* dense = malloc(frames * sizeof(*dense));
    if (!dense)
        return err(msg, bytes, "USTX 音高内存分配失败");
    size_t cursor = 0;
    for (size_t i = 0; i < len(notes); i++) {
        json_object* n = json_object_array_get_idx(notes, i);
        int stop = (int)number(n, "position", 0) + (int)number(n, "duration", 0);
        float key = (float)number(n, "tone", 60) + (float)number(n, "tuning", 0) / 100;
        while (cursor < frames && (int)cursor * 5 < stop)
            dense[cursor++] = key;
    }
    while (cursor < frames) {
        dense[cursor] = cursor ? dense[cursor - 1] : 60;
        cursor++;
    }
    int ok = 1;
    for (size_t i = 0; ok && i < len(notes); i++) {
        json_object *n = json_object_array_get_idx(notes, i), *pitch = get(n, "pitch"),
                    *data = get(pitch, "data"),
                    *prev = i ? json_object_array_get_idx(notes, i - 1) : NULL;
        double pos = number(n, "position", 0), duration = number(n, "duration", 0),
               tone = number(n, "tone", 60) + number(n, "tuning", 0) / 100,
               prev_end = prev ? number(prev, "position", 0) + number(prev, "duration", 0) : -1,
               prev_tone = prev ? number(prev, "tone", 60) + number(prev, "tuning", 0) / 100 : tone;
        size_t size = len(data);
        if (size > 4096) {
            ok = err(msg, bytes, "USTX 音符音高控制点过多");
            break;
        }
        Point* points = calloc(size + 2, sizeof(*points));
        if (!points) {
            ok = 0;
            break;
        }
        for (size_t j = 0; ok && j < size; j++) {
            json_object* p = json_object_array_get_idx(data, j);
            const char* shape = text(p, "shape", "io");
            double x = number(p, "x", NAN), y = number(p, "y", NAN);
            int kind = !strcmp(shape, "io") || (!strcmp(shape, "sp") && size <= 2) ? 1
                       : !strcmp(shape, "i")                                       ? 2
                       : !strcmp(shape, "o")                                       ? 3
                       : !strcmp(shape, "l")                                       ? 0
                                                                                   : -1;
            if (!isfinite(x) || fabs(x) > 600000 || !isfinite(y) || fabs(y) > 1270 || kind < 0) {
                ok = err(msg, bytes, "USTX 音高坐标无效或使用未支持的多点样条");
                break;
            }
            if (!j && boolean(pitch, "snap_first", 1) == 1)
                y = prev_end == pos ? (prev_tone - tone) * 10 : 0;
            points[j] = (Point){nearbyint(pos + x * s->bpm * DSE_PPQ / 60000), tone + y / 10, kind};
            if (j && points[j].x < points[j - 1].x)
                ok = err(msg, bytes, "USTX 音高控制点顺序无效");
        }
        if (!size) {
            points[0] = (Point){pos, tone, 1};
            points[1] = (Point){pos + duration, tone, 1};
            size = 2;
        } else if (points[size - 1].x < pos + duration) {
            points[size] = (Point){pos + duration, points[size - 1].y, 1};
            size++;
        }
        if (size == 1)
            ok = err(msg, bytes, "USTX 单点音高曲线缺少有效结束点");
        if (ok) {
            long start = (long)floor(points[0].x / 5);
            if (start < 0)
                start = 0;
            size_t seg = 0;
            for (size_t t = (size_t)start; t < frames && t * 5 < points[size - 1].x; t++) {
                double x = (double)t * 5;
                while (seg + 1 < size - 1 && points[seg + 1].x <= x)
                    seg++;
                Point a = points[seg], b = points[seg + 1];
                double blend = b.x - a.x < .001 ? 1 : fmax(0, fmin(1, (x - a.x) / (b.x - a.x)));
                double value = a.y + (b.y - a.y) * ease(blend, a.shape);
                dense[t] += (float)(value - (prev && x < prev_end ? prev_tone : tone));
            }
        }
        free(points);
    }
    for (size_t i = 0; ok && i < frames; i++)
        ok = append(s, part, 0, (int)i * 5, dense[i] + sample(xs, ys, count, (int)i * 5, 0) / 100);
    free(dense);
    return ok ? 1 : *msg ? 0 : err(msg, bytes, "USTX 音高容量或数值超出当前范围");
}
int dse_ustx_load(DseScore* out, DseProjectInfo* info, const char* path, char* msg, size_t bytes) {
    struct stat st;
    if (stat(path, &st) || st.st_size < 1 || st.st_size > 16 * 1024 * 1024)
        return err(msg, bytes, "USTX 无法读取或超过 16 MiB");
    FILE* file = fopen(path, "rb");
    char* source = calloc((size_t)st.st_size + 1, 1);
    if (!file || !source) {
        if (file)
            fclose(file);
        free(source);
        return err(msg, bytes, "USTX 无法读取");
    }
    size_t read = fread(source, 1, (size_t)st.st_size, file);
    int valid = read == (size_t)st.st_size && !ferror(file) && !memchr(source, 0, read);
    fclose(file);
    yaml_parser_t parser;
    yaml_document_t document;
    if (!valid || !yaml_parser_initialize(&parser)) {
        free(source);
        return err(msg, bytes, "USTX 读取失败");
    }
    yaml_parser_set_input_string(&parser, (const unsigned char*)source, read);
    valid = yaml_parser_load(&parser, &document);
    if (!valid) {
        snprintf(msg, bytes, "USTX YAML 无效：行 %zu，列 %zu", parser.problem_mark.line + 1,
                 parser.problem_mark.column + 1);
        yaml_parser_delete(&parser);
        free(source);
        return 0;
    }
    size_t nodes = (size_t)(document.nodes.top - document.nodes.start);
    unsigned char* seen = calloc(nodes + 1, 1);
    unsigned budget = 1000000;
    json_object* root = seen ? convert(&document, 1, seen, &budget, 0) : NULL;
    free(seen);
    yaml_document_delete(&document);
    yaml_document_t tail;
    int last = yaml_parser_load(&parser, &tail);
    if (last) {
        if (yaml_document_get_root_node(&tail))
            valid = 0;
        yaml_document_delete(&tail);
    } else
        valid = 0;
    yaml_parser_delete(&parser);
    DseScore* s = calloc(1, sizeof(*s));
    DseProjectInfo next = {.ustx = source};
    int ok = 0;
    if (bytes)
        *msg = 0;
#define FAIL(value)                                                                                \
    do {                                                                                           \
        err(msg, bytes, value);                                                                    \
        goto done;                                                                                 \
    } while (0)
    if (!valid || !root || !s || !json_object_is_type(root, json_type_object))
        FAIL("USTX YAML 无效；重复字段、别名或多文档不受支持");
    if (!copy(next.source, sizeof(next.source), path))
        FAIL("USTX 路径过长");
    const char* version = text(root, "ustx_version", "");
    if (strcmp(version, "0.7") && strcmp(version, "0.8") && strcmp(version, "0.9"))
        FAIL("当前支持 USTX 0.7 / 0.8 / 0.9");
    if (number(root, "resolution", 480) != 480)
        FAIL("当前 USTX 导入要求 480 PPQ");
    json_object *tempos = get(root, "tempos"), *signatures = get(root, "time_signatures"),
                *tracks = get(root, "tracks"), *parts = get(root, "voice_parts");
    if (len(tempos) != 1 || number(json_object_array_get_idx(tempos, 0), "position", -1) != 0 ||
        len(signatures) != 1 ||
        number(json_object_array_get_idx(signatures, 0), "bar_position", -1) != 0)
        FAIL("当前 USTX 导入支持从零开始的恒定速度和单一拍号");
    s->bpm = (float)number(json_object_array_get_idx(tempos, 0), "bpm", NAN);
    if (!isfinite(s->bpm) || s->bpm < 20 || s->bpm > 400)
        FAIL("USTX 速度超出 20 到 400 BPM 的范围");
    double top = number(json_object_array_get_idx(signatures, 0), "beat_per_bar", NAN),
           bottom = number(json_object_array_get_idx(signatures, 0), "beat_unit", NAN);
    if (!whole(top, 1, 16) || !whole(bottom, 1, 16))
        FAIL("USTX 拍号无效");
    s->numerator = (unsigned)top;
    s->denominator = (unsigned)bottom;
    if (s->denominator & (s->denominator - 1))
        FAIL("USTX 拍号分母必须为二的幂");
    if (!len(tracks) || len(tracks) > DSE_MAX_TRACKS || !len(parts) || len(parts) > DSE_MAX_PARTS)
        FAIL("USTX 超出 8 条音轨 / 32 个歌声片段的容量，或没有歌声片段");
    s->track_count = (unsigned)len(tracks);
    s->next_id = 1;
    next.wave_parts = (unsigned)len(get(root, "wave_parts"));
    if (next.wave_parts > DSE_MAX_WAVES)
        FAIL("USTX 超出 64 个音频片段容量");
    for (unsigned t = 0; t < s->track_count; t++) {
        json_object* tr = json_object_array_get_idx(tracks, t);
        DseTrack* track = &s->tracks[t];
        if (!copy(track->name, sizeof(track->name), text(tr, "track_name", "Vocal")))
            FAIL("USTX 音轨名称超过 63 字节");
        track->color = 0x68d9c0ff;
        track->gain = (float)number(tr, "volume", 0);
        track->pan = (float)number(tr, "pan", 0) / 100;
        int mute = boolean(tr, "mute", 0), solo = boolean(tr, "solo", 0);
        if (mute < 0 || solo < 0)
            FAIL("USTX 音轨静音 / 独奏字段无效");
        track->mute = (unsigned)mute;
        track->solo = (unsigned)solo;
    }
    for (unsigned p = 0; p < len(parts); p++) {
        json_object *part = json_object_array_get_idx(parts, p), *notes = get(part, "notes"),
                    *curves = get(part, "curves");
        double track = number(part, "track_no", NAN), pos = number(part, "position", NAN),
               duration = number(part, "duration", NAN);
        if (!whole(track, 0, (int)s->track_count - 1) || !whole(pos, 0, DSE_TICK_LIMIT) ||
            !whole(duration, 1, DSE_TICK_LIMIT) || !json_object_is_type(notes, json_type_array))
            FAIL("USTX 歌声片段字段无效");
        DsePart* dest = &s->parts[s->part_count++];
        *dest = (DsePart){.track = (int)track, .pos = (int)pos, .len = (int)duration};
        if (!copy(dest->name, sizeof(dest->name), text(part, "name", "Voice")))
            FAIL("USTX 片段名称超过 63 字节");
        json_object* tr = json_object_array_get_idx(tracks, (size_t)track);
        const char *singer = text(tr, "singer", ""), *phonemizer = text(tr, "phonemizer", "");
        if (*singer) {
            if (*next.singer && strcmp(next.singer, singer))
                next.mixed_singers = 1;
            else if (!copy(next.singer, sizeof(next.singer), singer))
                FAIL("USTX 声库名称过长");
        }
        if (!*next.phonemizer && !copy(next.phonemizer, sizeof(next.phonemizer), phonemizer))
            FAIL("USTX 音素器名称过长");
        int last_position = -1;
        for (size_t i = 0; i < len(notes); i++) {
            json_object* note = json_object_array_get_idx(notes, i);
            double x = number(note, "position", NAN), length = number(note, "duration", NAN),
                   key = number(note, "tone", NAN), tuning = number(note, "tuning", 0);
            if (!whole(x, 0, DSE_TICK_LIMIT) || !whole(length, 1, DSE_TICK_LIMIT) ||
                !whole(key, 0, 127) || x + length > DSE_TICK_LIMIT || x < last_position ||
                !isfinite(tuning) || fabs(tuning) > 1200)
                FAIL("USTX 音符字段或顺序无效");
            last_position = (int)x;
            int index = dse_add_note(s, (int)p, (int)x, (int)length, (int)key);
            if (index < 0)
                FAIL("USTX 超出 2048 个音符的容量");
            DseNote* n = &s->notes[index];
            n->selected = 0;
            n->phonemes[0] = 0;
            if (!copy(n->lyric, sizeof(n->lyric), text(note, "lyric", "")))
                FAIL("USTX 歌词超过 63 字节");
            json_object* vibrato = get(note, "vibrato");
            if (number(vibrato, "length", 0) != 0)
                next.unsupported_curves++;
            next.overrides += (unsigned)len(get(note, "phoneme_overrides"));
            json_object* expressions = get(note, "phoneme_expressions");
            for (size_t j = 0; j < len(expressions); j++) {
                json_object* exp = json_object_array_get_idx(expressions, j);
                const char* abbr = text(exp, "abbr", "");
                json_object* descriptor = get(get(root, "expressions"), abbr);
                if (number(exp, "value", NAN) != number(descriptor, "default_value", 0))
                    next.expressions++;
            }
            if ((int)x + (int)length > dest->len)
                dest->len = (int)x + (int)length;
        }
        int* xs = NULL;
        float* ys = NULL;
        size_t count = 0;
        json_object* pitd = NULL;
        for (size_t c = 0; c < len(curves); c++) {
            json_object* curve = json_object_array_get_idx(curves, c);
            const char* abbr = text(curve, "abbr", "");
            if (!strcmp(abbr, "pitd")) {
                if (pitd)
                    FAIL("USTX 音高偏移曲线重复");
                pitd = curve;
            } else if (len(get(curve, "xs")))
                next.unsupported_curves++;
        }
        if (pitd && !curve_read(pitd, &xs, &ys, &count)) {
            free(xs);
            free(ys);
            FAIL("USTX 音高偏移曲线的坐标、长度或顺序无效");
        }
        int pitch_ok = pitch_part(s, p, notes, xs, ys, count, msg, bytes);
        free(xs);
        free(ys);
        if (!pitch_ok)
            goto done;
    }
    json_object* waves = get(root, "wave_parts");
    for (unsigned w = 0; w < len(waves); w++) {
        json_object* part = json_object_array_get_idx(waves, w);
        DseWave* wave = &s->waves[s->wave_count++];
        double track = number(part, "track_no", NAN);
        if (!whole(track, 0, (int)s->track_count - 1) ||
            !copy(wave->name, sizeof(wave->name), text(part, "name", "Audio")) ||
            !dse_audio_path(wave->path, sizeof(wave->path), path, text(part, "relative_path", "")))
            FAIL("USTX 音频片段名称、音轨或路径无效");
        wave->track = (int)track;
        wave->duration_ms = number(part, "file_duration_ms", NAN);
        const char* keys[] = {"position", "skip", "trim", "fadein", "fadeout"};
        int32_t* fields[] = {&wave->pos, &wave->skip, &wave->trim, &wave->fade_in, &wave->fade_out};
        for (unsigned k = 0; k < 5; k++) {
            double value = number(part, keys[k], 0);
            if (!whole(value, 0, DSE_TICK_LIMIT))
                FAIL("USTX 音频片段位置或裁剪无效");
            *fields[k] = (int)value;
        }
    }
    dse_curve_sort(s);
    if (!dse_score_validate(s, msg, bytes))
        goto done;
    s->revision = out->revision + 1;
    *out = *s;
    if (info) {
        dse_project_info_clear(info);
        *info = next;
        memset(&next, 0, sizeof(next));
        dse_project_notice(info, msg, bytes);
    } else
        dse_project_notice(&next, msg, bytes);
    ok = 1;
done:
    dse_project_info_clear(&next);
    free(s);
    if (root)
        json_object_put(root);
    return ok;
#undef FAIL
}
void dse_engine_import(DseEngineConfig* config, const DseProjectInfo* info) {
    if (!info->ustx)
        return;
    config->bundle[0] = 0;
    config->dictionary[0] = 0;
    config->bank[0] = 0;
    config->language_code[0] = 0;
    config->language = 0;
    snprintf(config->name, sizeof(config->name), "%s", "请为导入工程选择声库");
    if (!*info->singer || info->mixed_singers || strchr(info->singer, '/') ||
        strstr(info->singer, ".."))
        return;
    char folder[DSE_PATH], msg[512];
    snprintf(folder, sizeof(folder), "/home/fuurin/.local/share/OpenUtau/Singers/%s", info->singer);
    if (dse_engine_resolve(config, folder, msg, sizeof(msg))) {
        const char* code =
            strstr(info->phonemizer, "Japanese")                                            ? "ja"
            : strstr(info->phonemizer, "English")                                           ? "en"
            : strstr(info->phonemizer, "Cantonese") || strstr(info->phonemizer, "Jyutping") ? "yue"
                                                                                            : "zh";
        if (!dse_engine_language(config, code, msg, sizeof(msg))) {
            config->bundle[0] = 0;
            snprintf(config->name, sizeof(config->name), "请选择支持 %s 的声库", code);
        }
    }
}
