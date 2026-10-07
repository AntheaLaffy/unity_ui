#define _POSIX_C_SOURCE 200809L
#include "editor.h"
#include "project.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static unsigned painted;
void dse_paint_note(AsmUiCanvas* c, const DseNote* n, const DseRect* r) {
    (void)c;
    (void)n;
    (void)r;
    painted++;
}
static void approx(float a, float b) { assert(fabsf(a - b) < .001f); }
static void ustx_check(DseScore* s, DseScore* before) {
    char folder[] = "/tmp/dse-ustx-XXXXXX", path[128], saved[128], msg[512];
    assert(mkdtemp(folder));
    snprintf(path, sizeof(path), "%s/source.ustx", folder);
    snprintf(saved, sizeof(saved), "%s/saved.dse", folder);
    const char* fixture =
        "ustx_version: '0.9'\nbpm: 140\n"
        "tempos: [{position: 0, bpm: 120}]\n"
        "time_signatures: [{bar_position: 0, beat_per_bar: 4, beat_unit: 4}]\n"
        "tracks: [{track_name: '原始音轨', volume: -3, pan: -25, mute: false, solo: true}]\n"
        "voice_parts:\n- {name: Voice, track_no: 0, position: 960, duration: 960, notes: "
        "[{position: 0, duration: 480, tone: 69, lyric: no, "
        "pitch: {snap_first: false, data: [{x: 0, y: 0, shape: io}, {x: 250, y: 20, shape: "
        "io}]}, "
        "phoneme_overrides: []}], curves: [{abbr: pitd, xs: [0, 240, 480], ys: [0, 100, "
        "0]}]}\n"
        "wave_parts: [{track_no: 0, position: 0, relative_path: music.flac, "
        "file_duration_ms: 1000}]\n"
        "future_field: {keep: '未知数据'}\n";
    FILE* f = fopen(path, "wb");
    assert(f && fputs(fixture, f) >= 0 && !fclose(f));
    DseProjectInfo info = {0}, reload = {0};
    dse_score_init(s, 0);
    assert(dse_project_load(s, &info, path, msg, sizeof(msg)));
    assert(s->bpm == 120 && s->count == 1 && s->parts[0].pos == 960);
    assert(!strcmp(s->notes[0].lyric, "no") && info.wave_parts == 1 &&
           !strcmp(info.ustx, fixture));
    assert(s->wave_count == 1 && s->waves[0].duration_ms == 1000 &&
           s->waves[0].path[0] == '/');
    approx(s->tracks[0].gain, -3);
    approx(s->tracks[0].pan, -.25f);
    approx(dse_curve_value(s, 0, 0, 120, 69), 70.5f);
    approx(dse_curve_value(s, 0, 0, 240, 69), 72);
    assert(!dse_project_save(s, &info, path, NULL, msg, sizeof(msg)));
    assert(dse_project_save(s, &info, saved, NULL, msg, sizeof(msg)));
    assert(dse_project_load(before, &reload, saved, msg, sizeof(msg)));
    assert(!strcmp(reload.ustx, fixture) && !strcmp(before->notes[0].lyric, "no"));
    assert(before->wave_count == 1 && !memcmp(before->waves, s->waves, sizeof(DseWave)));
    /* The lookup agrees with the independent unsorted implementation, including
     * lane boundaries, exact endpoints and fractional tick positions. */
    before->curve_sorted = 0;
    for (int i = -2; i < 970; i++)
        approx(dse_curve_value(s, 0, 0, i * .5f, 65),
               dse_curve_value(before, 0, 0, i * .5f, 65));
    *before = *s;
    char* old_source = info.ustx;
    f = fopen(path, "wb");
    assert(f && fputs("ustx_version: '0.9'\ntracks: []\n", f) >= 0 && !fclose(f));
    assert(!dse_project_load(s, &info, path, msg, sizeof(msg)) &&
           !memcmp(s, before, sizeof(*s)) && info.ustx == old_source);
    dse_project_info_clear(&info);
    dse_project_info_clear(&reload);
    unlink(path);
    unlink(saved);
    rmdir(folder);
}
int main(void) {
    DseScore *s = malloc(sizeof(*s)), *before = malloc(sizeof(*s));
    assert(s && before);
    dse_score_init(s, 0);
    assert(dse_quantize(-1, 120) == 0 && dse_quantize(179, 120) == 120 &&
           dse_quantize(180, 120) == 240);
    assert(dse_quantize(INT_MAX, 120) <= DSE_TICK_LIMIT);
    assert(dse_add_note(s, 0, -1, 120, 60) == -1 && dse_add_note(s, 0, 0, 0, 60) == -1);
    assert(dse_add_note(s, 0, 0, 120, 128) == -1 && dse_add_note(s, 1, 0, 120, 60) == -1);
    int i = dse_add_note(s, 0, 480, 480, 69);
    assert(i == 0 && s->notes[0].id == 1);
    assert(dse_add_note(s, 0, 960, 240, 72) == 1);
    dse_select(s, 0, 0);
    assert(s->notes[0].selected && !s->notes[1].selected);
    dse_select(s, 1, 1);
    assert(s->notes[0].selected && s->notes[1].selected);
    *before = *s;
    assert(dse_transform_selected(s, -1000, 0, 0) == -1 &&
           memcmp(s, before, sizeof(*s)) == 0);
    assert(dse_transform_selected(s, 120, 1, 120) == 2);
    assert(s->notes[0].pos == 600 && s->notes[0].key == 70 && s->notes[1].len == 360);
    dse_select(s, 0, 0);
    assert(dse_delete_selected(s) == 1 && s->count == 1 && s->notes[0].id == 2 &&
           s->notes[0].pos == 1080);
    dse_score_init(s, 0);
    dse_add_note(s, 0, 480, 480, 69);
    DseView v = {.width = 1000,
                 .height = 800,
                 .x = 200,
                 .y = 300,
                 .w = 700,
                 .h = 330,
                 .beat_width = 100,
                 .row_height = 20,
                 .top_key = 80,
                 .grid = 120};
    DseRect r;
    dse_note_rect(&s->notes[0], &v, &r);
    approx(r.x, 300);
    approx(r.y, 521);
    approx(r.w, 99);
    approx(r.h, 18);
    int tick, key;
    assert(dse_grid_coord(&v, 310, 530, &tick, &key) && tick == 528 && key == 69);
    assert(!dse_grid_coord(&v, NAN, 400, &tick, &key));
    assert(!dse_grid_coord(&v, 900, 400, &tick, &key));
    assert(dse_hit_note(s, &v, 310, 530) == 0 && dse_hit_note(s, &v, 310, 519) == -1);
    painted = 0;
    dse_draw_notes(s, &v, NULL);
    assert(painted == 1);
    v.scroll_tick = 1920;
    dse_draw_notes(s, &v, NULL);
    assert(painted == 1);
    float f0[101];
    dse_fill_f0(s, 0, f0, 101, 44100, 512);
    assert(f0[0] == 0 && f0[42] == 0 && f0[86] == 0);
    approx(f0[43], 440);
    approx(f0[85], 440);
    assert(dse_curve_set(s, 0, 0, 480, 69) && dse_curve_set(s, 0, 0, 960, 81));
    approx(dse_curve_value(s, 0, 0, 720, 60), 75);
    approx(dse_curve_value(s, 0, 0, 480, 60), 69);
    approx(dse_curve_value(s, 0, 0, 1000, 60), 60);
    dse_fill_f0(s, 0, f0, 101, 44100, 512);
    float expected = 440.f * exp2f(((64.f * 512 * 960 / 44100) - 480) / 480);
    assert(fabsf(f0[64] - expected) < .01f);
    char path[] = "/tmp/diffsinger-core-XXXXXX", msg[512];
    int fd = mkstemp(path);
    assert(fd >= 0);
    close(fd);
    strcpy(s->notes[0].lyric, "测试中文");
    strcpy(s->notes[0].phonemes, "zh/a");
    s->notes[0].selected = 0;
    assert(dse_score_save(s, path, msg, sizeof(msg)));
    DseScore* loaded = calloc(1, sizeof(*loaded));
    assert(loaded);
    assert(dse_score_load(loaded, path, msg, sizeof(msg)));
    assert(loaded->count == 1 && loaded->curve_count == 2 &&
           !strcmp(loaded->notes[0].lyric, "测试中文"));
    approx(loaded->bpm, 120);
    assert(loaded->notes[0].key == 69);
    *before = *loaded;
    FILE* file = fopen(path, "wb");
    assert(file);
    fputs("{\"version\":1,\"tempos\":null}", file);
    fclose(file);
    assert(!dse_score_load(loaded, path, msg, sizeof(msg)) &&
           memcmp(loaded, before, sizeof(*loaded)) == 0);
    const char* clipped =
        "{\"version\":1,\"tempos\":[{\"pos\":0,\"bpm\":120}],"
        "\"timeSignatures\":[{\"barIndex\":0,\"numerator\":4,\"denominator\":4}],"
        "\"tracks\":[{\"parts\":[{\"type\":\"midi\",\"endOffset\":120,"
        "\"notes\":[{\"pos\":0,\"dur\":480,\"pitch\":60}]}]}]}";
    file = fopen(path, "wb");
    assert(file);
    fputs(clipped, file);
    fclose(file);
    assert(!dse_score_load(loaded, path, msg, sizeof(msg)) &&
           memcmp(loaded, before, sizeof(*loaded)) == 0 && strstr(msg, "endOffset"));
    unlink(path);
    dse_score_init(s, 0);
    for (int j = 0; j < DSE_MAX_NOTES; j++)
        assert(dse_add_note(s, 0, j * 120, 120, j % 128) == j);
    *before = *s;
    assert(dse_add_note(s, 0, 0, 120, 60) == -1 && !memcmp(s, before, sizeof(*s)));
    for (unsigned j = 0; j < s->count; j++)
        s->notes[j].selected = 1;
    assert(dse_delete_selected(s) == DSE_MAX_NOTES && s->count == 0);
    AsmUiNode nodes[8];
    char strings[128];
    AsmUiCanvas canvas;
    AsmUiDrawCmd draws[8];
    AsmUiContext ctx;
    asmui_canvas_begin(&canvas, nodes, 8, strings, sizeof(strings), 1000, 800);
    int clip = asmui_canvas_clip(&canvas, 10, 20, 200, 100);
    canvas.parent = clip;
    asmui_canvas_rect(&canvas, 30, 40, 50, 20, 0xffffffff, 0);
    asm_ui_init(&ctx, nodes, 8, draws, 8, NULL, 0);
    ctx.node_count = canvas.count;
    asm_ui_set_viewport(&ctx, 1000, 800);
    asm_ui_layout(&ctx);
    asm_ui_build_drawlist(&ctx);
    assert(ctx.draw_count == 1);
    approx(draws[0].x0, 30);
    approx(draws[0].y0, 60);
    approx(draws[0].clip_x, 10);
    approx(draws[0].clip_y, 20);
    for (int j = 0; j < 10; j++)
        asmui_canvas_rect(&canvas, 0, 0, 1, 1, 0xff, 0);
    assert(canvas.overflow && canvas.count == 8);
    ustx_check(s, before);
    free(s);
    free(before);
    free(loaded);
    puts("DiffSinger-ASM core: assembly editing, geometry, F0, bounds, transactional TLP and "
         "canvas "
         "OK");
    return 0;
}
