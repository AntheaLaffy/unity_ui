#include "app.h"
#include "audio.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#define BG 0x191d24ff
#define PANEL 0x222731ff
#define BORDER 0x343c49ff
#define TEXT 0xe3e9f1ff
#define MUTED 0x8895a8ff
#define ACCENT 0x68d9c0ff
static void rect(DseApp* a, float x, float y, float w, float h, uint32_t color, float r) {
    asmui_canvas_rect(&a->canvas, x, y, w, h, color, r);
}
static void label(DseApp* a, float x, float y, float w, float size, uint32_t color, const char* s) {
    asmui_canvas_text(&a->canvas, x, y, w, size, color, s);
}
static void button(DseApp* a, int id, float x, float y, float w, float h, const char* s, int active,
                   int primary) {
    if (a->button_count == DSE_BUTTONS) {
        a->canvas.overflow = 1;
        return;
    }
    DseRect r = {x, y, w, h};
    for (int p = a->canvas.parent; p >= 0; p = a->canvas.nodes[p].parent) {
        AsmUiNode* node = &a->canvas.nodes[p];
        float px = 0, py = 0;
        for (int q = p; q >= 0; q = a->canvas.nodes[q].parent) {
            px += a->canvas.nodes[q].anchored_x;
            py -= a->canvas.nodes[q].anchored_y;
        }
        float right = fminf(r.x + r.w, px + node->size_delta_x),
              bottom = fminf(r.y + r.h, py + node->size_delta_y);
        r.x = fmaxf(r.x, px);
        r.y = fmaxf(r.y, py);
        r.w = fmaxf(0, right - r.x);
        r.h = fmaxf(0, bottom - r.y);
    }
    a->buttons[a->button_count++] = (DseButton){id, r};
    int hover =
        a->pointer_x >= x && a->pointer_x < x + w && a->pointer_y >= y && a->pointer_y < y + h;
    rect(a, x, y, w, h,
         primary  ? ACCENT
         : active ? 0x354e50ff
         : hover  ? 0x38414fff
                  : 0x2d3441ff,
         4);
    label(a, x + 10, y + (h - 23) * .5f, w - 16, 14,
          primary  ? 0x142d2aff
          : active ? ACCENT
                   : TEXT,
          s);
}
static void field(DseApp* a, int id, float x, float y, float w, const char* title,
                  const char* value) {
    label(a, x, y, w, 12, MUTED, title);
    button(a, id, x, y + 23, w, 34, a->focus == id ? a->edit : value, a->focus == id, 0);
    if (a->focus == id) {
        rect(a, x, y + 55, w, 2, ACCENT, 0);
        if (*a->composition)
            label(a, x, y + 64, w, 14, ACCENT, a->composition);
    }
}
static const char* key_name(int key, char* out, size_t n) {
    static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    snprintf(out, n, "%s%d", names[key % 12], key / 12 - 1);
    return out;
}
static int black_key(int key) {
    int k = key % 12;
    return k == 1 || k == 3 || k == 6 || k == 8 || k == 10;
}
void dse_app_layout(DseApp* a) {
    float w = a->view.width, h = a->view.height;
    a->view.x = 80;
    a->view.y = 326;
    a->view.w = w - 80 - 276;
    a->view.h = h - 326 - 250;
    if (a->view.h < 100)
        a->view.h = 100;
}
void dse_paint_note(AsmUiCanvas* c, const DseNote* n, const DseRect* r) {
    uint32_t color = n->selected ? 0xa2f0ddff : ACCENT;
    asmui_canvas_rect(c, r->x, r->y, r->w, r->h, n->selected ? 0xe8fff8ff : 0x327a73ff, 3);
    asmui_canvas_rect(c, r->x + 1, r->y + 1, fmaxf(1, r->w - 2), fmaxf(1, r->h - 2), color, 2);
    char preview[28];
    const char* lyric = n->lyric;
    if (strlen(lyric) > 24) {
        size_t bytes = 24;
        while (bytes && ((unsigned char)lyric[bytes] & 0xc0) == 0x80)
            bytes--;
        snprintf(preview, sizeof(preview), "%.*s…", (int)bytes, lyric);
        lyric = preview;
    }
    asmui_canvas_text(c, r->x + 7, r->y + 1, fmaxf(1, r->w - 12), 13, 0x16352fff, lyric);
    if (r->w > 24)
        asmui_canvas_rect(c, r->x + r->w - 4, r->y + 4, 1, fmaxf(1, r->h - 8), 0x346d60ff, 0);
}
static void inspector(DseApp* a) {
    float x = a->view.width - 256, w = 236;
    label(a, x, 119, w, 15, TEXT, "声库与合成");
    label(a, x, 151, w, 12, MUTED, "渲染引擎");
    label(a, x, 173, w, 16, ACCENT, "DiffSinger");
    button(a, B_SINGER, x, 210, w, 34, a->config.name, 0, 0);
    char text[256];
    snprintf(text, sizeof(text), "声线 · %s", a->config.speaker);
    button(a, B_SPEAKER, x, 254, w, 32, text, 0, 0);
    const char* language = !strcmp(a->config.language_code, "ja")    ? "日文"
                           : !strcmp(a->config.language_code, "zh")  ? "中文"
                           : !strcmp(a->config.language_code, "en")  ? "英文"
                           : !strcmp(a->config.language_code, "yue") ? "粤语"
                                                                     : "未选择";
    snprintf(text, sizeof(text), "歌词语言 · %s (%s)", language, a->config.language_code);
    button(a, B_LANGUAGE, x, 293, w, 30, text, 0, 0);
    rect(a, x, 333, w, 1, BORDER, 0);
    float bottom = a->view.height - 115;
    int max_scroll = (int)(780 - bottom);
    if (max_scroll < 0 || a->active_wave)
        max_scroll = 0;
    if (a->inspector_scroll > max_scroll)
        a->inspector_scroll = max_scroll;
    a->canvas.parent = asmui_canvas_clip(&a->canvas, x, 340, w, fmaxf(1, bottom - 345));
    if (a->active_wave && (unsigned)a->active_wave <= a->score->wave_count) {
        const DseWave* wave = &a->score->waves[a->active_wave - 1];
        const DseTrack* track = &a->score->tracks[wave->track];
        snprintf(text, sizeof(text), "%.3g", track->gain);
        field(a, B_TRACK_GAIN, x, 340, 112, "音轨音量 · dB", text);
        snprintf(text, sizeof(text), "%.3g", track->pan * 100);
        field(a, B_TRACK_PAN, x + 124, 340, 112, "声像 · ±100", text);
        label(a, x, 408, w, 14, ACCENT, wave->name);
        snprintf(text, sizeof(text), "%.6g", wave->pos / (double)DSE_PPQ);
        field(a, B_AUDIO_POS, x, 435, 112, "位置 · 拍", text);
        snprintf(text, sizeof(text), "%.4g", wave->gain);
        field(a, B_AUDIO_GAIN, x + 124, 435, 112, "片段音量 · dB", text);
        snprintf(text, sizeof(text), "%.6g", wave->skip / (double)DSE_PPQ);
        field(a, B_AUDIO_SKIP, x, 501, 112, "头部裁剪 · 拍", text);
        snprintf(text, sizeof(text), "%.6g", wave->trim / (double)DSE_PPQ);
        field(a, B_AUDIO_TRIM, x + 124, 501, 112, "尾部裁剪 · 拍", text);
        snprintf(text, sizeof(text), "%.6g", wave->fade_in / (double)DSE_PPQ);
        field(a, B_AUDIO_FADE_IN, x, 567, 112, "淡入 · 拍", text);
        snprintf(text, sizeof(text), "%.6g", wave->fade_out / (double)DSE_PPQ);
        field(a, B_AUDIO_FADE_OUT, x + 124, 567, 112, "淡出 · 拍", text);
        button(a, B_AUDIO_RELINK, x, 637, 112, 31, "重新链接", 0, 0);
        button(a, B_AUDIO_DELETE, x + 124, 637, 112, 31, "删除片段", 0, 0);
    } else {
        float scroll = a->inspector_scroll;
        label(a, x, 353 - scroll, w, 15, TEXT, "音符属性 · 滚动查看");
        const DseNote* note = NULL;
        unsigned selected = 0;
        for (unsigned i = 0; i < a->score->count; i++)
            if (a->score->notes[i].selected) {
                if (!note)
                    note = &a->score->notes[i];
                selected++;
            }
        if (note) {
            char key[32];
            key_name(note->key, key, sizeof(key));
            snprintf(text, sizeof(text), "%s   ·   已选择 %u 个音符", key, selected);
            label(a, x, 383 - scroll, w, 14, ACCENT, text);
            field(a, B_LYRIC, x, 426 - scroll, w, "歌词 · 双击音符也可编辑", note->lyric);
            field(a, B_PHONEMES, x, 505 - scroll, w, "音素 / 读音 · 留空自动转换", note->phonemes);
            DseNotePlan plan;
            unsigned index = (unsigned)(note - a->score->notes);
            if (dse_app_note_plan(a, index, &plan)) {
                label(a, x, 577 - scroll, w, 12, MUTED,
                      plan.tied          ? "元音延音 · 继承所属音节时长"
                      : plan.progression ? "当前音节 · 可独立调整时长"
                                         : "实际音素 · 与合成输入一致");
                char symbols[4096] = "";
                size_t used = 0;
                for (unsigned i = 0; i < plan.count; i++)
                    used += (size_t)snprintf(symbols + used, sizeof(symbols) - used, "%s%s",
                                             used ? " " : "", plan.phones[i].symbol);
                label(a, x, 599 - scroll, w, 13, ACCENT, symbols);
                char durations[1024];
                dse_timing_text(a->score, index, &plan, durations, sizeof(durations));
                field(a, B_TIMING, x, 650 - scroll, w - 68, "音素时长 ms · * 自动余量",
                      plan.tied ? "调整所属音节" : durations);
            } else {
                label(a, x, 577 - scroll, w, 12, 0xf4bb84ff, a->front_message);
                field(a, B_TIMING, x, 650 - scroll, w - 68, "音素时长 ms · * 自动余量",
                      "等待音素解析");
            }
            button(a, B_TIMING_RESET, x + w - 60, 673 - scroll, 60, 34, "重置", 0, 0);
            snprintf(text, sizeof(text), "位置 %.2f 拍 · 时长 %.2f 拍", note->pos / (float)DSE_PPQ,
                     note->len / (float)DSE_PPQ);
            label(a, x, 623 - scroll, w, 12, TEXT, text);
        } else {
            label(a, x, 395 - scroll, w, 14, MUTED, "选择音符以编辑歌词与音素");
            label(a, x, 437 - scroll, w, 13, MUTED, "画笔：点击空白处添加音符");
            label(a, x, 469 - scroll, w, 13, MUTED, "拖动音符移动，拖右边缘改时长");
            label(a, x, 501 - scroll, w, 13, MUTED, "选择：拖动框选多个音符");
            label(a, x, 533 - scroll, w, 13, MUTED, "Ctrl+C / V 复制与粘贴");
        }
        const DseTrack* track = &a->score->tracks[a->active_track];
        snprintf(text, sizeof(text), "%.3g", track->gain);
        field(a, B_TRACK_GAIN, x, 715 - scroll, 112, "音轨音量 · dB", text);
        snprintf(text, sizeof(text), "%.3g", track->pan * 100);
        field(a, B_TRACK_PAN, x + 124, 715 - scroll, 112, "声像 · ±100", text);
    }
    a->canvas.parent = -1;
    rect(a, x, bottom, w, 1, BORDER, 0);
    label(a, x, bottom + 14, w, 12, MUTED, "合成状态");
    const char* state =
        a->render ? (a->render->audio_only ? "正在准备伴奏…" : "正在合成…")
        : a->pcm && a->pcm_revision == a->score->revision
            ? (a->pcm_audio_only ? "伴奏就绪 · 可试听与导出" : "歌声混音就绪 · 可播放与导出")
            : "等待合成";
    label(a, x, bottom + 39, w, 14, a->pcm && a->pcm_revision == a->score->revision ? ACCENT : TEXT,
          state);
    if (a->render) {
        int progress = atomic_load(&a->render->progress);
        rect(a, x, bottom + 76, w, 3, BORDER, 0);
        rect(a, x, bottom + 76, w * progress / 100, 3, ACCENT, 0);
    } else if (a->project_info.ustx) {
        snprintf(text, sizeof(text), "USTX · %u 个音频片段 · %u 项待适配", a->score->wave_count,
                 a->project_info.unsupported_curves + a->project_info.expressions +
                     a->project_info.overrides);
        label(a, x, bottom + 67, w, 11, MUTED, text);
    }
}
static void arrangement(DseApp* a) {
    const float start = 208, right = a->view.width - 276,
                beat = a->arrange_beat > 0 ? a->arrange_beat : 42;
    float barbeats = a->score->numerator * 4.f / a->score->denominator;
    label(a, 18, 119, 175, 14, MUTED, "音轨");
    button(a, B_TRACK, 154, 111, 34, 28, "+", 0, 0);
    button(a, B_AUDIO_IMPORT, start, 111, 90, 28, "导入伴奏", 0, 0);
    button(a, B_AUDIO_PREVIEW, start + 102, 111, 90, 28,
           a->playing && a->pcm_audio_only ? "暂停伴奏" : "伴奏试听",
           a->playing && a->pcm_audio_only, 0);
    label(a, start + 209, 119, fmaxf(1, right - start - 209), 12, MUTED,
          "Shift 滚动时间轴 · Ctrl 缩放");
    int first_bar = (int)(a->arrange_tick / (DSE_PPQ * barbeats));
    for (int bar = first_bar;; bar++) {
        float x = start + (bar * DSE_PPQ * barbeats - a->arrange_tick) / DSE_PPQ * beat;
        if (x >= right)
            break;
        if (x < start)
            continue;
        asmui_canvas_printf(&a->canvas, x + 6, 147, 64, 12, MUTED, "%d", bar + 1);
        rect(a, x, 171, 1, 91, BORDER, 0);
    }
    int parent = asmui_canvas_clip(&a->canvas, 0, 169, right, 101);
    a->canvas.parent = parent;
    for (unsigned t = 0; t < a->score->track_count; t++) {
        float y = 174 + ((int)t - a->track_scroll) * 42;
        if (y + 39 <= 169 || y >= 270)
            continue;
        int active = a->active_track == (int)t;
        rect(a, 0, y, 194, 39, active ? 0x2a3940ff : PANEL, 0);
        rect(a, 0, y, 3, 39, a->score->tracks[t].color, 0);
        button(a, B_TRACK_BASE + (int)t, 10, y + 4, 112, 31, a->score->tracks[t].name, active, 0);
        button(a, B_MUTE_BASE + (int)t, 130, y + 6, 25, 27, "M", a->score->tracks[t].mute, 0);
        button(a, B_SOLO_BASE + (int)t, 161, y + 6, 25, 27, "S", a->score->tracks[t].solo, 0);
        for (unsigned p = 0; p < a->score->part_count; p++)
            if (a->score->parts[p].track == (int)t) {
                const DsePart* part = &a->score->parts[p];
                float x = start + (part->pos - a->arrange_tick) / (float)DSE_PPQ * beat,
                      w = part->len / (float)DSE_PPQ * beat;
                if (x + w <= start || x >= right)
                    continue;
                int clip = asmui_canvas_clip(&a->canvas, start, y, right - start, 38);
                a->canvas.parent = clip;
                float bx = fmaxf(start, x), bw = fminf(right, x + w - 3) - bx;
                button(a, B_PART_BASE + (int)p, bx, y + 3, fmaxf(1, bw), 33, part->name,
                       !a->active_wave && a->view.active_part == (int)p, 0);
                rect(a, x + 2, y + 33, w - 7, 2, a->score->tracks[t].color, 0);
                a->canvas.parent = parent;
            }
        for (unsigned w = 0; w < a->score->wave_count; w++) {
            const DseWave* wave = &a->score->waves[w];
            if (wave->track != (int)t)
                continue;
            float x = start + (wave->pos - a->arrange_tick) / (float)DSE_PPQ * beat,
                  width = (float)dse_wave_ticks(wave, a->score->bpm) / DSE_PPQ * beat;
            if (x + width <= start || x >= right)
                continue;
            int clip = asmui_canvas_clip(&a->canvas, start, y, right - start, 38);
            a->canvas.parent = clip;
            float bx = fmaxf(start, x), bw = fminf(right, x + width) - bx;
            button(a, B_WAVE_BASE + (int)w, bx, y + 3, fmaxf(1, bw), 33, "",
                   a->active_wave == (int)w + 1, 0);
            if (a->peaks_revision) {
                int begin = (int)fmaxf(0, (start - x) / width * DSE_WAVE_PEAKS),
                    end = (int)fminf(DSE_WAVE_PEAKS, (right - x) / width * DSE_WAVE_PEAKS + 1);
                int stride = (end - begin + 127) / 128;
                if (stride < 1)
                    stride = 1;
                for (int bin = begin; bin < end; bin += stride) {
                    float high = 0, low = 0;
                    for (int b = bin; b < bin + stride && b < end; b++) {
                        high = fmaxf(high, a->peaks[w].high[b]);
                        low = fminf(low, a->peaks[w].low[b]);
                    }
                    float px = x + bin * width / DSE_WAVE_PEAKS;
                    asmui_canvas_line(&a->canvas, px, y + 21 - fminf(1, high) * 12, px,
                                      y + 21 - fmaxf(-1, low) * 12,
                                      fmaxf(1, width / DSE_WAVE_PEAKS * stride), 0x5c92b2ff);
                }
            }
            label(a, bx + 8, y + 4, fmaxf(1, bw - 20), 12, TEXT, wave->name);
            float fade_in = fminf(width, wave->fade_in / (float)DSE_PPQ * beat),
                  fade_out = fminf(width, wave->fade_out / (float)DSE_PPQ * beat);
            if (fade_in > 0)
                asmui_canvas_line(&a->canvas, x, y + 35, x + fade_in, y + 4, 1, 0xd4bc8cff);
            if (fade_out > 0)
                asmui_canvas_line(&a->canvas, x + width - fade_out, y + 4, x + width, y + 35, 1,
                                  0xd4bc8cff);
            rect(a, x + 1, y + 6, 3, 26, 0xabcbe0ff, 0);
            rect(a, x + width - 4, y + 6, 3, 26, 0xabcbe0ff, 0);
            a->canvas.parent = parent;
        }
    }
    float play_x = start + (a->play_tick - a->arrange_tick) / DSE_PPQ * beat;
    if (play_x >= start && play_x < right)
        rect(a, play_x, 169, 2, 101, 0xf8cc82ff, 0);
    a->canvas.parent = -1;
}
static void piano(DseApp* a) {
    DseView* v = &a->view;
    float right = v->x + v->w, bottom = v->y + v->h;
    rect(a, 0, 281, a->view.width - 276, 40, PANEL, 0);
    button(a, B_SELECT, 16, 286, 67, 29, "选择 S", v->tool == 0, 0);
    button(a, B_DRAW, 91, 286, 67, 29, "画笔 D", v->tool == 1, 0);
    button(a, B_PITCH, 166, 286, 67, 29, "音高 R", v->tool == 2, 0);
    button(a, B_ERASE, 241, 286, 67, 29, "擦除 E", v->tool == 3, 0);
    char snap[64];
    snprintf(snap, sizeof(snap), "吸附 1/%d", 1920 / v->grid);
    button(a, B_SNAP, 326, 286, 99, 29, snap, 0, 0);
    const DsePart* part = &a->score->parts[v->active_part];
    label(a, 443, 290, fmaxf(80, right - 600), 13, MUTED, part->name);
    button(a, B_ZOOM_OUT, right - 76, 286, 29, 29, "−", 0, 0);
    button(a, B_ZOOM_IN, right - 38, 286, 29, 29, "+", 0, 0);
    int parent = asmui_canvas_clip(&a->canvas, 0, v->y, right, v->h);
    a->canvas.parent = parent;
    for (int row = 0; row * v->row_height < v->h; row++) {
        int key = v->top_key - row;
        if (key < 0 || key > 127)
            continue;
        float y = v->y + row * v->row_height;
        int black = black_key(key);
        rect(a, v->x, y, v->w, v->row_height, black ? 0x1c2129ff : 0x232933ff, 0);
        rect(a, v->x, y, v->w, 1, 0x2d3541ff, 0);
        rect(a, 2, y, 74, v->row_height - 1, black ? 0x181e26ff : 0xb9c4cfff, 0);
        if (black)
            rect(a, 2, y, 48, v->row_height - 1, 0x10151bff, 0);
        char name[32];
        key_name(key, name, sizeof(name));
        if (!black && key % 12 != 0)
            label(a, 46, y + 2, 28, 10, 0x44505fff, name);
        if (key % 12 == 0) {
            label(a, 8, y + 2, 65, 11, black ? MUTED : 0x344354ff, name);
            rect(a, v->x, y, v->w, 1, 0x465364ff, 0);
        }
    }
    float ticks = v->w / v->beat_width * DSE_PPQ;
    int begin = v->scroll_tick / v->grid;
    int bar = (int)(DSE_PPQ * a->score->numerator * 4 / a->score->denominator);
    for (int k = begin; k * v->grid < v->scroll_tick + ticks; k++) {
        float x = v->x + (k * v->grid - v->scroll_tick) / (float)DSE_PPQ * v->beat_width;
        if (x < v->x)
            continue;
        rect(a, x, v->y, 1, v->h,
             k * v->grid % bar == 0   ? 0x526172ff
             : k * v->grid % 480 == 0 ? 0x394351ff
                                      : 0x2a313cff,
             0);
    }
    int noteclip = asmui_canvas_clip(&a->canvas, v->x, v->y, v->w, v->h);
    a->canvas.parent = noteclip;
    dse_draw_notes(a->score, v, &a->canvas);
    /* Actual editable pitch curve, evaluated by the same ASM function used by
     * the acoustic input preparation. It is never a decorative fake curve. */
    float lastx = 0, lasty = 0;
    int last_note = -1;
    /* Bound curve geometry by viewport width, including projects containing
     * many overlapping notes. The renderer rejects overlaps during synthesis. */
    for (float x = v->x; x <= right; x += 4) {
        float tick = v->scroll_tick + (x - v->x) / v->beat_width * DSE_PPQ;
        int index = -1;
        for (unsigned i = a->score->count; i-- > 0;)
            if (a->score->notes[i].part == v->active_part && tick >= a->score->notes[i].pos &&
                tick < a->score->notes[i].pos + a->score->notes[i].len) {
                index = (int)i;
                break;
            }
        if (index < 0) {
            last_note = -1;
            continue;
        }
        float pitch =
            dse_curve_value(a->score, v->active_part, 0, tick, a->score->notes[index].key);
        float y = v->y + (v->top_key - pitch + .5f) * v->row_height;
        if (last_note == index)
            asmui_canvas_line(&a->canvas, lastx, lasty, x, y, 1.2f, 0xf8e5a8ff);
        lastx = x;
        lasty = y;
        last_note = index;
    }
    if (a->drag == 4) {
        DseRect r = a->selection_box;
        rect(a, r.x, r.y, r.w, r.h, 0x68d9c030, 0);
    }
    float play =
        v->x + (a->play_tick - part->pos - v->scroll_tick) / (float)DSE_PPQ * v->beat_width;
    if (play >= v->x && play <= right)
        rect(a, play, v->y, 1.5f, v->h, 0xf3ce8cff, 0);
    a->canvas.parent = -1;
    rect(a, 0, bottom + 2, right, 2, BORDER, 0);
    for (int tick = (v->scroll_tick / bar) * bar; tick < v->scroll_tick + ticks; tick += bar) {
        float x = v->x + (tick - v->scroll_tick) / (float)DSE_PPQ * v->beat_width;
        if (x >= v->x)
            asmui_canvas_printf(&a->canvas, x + 5, bottom + 8, 80, 11, MUTED, "%d", tick / bar + 1);
    }
}
static void phoneme_lane(DseApp* a) {
    DseView* v = &a->view;
    float y = v->y + v->h + 31;
    rect(a, v->x, y, v->w, 31, 0x202e34ff, 0);
    label(a, 6, y + 6, 70, 11, MUTED, "音素时长");
    unsigned index = a->score->count;
    for (unsigned i = 0; i < a->score->count; i++)
        if (a->score->notes[i].selected && a->score->notes[i].part == v->active_part) {
            index = i;
            break;
        }
    if (index == a->score->count) {
        label(a, v->x + 8, y + 6, v->w - 16, 11, MUTED, "选择音符显示实际音素 · 右侧调整时长");
        return;
    }
    DseNotePlan plan;
    if (!dse_app_note_plan(a, index, &plan)) {
        label(a, v->x + 8, y + 6, v->w - 16, 11, MUTED, a->front_message);
        return;
    }
    a->canvas.parent = asmui_canvas_clip(&a->canvas, v->x, y, v->w, 31);
    int cursor = plan.start;
    for (unsigned i = 0; i < plan.count; i++) {
        const DsePhone* p = &plan.phones[i];
        double start = cursor * 512.0 * a->score->bpm * DSE_PPQ / (60 * 44100),
               stop = (cursor + p->frames) * 512.0 * a->score->bpm * DSE_PPQ / (60 * 44100);
        float x = v->x + (float)(start - v->scroll_tick) / DSE_PPQ * v->beat_width,
              width = (float)(stop - start) / DSE_PPQ * v->beat_width;
        if (x + width >= v->x && x < v->x + v->w) {
            rect(a, x, y + 1, fmaxf(1, width - 1), 29,
                 (int)i == plan.sustain ? 0x37645bff : 0x354651ff, 2);
            label(a, x + 4, y + 6, fmaxf(1, width - 8), 11, TEXT, p->symbol);
        }
        cursor += p->frames;
    }
    a->canvas.parent = -1;
}
static void expression(DseApp* a) {
    DseView* v = &a->view;
    float y = v->y + v->h + 76, w = v->w;
    const char* names[] = {"音高", "气声", "张力", "清浊", "性别", "速度"};
    for (int k = 0; k < 6; k++)
        button(a, B_LANE_PITCH + k, 18 + k * 67, y, 61, 27, names[k], a->lane == k, 0);
    button(a, B_CLEAR_CURVE, v->x + w - 94, y, 94, 27, "重置曲线", 0, 0);
    y += 42;
    float h = 66;
    rect(a, v->x, y, w, h, 0x1e242eff, 0);
    label(a, 6, y + 4, 70, 12, MUTED, names[a->lane]);
    label(a, 6, y + 33, 70, 11, MUTED,
          a->lane == 0 ? "±2 半音" : a->lane == 1 || a->lane == 3 ? "±12 dB" : "拖动画线");
    float mid = y + h * .5f;
    rect(a, v->x, mid, w, 1, BORDER, 0);
    int have = 0;
    float lastx = 0, lasty = 0;
    for (float x = 0; x < w; x += 4) {
        float tick = v->scroll_tick + x / v->beat_width * 480, base = a->lane == 5 ? 1 : 0;
        if (a->lane == 0) {
            base = 60;
            for (unsigned i = 0; i < a->score->count; i++)
                if (a->score->notes[i].part == v->active_part && tick >= a->score->notes[i].pos &&
                    tick <= a->score->notes[i].pos + a->score->notes[i].len) {
                    base = a->score->notes[i].key;
                    break;
                }
        }
        float value = dse_curve_value(a->score, v->active_part, a->lane, tick, base);
        float scales[] = {15, 2.5f, 6, 2.5f, 30, 25};
        float vy = mid - (value - base) * scales[a->lane];
        vy = fmaxf(y, fminf(y + h, vy));
        if (have)
            asmui_canvas_line(&a->canvas, lastx, lasty, v->x + x, vy, 1.5f, ACCENT);
        lastx = v->x + x;
        lasty = vy;
        have = 1;
    }
    y += 82;
    label(a, 6, y, 70, 12, MUTED, a->pcm_audio_only ? "伴奏波形" : "工程波形");
    if (a->pcm && a->pcm_revision == a->score->revision) {
        size_t offset =
            (size_t)(a->score->parts[v->active_part].pos * 60.0 * 44100 / (a->score->bpm * 480));
        for (int x = 0; x < (int)w; x += 3) {
            double tick = v->scroll_tick + x / v->beat_width * 480;
            size_t index = offset + (size_t)(tick * 60 * 44100 / (a->score->bpm * 480));
            float peak = 0;
            for (size_t k = 0; k < 128 && index + k < a->samples; k++)
                peak = fmaxf(peak, fabsf(a->pcm[(index + k) * 2]));
            rect(a, v->x + x, y + 11 - peak * 10, 1, 1 + peak * 20, 0x6d9c9cff, 0);
        }
    } else
        label(a, v->x, y, w, 12, MUTED, "合成或伴奏准备完成后显示波形");
}
void dse_app_scene(DseApp* a) {
    asmui_canvas_begin(&a->canvas, a->nodes, DSE_NODES, a->strings, 1024 * 1024, a->view.width,
                       a->view.height);
    a->button_count = 0;
    float w = a->view.width, h = a->view.height;
    rect(a, 0, 0, w, h, BG, 0);
    rect(a, 0, 0, w, 43, 0x202630ff, 0);
    rect(a, 0, 43, w, 60, PANEL, 0);
    label(a, 18, 8, 170, 19, ACCENT, "DiffSinger-ASM");
    label(a, 205, 10, 300, 14, TEXT,
          a->path[0] ? strrchr(a->path, '/') ? strrchr(a->path, '/') + 1 : a->path : "未命名工程");
    if (a->modified)
        label(a, 505, 11, 100, 13, ACCENT, "● 未保存");
    label(a, w - 267, 11, 250, 12, MUTED, "DiffSinger 专属虚拟歌姬编辑器");
    button(a, B_NEW, 16, 57, 56, 31, "新建", 0, 0);
    button(a, B_OPEN, 80, 57, 56, 31, "打开", 0, 0);
    button(a, B_SAVE, 144, 57, 56, 31, "保存", 0, 0);
    button(a, B_UNDO, 219, 57, 35, 31, "↶", 0, 0);
    button(a, B_REDO, 262, 57, 35, 31, "↷", 0, 0);
    button(a, B_STOP, 323, 57, 35, 31, "■", 0, 0);
    button(a, B_PLAY, 366, 57, 73, 31, a->playing ? "暂停" : "▶ 播放", a->playing, 0);
    char text[256];
    snprintf(text, sizeof(text), "%.0f BPM", a->score->bpm);
    button(a, B_BPM, 458, 57, 87, 31, a->focus == B_BPM ? a->edit : text, a->focus == B_BPM, 0);
    snprintf(text, sizeof(text), "%u / %u", a->score->numerator, a->score->denominator);
    label(a, 560, 62, 60, 14, MUTED, text);
    snprintf(text, sizeof(text), "%02d:%05.2f",
             (int)(a->play_tick * 60 / (a->score->bpm * 480)) / 60,
             fmod(a->play_tick * 60 / (a->score->bpm * 480), 60));
    label(a, 650, 61, 150, 17, TEXT, text);
    button(a, B_EXPORT, w - 222, 57, 82, 31, "导出 WAV", 0, 0);
    button(a, B_RENDER, w - 126, 57, 108, 31, a->render ? "取消合成" : "合成歌声", 0, 1);
    rect(a, 194, 105, 1, 176, BORDER, 0);
    rect(a, w - 276, 105, 1, h - 142, BORDER, 0);
    arrangement(a);
    piano(a);
    phoneme_lane(a);
    expression(a);
    inspector(a);
    rect(a, 0, h - 35, w, 35, PANEL, 0);
    label(a, 18, h - 28, w - 38, 12, MUTED,
          a->message[0]
              ? a->message
              : "就绪 · 空格播放 · Ctrl+S 保存 · F5 合成 · Ctrl+滚轮缩放 · Shift+滚轮横向滚动");
    if (a->modal) {
        rect(a, 0, 0, w, h, 0x00000090, 0);
        float x = w / 2 - 224, y = h / 2 - 97;
        rect(a, x, y, 448, 194, PANEL, 8);
        label(a, x + 22, y + 20, 400, 18, TEXT, "工程有未保存的修改");
        label(a, x + 22, y + 58, 400, 14, MUTED, "保存后继续，或放弃本次修改。");
        button(a, B_MODAL_CANCEL, x + 22, y + 126, 115, 36, "取消", 0, 0);
        button(a, B_MODAL_DISCARD, x + 149, y + 126, 127, 36, "放弃修改", 0, 0);
        button(a, B_MODAL_SAVE, x + 288, y + 126, 138, 36, "保存并继续", 0, 1);
    }
}
