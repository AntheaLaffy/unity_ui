#define _XOPEN_SOURCE 700
#include "audio.h"
#include <math.h>
#include <samplerate.h>
#include <sndfile.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int error(char* msg, size_t n, const char* text) {
    snprintf(msg, n, "%s", text);
    return 0;
}
int dse_audio_path(char* out, size_t n, const char* project, const char* asset) {
    if (!asset || !*asset)
        return 0;
    char base[4096], joined[4096];
    if (*asset == '/') {
        if (strlen(asset) >= sizeof(joined))
            return 0;
        strcpy(joined, asset);
    } else {
        if (project && *project) {
            if (!realpath(project, base))
                return 0;
            char* slash = strrchr(base, '/');
            if (!slash)
                return 0;
            slash[1] = 0;
        } else {
            if (!getcwd(base, sizeof(base)))
                return 0;
            size_t len = strlen(base);
            if (len + 1 >= sizeof(base))
                return 0;
            base[len] = '/';
            base[len + 1] = 0;
        }
        if (snprintf(joined, sizeof(joined), "%s%s", base, asset) >= (int)sizeof(joined))
            return 0;
    }
    /* Missing media can still be opened and relinked in the editor. */
    const char* path = realpath(joined, base) ? base : joined;
    if (strlen(path) >= n)
        return 0;
    strcpy(out, path);
    return 1;
}
static int supported(const SF_INFO* info) {
    return info->frames > 0 && (info->channels == 1 || info->channels == 2) &&
           info->samplerate >= 8000 && info->samplerate <= 384000 &&
           (double)info->frames / info->samplerate <= 30 * 60;
}
int dse_audio_probe(DseWave* wave, const char* path, char* msg, size_t n) {
    char absolute[4096];
    if (!dse_audio_path(absolute, sizeof(absolute), NULL, path))
        return error(msg, n, "音频路径无效或过长");
    SF_INFO info = {0};
    SNDFILE* file = sf_open(absolute, SFM_READ, &info);
    if (!file) {
        snprintf(msg, n, "无法读取音频 %.160s：%.250s", path, sf_strerror(NULL));
        return 0;
    }
    sf_close(file);
    if (!supported(&info))
        return error(msg, n, "音频需为单声道或立体声，8 到 384 kHz，最长 30 分钟");
    DseWave next = *wave;
    strcpy(next.path, absolute);
    const char* name = strrchr(absolute, '/');
    name = name ? name + 1 : absolute;
    if (strlen(name) >= sizeof(next.name))
        return error(msg, n, "音频文件名称过长");
    strcpy(next.name, name);
    next.duration_ms = (double)info.frames * 1000 / info.samplerate;
    *wave = next;
    return 1;
}
double dse_wave_ticks(const DseWave* wave, float bpm) {
    return wave->duration_ms * bpm * DSE_PPQ / 60000 - wave->skip - wave->trim;
}
int dse_audio_destination(const DseScore* score, const char* path, char* msg, size_t n) {
    char absolute[4096];
    if (!dse_audio_path(absolute, sizeof(absolute), NULL, path))
        return error(msg, n, "保存路径无效或过长");
    struct stat output;
    int exists = stat(path, &output) == 0;
    for (unsigned w = 0; w < score->wave_count; w++) {
        struct stat input;
        if (!strcmp(absolute, score->waves[w].path) ||
            (exists && !stat(score->waves[w].path, &input) && input.st_dev == output.st_dev &&
             input.st_ino == output.st_ino))
            return error(msg, n, "保存路径是工程引用的原始音频，请选择新文件");
    }
    return 1;
}
static size_t tick_frames(int tick, double frames_per_tick) {
    return (size_t)((double)tick * frames_per_tick);
}
static int reserve(DseRender* r, size_t* capacity, size_t end) {
    if (end > DSE_AUDIO_LIMIT)
        return error(r->message, sizeof(r->message), "工程输出最长支持 30 分钟");
    if (end <= *capacity)
        return 1;
    float* next = realloc(r->pcm, end * 2 * sizeof(float));
    if (!next)
        return error(r->message, sizeof(r->message), "混音内存分配失败");
    memset(next + *capacity * 2, 0, (end - *capacity) * 2 * sizeof(float));
    r->pcm = next;
    *capacity = end;
    return 1;
}
static int mix_wave(DseRender* r, unsigned index, size_t* capacity) {
    const DseWave* wave = &r->score.waves[index];
    const DseTrack* track = &r->score.tracks[wave->track];
    SF_INFO info = {0};
    SNDFILE* file = sf_open(wave->path, SFM_READ, &info);
    if (!file) {
        snprintf(r->message, sizeof(r->message), "伴奏无法读取 %.180s：%.230s", wave->name,
                 sf_strerror(NULL));
        return 0;
    }
    int ok = 0, code = 0;
    SRC_STATE* converter = NULL;
    if (!supported(&info)) {
        error(r->message, sizeof(r->message), "伴奏格式、通道数、采样率或时长不受支持");
        goto done;
    }
    double native_tick = 60.0 * info.samplerate / (r->score.bpm * DSE_PPQ),
           output_tick = 60.0 * DSE_AUDIO_RATE / (r->score.bpm * DSE_PPQ);
    size_t skip = tick_frames(wave->skip, native_tick),
           trim = tick_frames(wave->trim, native_tick);
    if (skip + trim >= (size_t)info.frames) {
        error(r->message, sizeof(r->message),
              "音频裁剪超出实际文件时长，请重新链接或调整裁剪");
        goto done;
    }
    size_t remaining = (size_t)info.frames - skip - trim,
           total = (size_t)llround(remaining * (double)DSE_AUDIO_RATE / info.samplerate),
           offset = (size_t)llround(wave->pos * output_tick), written = 0,
           fade_in = tick_frames(wave->fade_in, output_tick),
           fade_out = tick_frames(wave->fade_out, output_tick);
    if (!total || !reserve(r, capacity, offset + total))
        goto done;
    if (sf_seek(file, (sf_count_t)skip, SEEK_SET) != (sf_count_t)skip) {
        error(r->message, sizeof(r->message), "伴奏裁剪定位失败");
        goto done;
    }
    if (info.samplerate != DSE_AUDIO_RATE) {
        converter = src_new(SRC_SINC_BEST_QUALITY, 2, &code);
        if (!converter) {
            snprintf(r->message, sizeof(r->message), "重采样初始化失败：%s",
                     src_strerror(code));
            goto done;
        }
    }
    float gain = powf(10, (track->gain + wave->gain) / 20),
          left = gain * (1 - fmaxf(0, track->pan)), right = gain * (1 + fminf(0, track->pan));
    float input[8192 * 2], stereo[8192 * 2], output[16384 * 2];
    for (;;) {
        if (atomic_load(&r->cancel)) {
            error(r->message, sizeof(r->message), "音频准备已取消");
            goto done;
        }
        size_t want = remaining > 8192 ? 8192 : remaining;
        sf_count_t got = sf_readf_float(file, input, (sf_count_t)want);
        if (got != (sf_count_t)want || sf_error(file)) {
            error(r->message, sizeof(r->message), "音频解码失败或文件已被截断");
            goto done;
        }
        remaining -= (size_t)got;
        for (size_t f = 0; f < (size_t)got; f++) {
            float l = input[f * info.channels],
                  rt = input[f * info.channels + info.channels - 1];
            if (!isfinite(l) || !isfinite(rt)) {
                error(r->message, sizeof(r->message), "音频包含无效采样值");
                goto done;
            }
            stereo[f * 2] = l;
            stereo[f * 2 + 1] = rt;
        }
        size_t consumed = 0;
        for (;;) {
            const float* samples = stereo;
            size_t frames = (size_t)got;
            if (converter) {
                SRC_DATA data = {.data_in = stereo + consumed * 2,
                                 .data_out = output,
                                 .input_frames = got - (long)consumed,
                                 .output_frames = 16384,
                                 .end_of_input = remaining == 0,
                                 .src_ratio = (double)DSE_AUDIO_RATE / info.samplerate};
                code = src_process(converter, &data);
                if (code) {
                    snprintf(r->message, sizeof(r->message), "重采样失败：%s",
                             src_strerror(code));
                    goto done;
                }
                consumed += (size_t)data.input_frames_used;
                samples = output;
                frames = (size_t)data.output_frames_gen;
                if (!frames && !data.input_frames_used && consumed < (size_t)got) {
                    error(r->message, sizeof(r->message), "重采样未消耗输入");
                    goto done;
                }
            } else
                consumed = (size_t)got;
            size_t count = frames < total - written ? frames : total - written;
            /* Peaks describe the clip itself, independently of the track mix. */
            DseWavePeaks* peaks = &r->peaks[index];
            for (size_t f = 0; f < count; f++) {
                unsigned bin = (unsigned)((written + f) * DSE_WAVE_PEAKS / total);
                float l = samples[f * 2], rt = samples[f * 2 + 1];
                peaks->low[bin] = fminf(peaks->low[bin], fminf(l, rt));
                peaks->high[bin] = fmaxf(peaks->high[bin], fmaxf(l, rt));
            }
            dse_mix_stereo(r->pcm + (offset + written) * 2, samples, count, written, total,
                           fade_in, fade_out, left, right);
            written += count;
            if (!converter || (consumed == (size_t)got && (remaining || !frames)))
                break;
        }
        if (!remaining)
            break;
    }
    if (written + 1 < total) {
        error(r->message, sizeof(r->message), "重采样输出时长不完整");
        goto done;
    }
    r->samples = *capacity;
    ok = 1;
done:
    if (converter)
        src_delete(converter);
    sf_close(file);
    return ok;
}
int dse_audio_mix(DseRender* r, size_t* capacity) {
    int solo = 0;
    for (unsigned t = 0; t < r->score.track_count; t++)
        solo |= r->score.tracks[t].solo;
    memset(r->peaks, 0, sizeof(r->peaks));
    for (unsigned w = 0; w < r->score.wave_count; w++) {
        const DseTrack* track = &r->score.tracks[r->score.waves[w].track];
        if (track->mute || (solo && !track->solo))
            continue;
        if (!mix_wave(r, w, capacity))
            return 0;
        if (r->audio_only)
            atomic_store(&r->progress, (int)((w + 1) * 100 / r->score.wave_count));
    }
    return 1;
}
