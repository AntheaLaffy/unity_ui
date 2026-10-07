#include "asmui_builder.h"
#include "asmui_editor.h"
#include "asmui_font_sdf_ft.h"
#include "asmui_text.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static AsmUiNode nodes[EDITOR_MAX_NODES];
static AsmUiDrawCmd draws[EDITOR_MAX_DRAWS];
static void build(EditorState *e) {
    assert(editor_build(e, nodes, EDITOR_MAX_NODES, 16) < EDITOR_MAX_NODES);
}
static void click(EditorState *e, int action, int value) {
    for (uint32_t i = 0; i < e->hit_count; ++i)
        if (e->hits[i].action == action && e->hits[i].value == value) {
            EditorRect r = e->hits[i].rect;
            editor_pointer(e, r.x + r.w * .5f, r.y + r.h * .5f, 1, 1);
            editor_pointer(e, r.x + r.w * .5f, r.y + r.h * .5f, 1, 0);
            build(e);
            return;
        }
    fprintf(stderr, "Missing visible control action=%d value=%d\n", action, value);
    assert(0);
}
static void geometry(void) {
    EditorRect r = {17, 31, 800, 600};
    float p[300], out[200],
        m[16] = {1.2f, .1f, .2f, 2, .3f, 1.1f, .4f, 1, 0, 0, 1, 0, .1f, .2f, .05f, 20};
    for (int i = 0; i < 300; ++i)
        p[i] = (float)(i % 19) - 9;
    asm_ui_project_points(p, 100, m, &r, out);
    for (int i = 0; i < 100; ++i) {
        float x = p[i * 3], y = p[i * 3 + 1], z = p[i * 3 + 2],
              w = x * m[12] + y * m[13] + z * m[14] + m[15];
        float ex = r.x + r.w * .5f + (x * m[0] + y * m[1] + z * m[2] + m[3]) / w * r.w * .5f;
        float ey = r.y + r.h * .5f - (x * m[4] + y * m[5] + z * m[6] + m[7]) / w * r.h * .5f;
        assert(fabsf(ex - out[i * 2]) < .001f && fabsf(ey - out[i * 2 + 1]) < .001f);
    }
    EditorHit hits[] = {{{0, 0, 50, 50}, ED_SELECT, 0}, {{10, 10, 30, 30}, ED_MENU, 0}};
    assert(asm_ui_editor_hit(hits, 2, 20, 20) == 1);
    assert(asm_ui_editor_hit(hits, 2, 45, 45) == 0);
    assert(asm_ui_editor_hit(hits, 2, 50, 20) == -1);
    assert(asm_ui_editor_hit(hits, 0, 20, 20) == -1);
    assert(asm_ui_editor_hit(hits, 2, NAN, 20) == -1);

    AsmUiContext c;
    asm_ui_init(&c, nodes, EDITOR_MAX_NODES, draws, EDITOR_MAX_DRAWS, NULL, 0);
    asm_ui_set_viewport(&c, 800, 600);
    nodes[0] = asmui_node(ASMUI_NODE_CONTAINER, -1, 0xffffffff);
    nodes[0].size_delta_x = 200;
    nodes[0].size_delta_y = 100;
    nodes[0].anchored_y = 180;
    nodes[0].flags |= ASMUI_CLIP;
    nodes[1] = asmui_node(ASMUI_NODE_IMAGE, 0, 0xffffffff);
    nodes[1].size_delta_x = 100;
    nodes[1].size_delta_y = 40;
    nodes[1].anchored_y = 15;
    asmui_gradient_y(&nodes[1], 0xff0000ff, 0x00ff00ff);
    c.node_count = 2;
    asm_ui_layout(&c);
    asm_ui_build_drawlist(&c);
    assert(c.draw_count == 1);
    assert(fabsf(draws[0].y3 - 85) < .001f && fabsf(draws[0].y0 - 125) < .001f);
    assert(fabsf(draws[0].clip_y - 70) < .001f);
    AsmUiGpuVertex v[4];
    assert(asm_ui_build_gpu_vertices(&c, v, 4) == 4);
    assert(v[0].rgba == 0xff0000ff && v[3].rgba == 0x00ff00ff && v[3].y < v[0].y);
    puts("editor ASM: project 100 points, hit priority/edges/NaN, nested Y-up layout, gradient "
         "orientation OK");
}
static void interactions(void) {
    EditorState e;
    editor_init(&e, 1440, 900);
    build(&e);
    assert(e.locale == EDITOR_ZH_CN);
    click(&e, ED_LOCALE, 0);
    assert(e.locale == EDITOR_EN_US);
    click(&e, ED_LOCALE, 0);
    assert(e.locale == EDITOR_ZH_CN);
    click(&e, ED_SELECT, 0);
    assert(e.selected == 0);
    click(&e, ED_SELECT, 3);
    assert(e.selected == 3);
    click(&e, ED_FIELD, 0);
    editor_key(&e, 'a', 1);
    editor_text(&e, "1.25");
    editor_key(&e, 13, 0);
    build(&e);
    assert(fabsf(e.position[0] - 1.25f) < .0001f);
    for (int axis = 0; axis < 3; ++axis) {
        EditorRect before = e.object_rect;
        click(&e, ED_FIELD, 3 + axis);
        editor_key(&e, 'a', 1);
        editor_text(&e, "27");
        editor_key(&e, 13, 0);
        build(&e);
        assert(e.rotation[axis] == 27);
        assert(fabsf(before.w - e.object_rect.w) + fabsf(before.h - e.object_rect.h) > .1f);
    }
    click(&e, ED_PLAY, 0);
    assert(e.playing);
    click(&e, ED_PAUSE, 0);
    assert(e.paused);
    click(&e, ED_STEP, 0);
    assert(e.frame == 1);
    assert(!strcmp(e.values[23], "帧 1"));
    click(&e, ED_PLAY, 0);
    assert(!e.playing && !e.paused);
    click(&e, ED_GAME, 1);
    assert(e.game);
    click(&e, ED_GAME, 0);
    click(&e, ED_2D, 0);
    assert(e.two_d);
    click(&e, ED_2D, 0);
    assert(!e.two_d);
    click(&e, ED_CONSOLE, 1);
    assert(e.console);
    click(&e, ED_CONSOLE, 0);
    click(&e, ED_FOLD, 0);
    assert(!(e.folds & 1));
    click(&e, ED_FOLD, 0);
    click(&e, ED_SEARCH, 0);
    editor_composition(&e, "摄像机");
    editor_key(&e, 13, 0);
    assert(e.focus == 100 && !e.search[0]);
    build(&e);
    assert(!strcmp(e.composed, "摄像机"));
    editor_text(&e, "摄像机");
    assert(!e.composition[0]);
    assert(!strcmp(e.search, "摄像机"));
    build(&e);
    int rows = 0;
    for (uint32_t i = 0; i < e.hit_count; ++i)
        if (e.hits[i].action == ED_SELECT) {
            assert(e.hits[i].value == 0);
            ++rows;
        }
    assert(rows == 1);
    click(&e, ED_LOCALE, 0);
    assert(e.locale == EDITOR_EN_US && !strcmp(e.search, "摄像机"));
    rows = 0;
    for (uint32_t i = 0; i < e.hit_count; ++i)
        if (e.hits[i].action == ED_SELECT) {
            assert(e.hits[i].value == 0);
            ++rows;
        }
    assert(rows == 1);
    click(&e, ED_LOCALE, 0);
    click(&e, ED_SEARCH, 0);
    editor_key(&e, 8, 0);
    assert(!strcmp(e.search, "摄像"));
    editor_key(&e, 'a', 1);
    editor_text(&e, "\xe0\x80\x80\xed\xa0\x80\xf4\x90\x80\x80");
    assert(!e.search[0]);
    for (int i = 0; i < 40; ++i)
        editor_text(&e, "界");
    assert(strlen(e.search) == 63);
    editor_key(&e, 8, 0);
    assert(strlen(e.search) == 60);
    editor_key(&e, 'a', 1);
    editor_key(&e, 13, 0);
    build(&e);
    click(&e, ED_MENU, 0);
    assert(e.menu == 0);
    for (uint32_t i = 0; i < e.hit_count; ++i)
        if (e.hits[i].action == ED_MENU_ITEM)
            assert(e.hits[i].value == 0 || e.hits[i].value == 5);
    assert(asm_ui_editor_hit(e.hits, e.hit_count, 50, 65) >= 0);
    editor_key(&e, 27, 0);
    build(&e);
    assert(e.menu == -1);
    editor_resize(&e, 1024, 680);
    build(&e);
    editor_motion(&e, 1000, 500);
    editor_wheel(&e, -20);
    assert(e.inspector_scroll > 0);
    build(&e);
    for (uint32_t i = 0; i < e.hit_count; ++i) {
        const EditorHit *h = &e.hits[i];
        if (h->action == ED_FIELD)
            assert(h->rect.y >= 107 && h->rect.y + h->rect.h <= e.height - 23 + .01f);
    }
    editor_motion(&e, 110, 610);
    editor_wheel(&e, -20);
    build(&e);
    assert(e.project_scroll > 0);
    puts("editor interaction: locale, object binding, numeric edit, play/pause/step, tabs, "
         "foldouts, XYZ transform, IME preedit, UTF-8 bounds/search/backspace, clipped hits and "
         "scrolling OK");
}
static void font_coverage(void) {
    AsmUiFontSdfAtlas atlas = {0};
    size_t count;
    const char *const *samples = editor_font_samples(&count);
    assert(asmui_font_sdf_build(&atlas, editor_font(), 1, 64, samples, count));
    assert(!atlas.missing_glyph_count);
    assert(atlas.px_range == 16);
    unsigned checked = 0;
    for (int lang = 0; lang < EDITOR_LOCALE_COUNT; ++lang)
        for (int state = 0; state < 24; ++state) {
            EditorState e;
            editor_init(&e, state % 2 ? 1024 : 1440, state % 2 ? 680 : 900);
            e.locale = (EditorLocale)lang;
            if (state < 5)
                e.selected = state;
            else if (state < 13)
                e.menu = state - 5;
            else if (state == 13)
                e.console = 1;
            else if (state < 19)
                e.folder = state - 14;
            else {
                e.folder = state - 19;
                e.asset = 5;
            }
            build(&e);
            for (uint32_t i = 0; i < e.node_count; ++i)
                if (nodes[i].type == ASMUI_NODE_TEXT) {
                    hb_buffer_t *b = hb_buffer_create();
                    hb_buffer_add_utf8(b, nodes[i].payload, -1, 0, -1);
                    hb_buffer_guess_segment_properties(b);
                    hb_shape(atlas.hb_font, b, NULL, 0);
                    unsigned ng;
                    hb_glyph_info_t *g = hb_buffer_get_glyph_infos(b, &ng);
                    for (unsigned j = 0; j < ng; ++j) {
                        hb_glyph_extents_t ext;
                        if (hb_font_get_glyph_extents(atlas.hb_font, g[j].codepoint, &ext) &&
                            ext.width && ext.height) {
                            AsmUiHbAtlasGlyph gm;
                            if (!asmui_font_sdf_lookup(g[j].codepoint, &gm, &atlas)) {
                                fprintf(stderr, "Missing shaped glyph %u in %s\n", g[j].codepoint,
                                        nodes[i].payload);
                                assert(0);
                            }
                            ++checked;
                        }
                    }
                    hb_buffer_destroy(b);
                }
        }
    /* Input glyphs can be absent from a prebuilt atlas. Preserve a visible
       replacement until the host extends the texture; never silently drop text. */
    AsmUiHbShaper shaper = asmui_font_sdf_shaper(&atlas);
    AsmUiDrawCmd text_draw = {0};
    text_draw.payload = "测试";
    text_draw.font_size = 13;
    text_draw.local_w = 200;
    text_draw.local_h = 20;
    AsmUiGlyphQuad g[16];
    assert(asmui_hb_shape(&text_draw, g, 16, &shaper) == 2);
    AsmUiFontSdfAtlas input = {0};
    const char *input_texts[] = {"测试 Main Camera"};
    assert(asmui_font_sdf_build_missing(&input, editor_font(), 2, 64, input_texts, 1,
                                        asmui_font_sdf_lookup, &atlas));
    assert(input.glyph_count == 2 && !input.missing_glyph_count);
    hb_codepoint_t id;
    AsmUiHbAtlasGlyph gm;
    assert(hb_font_get_nominal_glyph(atlas.hb_font, 0x6d4b, &id));
    assert(asmui_font_sdf_lookup(id, &gm, &input) && gm.texture_id == 2);
    assert(hb_font_get_nominal_glyph(atlas.hb_font, 'M', &id));
    assert(!asmui_font_sdf_lookup(id, &gm, &input));
    asmui_font_sdf_destroy(&input);
    printf("editor localization: 2 locales x 24 states, %u shaped glyphs covered, atlas=%ux%u, "
           "missing=0, font=%s\n",
           checked, atlas.width, atlas.height, editor_font());
    asmui_font_sdf_destroy(&atlas);
}
int main(void) {
    geometry();
    interactions();
    font_coverage();
    puts("Editor core checks passed");
    return 0;
}
