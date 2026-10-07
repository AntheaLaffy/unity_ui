#include "asmui_builder.h"
#include "asmui_editor.h"
#include "asmui_font_sdf_ft.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

_Static_assert(sizeof(EditorHit) == 24, "ASM hit-test ABI");
static const uint32_t BG = 0x282828ff, PANEL = 0x383838ff, BAR = 0x303030ff;
static const uint32_t EDGE = 0x202020ff, TEXT = 0xc4c4c4ff, MUTED = 0x929292ff;
static const uint32_t BLUE = 0x2c5f94ff, ORANGE = 0xff9900ff;
static const char *objects[] = {"Main Camera", "Directional Light", "Plane", "Cube", "Sphere"};
static const char *folders[] = {"Materials", "Prefabs", "Scenes", "Scripts", "Textures"};
static const char *asset_name(const EditorState *e, int id) {
    if (e->folder < 0)
        return id < 5 ? folders[id] : "SampleScene";
    if (e->folder == 0)
        return id == 5 ? "Default" : id == 6 ? "Warm" : "Sage";
    return e->folder == 1   ? "Cube"
           : e->folder == 2 ? "SampleScene"
           : e->folder == 3 ? "PlayerController"
                            : "Grid";
}
static const char *status_text(EditorState *e) {
    if (strncmp(e->status, "Frame ", 6))
        return e->status;
    snprintf(e->values[23], sizeof(e->values[23]), "%s %.24s", editor_localize(e->locale, "Frame"),
             e->status + 6);
    return e->values[23];
}
typedef struct Ui {
    EditorState *e;
    AsmUiNode *nodes;
    uint32_t n, cap;
    float range;
    int parent;
    EditorRect clip;
} Ui;
static float clampf(float x, float a, float b) { return fminf(fmaxf(x, a), b); }

static uint32_t rect(Ui *u, float x, float y, float w, float h, uint32_t color, float radius) {
    if (u->n >= u->cap)
        return UINT32_MAX;
    AsmUiNode n = asmui_node(ASMUI_NODE_IMAGE, u->parent, color);
    float ox = u->parent < 0 ? u->e->width * .5f : u->clip.x + u->clip.w * .5f;
    float oy = u->parent < 0 ? u->e->height * .5f : u->clip.y + u->clip.h * .5f;
    n.anchored_x = x + w * .5f - ox;
    n.anchored_y = oy - y - h * .5f;
    n.size_delta_x = fmaxf(w, 0);
    n.size_delta_y = fmaxf(h, 0);
    if (radius > 0)
        asmui_material_rounded(&n, radius);
    u->nodes[u->n] = n;
    return u->n++;
}
static void label(Ui *u, const char *s, float x, float y, float w, float h, uint32_t c,
                  float size) {
    uint32_t i = rect(u, x, y, w, h, c, 0);
    if (i == UINT32_MAX)
        return;
    AsmUiNode *n = &u->nodes[i];
    n->type = ASMUI_NODE_TEXT;
    n->resource_id = 1;
    n->payload = editor_localize(u->e->locale, s);
    n->font_size = size >= 12 ? 13 : size;
    n->flags |= ASMUI_CLIP;
    asmui_text_msdf(n, u->range, 0, 0);
    ++u->e->text_count;
}
static void text(Ui *u, const char *s, float x, float y, float w, float h) {
    label(u, s, x, y, w, h, TEXT, 12);
}
static void centered(Ui *u, const char *s, float x, float y, float w, float h, uint32_t c) {
    label(u, s, x, y, w, h, c, 12);
    if (u->n)
        u->nodes[u->n - 1].text_align = ASMUI_TEXT_CENTER;
}
static void line(Ui *u, float x, float y, float x2, float y2, float thickness, uint32_t c) {
    float dx = x2 - x, dy = y2 - y, l = sqrtf(dx * dx + dy * dy);
    uint32_t i =
        rect(u, (x + x2 - l) * .5f, (y + y2 - thickness - 2) * .5f, l, thickness + 2, c, 0);
    if (i != UINT32_MAX) {
        u->nodes[i].rotation = -atan2f(dy, dx);
        asmui_set_material4(&u->nodes[i], ASMUI_MATERIAL_LINE, thickness, 0, 0, 0);
    }
}
static void outline(Ui *u, float x, float y, float w, float h, uint32_t c) {
    rect(u, x, y, w, 1, c, 0);
    rect(u, x, y + h - 1, w, 1, c, 0);
    rect(u, x, y, 1, h, c, 0);
    rect(u, x + w - 1, y, 1, h, c, 0);
}
static void quad(Ui *u, const float *xy, uint32_t c) {
    if (u->e->quad_count >= EDITOR_MAX_QUADS)
        return;
    uint32_t i = rect(u, 0, 0, 1, 1, c, 0);
    if (i == UINT32_MAX)
        return;
    EditorQuad *q = &u->e->quads[u->e->quad_count++];
    q->node = i;
    memcpy(q->xy, xy, sizeof(q->xy));
}
static void triangle(Ui *u, float x, float y, float size, int down, uint32_t c) {
    float p[8];
    if (down) {
        float t[] = {
            x, y, x + size, y, x + size * .5f, y + size * .65f, x + size * .5f, y + size * .65f};
        memcpy(p, t, sizeof(p));
    } else {
        float t[] = {
            x, y, x, y + size, x + size * .65f, y + size * .5f, x + size * .65f, y + size * .5f};
        memcpy(p, t, sizeof(p));
    }
    quad(u, p, c);
}
static void circle(Ui *u, float x, float y, float size, uint32_t c) {
    rect(u, x, y, size, size, c, size * .5f);
}
static void icon(Ui *u, int type, float x, float y, float sz, uint32_t c) {
    float s = sz / 16.0f;
#define L(A, B, C, D) line(u, x + (A) * s, y + (B) * s, x + (C) * s, y + (D) * s, 1.15f, c)
    if (type == 0) { /* cube */
        L(8, 1, 15, 5);
        L(15, 5, 15, 12);
        L(15, 12, 8, 16);
        L(8, 16, 1, 12);
        L(1, 12, 1, 5);
        L(1, 5, 8, 1);
        L(1, 5, 8, 9);
        L(15, 5, 8, 9);
        L(8, 9, 8, 16);
    } else if (type == 1) { /* camera */
        outline(u, x + 2 * s, y + 4 * s, 10 * s, 9 * s, c);
        L(12, 7, 16, 4);
        L(16, 4, 16, 13);
        L(16, 13, 12, 10);
        L(4, 2, 8, 2);
    } else if (type == 2) { /* light */
        circle(u, x + 5 * s, y + 5 * s, 6 * s, c);
        for (int k = 0; k < 8; ++k) {
            float a = k * .785398f;
            line(u, x + 8 * s + cosf(a) * 5 * s, y + 8 * s + sinf(a) * 5 * s,
                 x + 8 * s + cosf(a) * 8 * s, y + 8 * s + sinf(a) * 8 * s, 1, c);
        }
    } else if (type == 3) { /* folder */
        rect(u, x, y + 2 * s, 7 * s, 4 * s, c, s);
        rect(u, x, y + 5 * s, 16 * s, 10 * s, c, s);
        rect(u, x + s, y + 6 * s, 14 * s, s, 0xffffff30, 0);
    } else if (type == 4) { /* search */
        circle(u, x + 1 * s, y + 1 * s, 10 * s, c);
        circle(u, x + 2.5f * s, y + 2.5f * s, 7 * s, BAR);
        L(10, 10, 15, 15);
    } else if (type == 5) { /* move */
        L(8, 1, 8, 15);
        L(1, 8, 15, 8);
        L(8, 1, 5, 4);
        L(8, 1, 11, 4);
        L(15, 8, 12, 5);
        L(15, 8, 12, 11);
        L(8, 15, 5, 12);
        L(8, 15, 11, 12);
        L(1, 8, 4, 5);
        L(1, 8, 4, 11);
    } else if (type == 6) { /* rotate */
        for (int i = 0; i < 13; ++i) {
            float a = i * .39f, b = (i + 1) * .39f;
            line(u, x + 8 * s + 6 * s * cosf(a), y + 8 * s + 6 * s * sinf(a),
                 x + 8 * s + 6 * s * cosf(b), y + 8 * s + 6 * s * sinf(b), 1.2f, c);
        }
        L(11, 1, 15, 3);
        L(15, 3, 14, 7);
    } else if (type == 7) { /* scale */
        outline(u, x + 2 * s, y + 8 * s, 6 * s, 6 * s, c);
        L(7, 9, 14, 2);
        L(10, 2, 14, 2);
        L(14, 2, 14, 6);
    } else if (type == 8) { /* hand */
        L(5, 14, 2, 9);
        L(2, 9, 4, 8);
        L(4, 8, 6, 10);
        L(6, 10, 6, 3);
        L(6, 3, 8, 3);
        L(8, 3, 8, 9);
        L(8, 7, 11, 5);
        L(11, 5, 14, 7);
        L(14, 7, 13, 13);
        L(13, 13, 10, 15);
        L(10, 15, 5, 14);
    } else if (type == 9) { /* grid/rect */
        outline(u, x + 2 * s, y + 2 * s, 12 * s, 12 * s, c);
        L(2, 8, 14, 8);
        L(8, 2, 8, 14);
    } else if (type == 10) { /* document */
        outline(u, x + 3 * s, y + 1 * s, 10 * s, 14 * s, c);
        L(5, 6, 11, 6);
        L(5, 9, 11, 9);
        L(5, 12, 9, 12);
    } else if (type == 11) { /* lock */
        outline(u, x + 5 * s, y + 1 * s, 6 * s, 8 * s, c);
        rect(u, x + 3 * s, y + 7 * s, 10 * s, 8 * s, c, 1);
    } else {
        circle(u, x + 3 * s, y + 3 * s, 10 * s, c);
    }
#undef L
}
static void clip_begin(Ui *u, EditorRect r) {
    u->parent = -1;
    uint32_t i = rect(u, r.x, r.y, r.w, r.h, 0xffffffff, 0);
    if (i == UINT32_MAX)
        return;
    u->nodes[i].type = ASMUI_NODE_CONTAINER;
    u->nodes[i].flags |= ASMUI_CLIP;
    u->parent = (int)i;
    u->clip = r;
}
static void clip_end(Ui *u) { u->parent = -1; }
static int hit(Ui *u, float x, float y, float w, float h, int action, int val) {
    EditorState *e = u->e;
    if (u->parent >= 0) {
        float x1 = fminf(x + w, u->clip.x + u->clip.w), y1 = fminf(y + h, u->clip.y + u->clip.h);
        x = fmaxf(x, u->clip.x);
        y = fmaxf(y, u->clip.y);
        w = x1 - x;
        h = y1 - y;
    }
    if (w <= 0 || h <= 0)
        return 0;
    if (e->hit_count >= EDITOR_MAX_HITS)
        return 0;
    uint32_t i = e->hit_count++;
    e->hits[i] = (EditorHit){{x, y, w, h}, action, val};
    return e->mouse_x >= x && e->mouse_x < x + w && e->mouse_y >= y && e->mouse_y < y + h;
}
static void button(Ui *u, const char *s, float x, float y, float w, float h, int action, int val,
                   int active) {
    int over = hit(u, x, y, w, h, action, val);
    rect(u, x, y, w, h, active ? BLUE : over ? 0x555555ff : 0x414141ff, 2);
    outline(u, x, y, w, h, EDGE);
    centered(u, s, x, y, w, h, TEXT);
}
static void field(Ui *u, const char *s, float x, float y, float w, int action, int val) {
    int over = hit(u, x, y, w, 18, action, val);
    int editing =
        (action == ED_FIELD && u->e->focus == val) || (action == ED_SEARCH && u->e->focus == 100);
    rect(u, x, y, w, 18, 0x292929ff, 2);
    outline(u, x, y, w, 18, editing ? 0x4c83b7ff : over ? 0x666666ff : 0x222222ff);
    const char *shown = s;
    if (editing && u->e->composition[0]) {
        snprintf(u->e->composed, sizeof(u->e->composed), "%s%s", s, u->e->composition);
        shown = u->e->composed;
    }
    text(u, shown, x + 5, y, w - 9, 18);
    if (editing && u->n) {
        AsmUiNode *n = &u->nodes[u->n - 1];
        n->payload = shown;
        float advance =
            u->e->measure_text ? u->e->measure_text(shown, n->font_size, u->e->measure_user) : 0;
        if (advance > w - 10)
            n->text_align = ASMUI_TEXT_RIGHT;
        u->e->input_rect = (EditorRect){x, y, w, 18};
        u->e->caret_offset = 5 + fminf(advance, w - 10);
        rect(u, x + u->e->caret_offset, y + 3, 1, 12, 0xd0d0d0ff, 0);
    }
    if (editing && u->e->composition[0])
        rect(u, x + 4, y + 16, w - 8, 1, 0x7ba2ceff, 0);
}
static void check(Ui *u, float x, float y, int on, int action, int val) {
    hit(u, x - 2, y - 2, 18, 18, action, val);
    rect(u, x, y, 13, 13, 0x252525ff, 2);
    outline(u, x, y, 13, 13, 0x1e1e1eff);
    if (on) {
        line(u, x + 2, y + 6, x + 5, y + 9, 1.6f, TEXT);
        line(u, x + 5, y + 9, x + 11, y + 3, 1.6f, TEXT);
    }
}
static void dock_header(Ui *u, const char *s, float x, float y, float w, int icon_type) {
    rect(u, x, y, w, 24, BAR, 0);
    rect(u, x, y, 114, 23, PANEL, 0);
    icon(u, icon_type, x + 7, y + 6, 12, MUTED);
    text(u, s, x + 25, y + 1, 95, 22);
    circle(u, x + w - 10, y + 7, 2, MUTED);
    circle(u, x + w - 10, y + 11, 2, MUTED);
    circle(u, x + w - 10, y + 15, 2, MUTED);
    rect(u, x, y + 23, w, 1, EDGE, 0);
}

void editor_init(EditorState *e, float w, float h) {
    memset(e, 0, sizeof(*e));
    e->selected = 3;
    e->asset = -1;
    e->folder = -1;
    e->tool = 1;
    e->enabled = 31;
    e->scene_open = e->env_open = 1;
    e->menu = e->focus = -1;
    e->mouse_x = e->mouse_y = -100;
    e->left = 236;
    e->right = 326;
    e->bottom = 246;
    e->yaw = .70f;
    e->pitch = .36f;
    e->zoom = 7.2f;
    e->position[1] = .5f;
    e->scale[0] = e->scale[1] = e->scale[2] = 1;
    e->folds = 15;
    snprintf(e->status, sizeof(e->status), "SampleScene loaded");
    editor_resize(e, w, h);
}
void editor_resize(EditorState *e, float w, float h) {
    e->width = w;
    e->height = h;
    e->left = clampf(e->left, 160, w * .3f);
    e->right = clampf(e->right, 280, w * .4f);
    e->bottom = clampf(e->bottom, 150, h * .48f);
}
const char *editor_font(void) {
    const char *p = getenv("ASMUI_FONT");
    if (p && *p && access(p, R_OK) == 0)
        return p;
    static char lxgw[4096];
    const char *data = getenv("XDG_DATA_HOME"), *user_home = getenv("HOME");
    if (data && snprintf(lxgw, sizeof(lxgw), "%s/fonts/LXGWWenKai-Regular.ttf", data) > 0 &&
        access(lxgw, R_OK) == 0)
        return lxgw;
    if (user_home &&
        snprintf(lxgw, sizeof(lxgw), "%s/.local/share/fonts/LXGWWenKai-Regular.ttf", user_home) >
            0 &&
        access(lxgw, R_OK) == 0)
        return lxgw;
    /* The shared finder prefers CJK-capable Noto faces before Latin-only fonts. */
    return asmui_font_find_default();
}
const char *const *editor_font_samples(size_t *count) { return editor_language_samples(count); }

static void camera_matrix(EditorState *e, EditorRect r, float *m) {
    memset(m, 0, 16 * sizeof(float));
    if (e->two_d && !e->game) {
        m[0] = 2.f / (e->zoom * r.w / r.h);
        m[5] = 2.f / e->zoom;
        m[7] = -.35f * m[5];
        m[15] = 1;
        return;
    }
    float cy = cosf(e->yaw), sy = sinf(e->yaw), cp = cosf(e->pitch), sp = sinf(e->pitch);
    float f = 1.9f, aspect = r.w / r.h;
    m[0] = cy * f / aspect;
    m[2] = -sy * f / aspect;
    m[4] = -sy * sp * f;
    m[5] = cp * f;
    m[6] = -cy * sp * f;
    m[7] = -.35f * cp * f;
    m[12] = -sy * cp;
    m[13] = -sp;
    m[14] = -cy * cp;
    m[15] = e->zoom + .35f * sp;
}
static void scene(Ui *u, EditorRect r) {
    EditorState *e = u->e;
    e->scene_rect = r;
    clip_begin(u, r);
    hit(u, r.x, r.y, r.w, r.h, ED_SCENE, 0);
    if (e->game && e->application_build) {
        rect(u, r.x, r.y, r.w, r.h, 0x000000ff, 0);
        u->n = e->application_build(e->application_user, u->nodes, u->n, u->cap, u->parent, r);
        clip_end(u);
        return;
    }
    uint32_t bg = rect(u, r.x, r.y, r.w, r.h, 0x6f8395ff, 0);
    if (bg != UINT32_MAX)
        asmui_gradient_y(&u->nodes[bg], 0xb6c2caff, 0x61778fff);
    uint32_t grid = rect(u, r.x, r.y, r.w, r.h, 0xffffffff, 0);
    if (grid != UINT32_MAX)
        asmui_set_material4(&u->nodes[grid],
                            e->game    ? ASMUI_MATERIAL_GROUND
                            : e->two_d ? ASMUI_MATERIAL_GRID_2D
                                       : ASMUI_MATERIAL_GRID,
                            e->yaw, e->pitch, e->zoom, 1.9f);
    float m[16];
    camera_matrix(e, r, m);
    /* A real projected mesh keeps orbit, zoom, Inspector edits and outline
       selection coherent. The default scene is deliberately asset-free. */
    float xyz[24], xy[16];
    float ax = e->rotation[0] * .0174532925f, ay = e->rotation[1] * .0174532925f,
          az = e->rotation[2] * .0174532925f;
    float cx = cosf(ax), sx = sinf(ax), cy = cosf(ay), sy = sinf(ay), cz = cosf(az), sz = sinf(az);
    for (int i = 0; i < 8; ++i) {
        float x = ((i & 1) ? .5f : -.5f) * e->scale[0], y = ((i & 2) ? .5f : -.5f) * e->scale[1],
              z = ((i & 4) ? .5f : -.5f) * e->scale[2];
        /* Unity's Euler order is Z, X, Y. Keep all three Inspector axes live. */
        float zx = x * cz - y * sz, zy = x * sz + y * cz;
        float ry = zy * cx - z * sx, rz = zy * sx + z * cx;
        xyz[i * 3] = zx * cy + rz * sy + e->position[0];
        xyz[i * 3 + 1] = ry + e->position[1];
        xyz[i * 3 + 2] = -zx * sy + rz * cy + e->position[2];
    }
    asm_ui_project_points(xyz, 8, m, &r, xy);
    float xmin = xy[0], xmax = xy[0], ymin = xy[1], ymax = xy[1];
    for (int i = 1; i < 8; ++i) {
        xmin = fminf(xmin, xy[i * 2]);
        xmax = fmaxf(xmax, xy[i * 2]);
        ymin = fminf(ymin, xy[i * 2 + 1]);
        ymax = fmaxf(ymax, xy[i * 2 + 1]);
    }
    e->object_rect = (EditorRect){xmin, ymin, xmax - xmin, ymax - ymin};
    if (e->enabled & (1 << 3)) {
        float shadowxyz[] = {-.65f + e->position[0], .002f, -.5f + e->position[2],
                             1.2f + e->position[0],  .002f, -.5f + e->position[2],
                             1.55f + e->position[0], .002f, 1.5f + e->position[2],
                             -.25f + e->position[0], .002f, 1.5f + e->position[2]},
              shadow[8];
        asm_ui_project_points(shadowxyz, 4, m, &r, shadow);
        quad(u, shadow, 0x24242465);
        static const int faces[6][4] = {{0, 4, 6, 2}, {1, 3, 7, 5}, {0, 1, 5, 4},
                                        {2, 6, 7, 3}, {0, 2, 3, 1}, {4, 5, 7, 6}};
        static const uint32_t colors[] = {0x888c91ff, 0x9e9fa2ff, 0x72767bff,
                                          0xd9daddff, 0x989b9fff, 0xb6b8bbff};
        float depths[6];
        int order[6];
        for (int f = 0; f < 6; ++f) {
            order[f] = f;
            depths[f] = 0;
            for (int j = 0; j < 4; ++j) {
                int v = faces[f][j];
                depths[f] += xyz[v * 3] * m[12] + xyz[v * 3 + 1] * m[13] + xyz[v * 3 + 2] * m[14];
            }
        }
        for (int i = 0; i < 6; ++i)
            for (int j = i + 1; j < 6; ++j)
                if (depths[order[j]] > depths[order[i]]) {
                    int a = order[i];
                    order[i] = order[j];
                    order[j] = a;
                }
        for (int o = 0; o < 6; ++o) {
            int f = order[o];
            float q[8];
            for (int j = 0; j < 4; ++j) {
                int v = faces[f][j];
                q[j * 2] = xy[v * 2];
                q[j * 2 + 1] = xy[v * 2 + 1];
            }
            quad(u, q, colors[f]);
            if (e->selected == 3 && !e->game) {
                for (int j = 0; j < 4; ++j) {
                    int k = (j + 1) % 4;
                    line(u, q[j * 2], q[j * 2 + 1], q[k * 2], q[k * 2 + 1], 1.25f, ORANGE);
                }
            }
        }
        if (e->selected == 3 && !e->game) {
            float origin[] = {e->position[0], e->position[1], e->position[2]}, p[2];
            asm_ui_project_points(origin, 1, m, &r, p);
            if (e->tool == 2) {
                for (int k = 0; k < 48; ++k) {
                    float a = k * .1309f, b = (k + 1) * .1309f;
                    line(u, p[0] + 48 * cosf(a), p[1] + 48 * sinf(a), p[0] + 48 * cosf(b),
                         p[1] + 48 * sinf(b), 1.4f, 0x78bdf2ff);
                }
            } else if (e->tool != 0) {
                float axis[] = {origin[0] + 1.3f, origin[1],        origin[2],
                                origin[0],        origin[1] + 1.3f, origin[2],
                                origin[0],        origin[1],        origin[2] + 1.3f},
                      a[6];
                asm_ui_project_points(axis, 3, m, &r, a);
                uint32_t cs[] = {0xe55a4fff, 0x8cca57ff, 0x5797e8ff};
                for (int i = 0; i < 3; ++i) {
                    if (e->two_d && i == 2)
                        continue;
                    line(u, p[0], p[1], a[i * 2], a[i * 2 + 1], 2, cs[i]);
                    circle(u, a[i * 2] - 4, a[i * 2 + 1] - 4, 8, cs[i]);
                }
                rect(u, p[0] - 3, p[1] - 3, 6, 6, 0xfff0b4ff, 0);
            }
        }
    }
    if (!e->game) {
        float gx = r.x + r.w - 59, gy = r.y + 54;
        line(u, gx, gy, gx, gy - 27, 2, 0x8cca57ff);
        line(u, gx, gy, gx + 27, gy + (e->two_d ? 0 : 10), 2, 0xe97763ff);
        if (!e->two_d)
            line(u, gx, gy, gx - 22, gy + 15, 2, 0x5797e8ff);
        circle(u, gx - 5, gy - 5, 10, 0xd8d8d8ff);
        centered(u, "Y", gx - 8, gy - 47, 16, 18, 0xb5e982ff);
        centered(u, "X", gx + 23, gy + (e->two_d ? -8 : 9), 16, 18, 0xff9580ff);
        if (!e->two_d)
            centered(u, "Z", gx - 36, gy + 15, 16, 18, 0x78bdf2ff);
        centered(u, e->two_d ? "2D" : "Persp", gx - 25, gy + 38, 50, 20, TEXT);
        rect(u, r.x + 8, r.y + 8, 32, 166, 0x333333f2, 3);
        int types[] = {8, 5, 6, 7, 9, 0};
        for (int i = 0; i < 6; ++i) {
            int over = hit(u, r.x + 10, r.y + 10 + i * 27, 28, 26, ED_TOOL, i);
            rect(u, r.x + 10, r.y + 10 + i * 27, 28, 26,
                 e->tool == i ? BLUE
                 : over       ? 0x555555ff
                              : 0x383838ff,
                 2);
            icon(u, types[i], r.x + 16, r.y + 15 + i * 27, 16, TEXT);
        }
        rect(u, r.x + 9, r.y + r.h - 28, 148, 22, 0x333333e8, 2);
        text(u, e->two_d ? "Orthographic" : "Perspective", r.x + 17, r.y + r.h - 27, 88, 20);
        text(u, "Shaded", r.x + 104, r.y + r.h - 27, 51, 20);
    }
    clip_end(u);
}

static void hierarchy(Ui *u, float bottom) {
    EditorState *e = u->e;
    float w = e->left;
    rect(u, 0, 82, w, bottom - 82, BG, 0);
    dock_header(u, "Hierarchy", 0, 82, w, 10);
    rect(u, 0, 106, w, 24, BAR, 0);
    text(u, "+", 8, 107, 20, 21);
    triangle(u, 29, 116, 6, 1, MUTED);
    field(u, e->search[0] || e->focus == 100 ? e->search : "Search", 48, 109, w - 56, ED_SEARCH, 0);
    clip_begin(u, (EditorRect){0, 131, w, bottom - 131});
    hit(u, 0, 132, w, 20, ED_SCENE_OPEN, 0);
    triangle(u, 8, 139, 7, e->scene_open, TEXT);
    icon(u, 0, 22, 135, 13, MUTED);
    text(u, "SampleScene", 42, 131, w - 48, 22);
    float y = 155;
    if (e->scene_open) {
        for (int i = 0; i < 5; ++i) {
            if (i == 2) {
                hit(u, 0, y, w, 20, ED_ENV_OPEN, 0);
                triangle(u, 24, y + 8, 7, e->env_open, MUTED);
                icon(u, 3, 38, y + 4, 13, MUTED);
                text(u, "Environment", 58, y, w - 62, 20);
                y += 20;
            }
            if (i >= 2 && !e->env_open)
                continue;
            if (e->search[0] && !strstr(objects[i], e->search) &&
                !strstr(editor_localize(EDITOR_ZH_CN, objects[i]), e->search))
                continue;
            int over = hit(u, 0, y, w, 20, ED_SELECT, i);
            if (e->selected == i && e->asset < 0)
                rect(u, 0, y, w, 20, BLUE, 0);
            else if (over)
                rect(u, 0, y, w, 20, 0x414141ff, 0);
            float ix = i < 2 ? 39 : 55;
            icon(u, i == 0 ? 1 : i == 1 ? 2 : 0, ix, y + 3, 13, i == 3 ? 0xa9c5e4ff : MUTED);
            label(u, objects[i], ix + 21, y, w - ix - 27, 20, e->selected == i ? 0xffffffff : TEXT,
                  12);
            y += 20;
        }
    }
    clip_end(u);
    rect(u, w - 1, 82, 1, bottom - 82, EDGE, 0);
}
static float component(Ui *u, const char *s, float x, float y, float w, int bit, int type) {
    int open = (u->e->folds & (1u << bit)) != 0;
    hit(u, x, y, w, 24, ED_FOLD, bit);
    rect(u, x, y, w, 24, 0x3f3f3fff, 0);
    rect(u, x, y, w, 1, EDGE, 0);
    triangle(u, x + 7, y + 9, 7, open, TEXT);
    icon(u, type, x + 23, y + 5, 14, TEXT);
    text(u, s, x + 45, y + 1, w - 67, 22);
    text(u, ":", x + w - 16, y, w > 20 ? 12 : 0, 22);
    return y + 24;
}
static void triple(Ui *u, const char *s, float x, float y, float w, int row, const float *values) {
    text(u, s, x + 16, y, 72, 20);
    float fx = x + 95, fw = (w - 108) / 3;
    const char *axis[] = {"X", "Y", "Z"};
    for (int j = 0; j < 3; ++j) {
        int v = row * 3 + j;
        snprintf(u->e->values[v], sizeof(u->e->values[v]), "%.3g", values[j]);
        label(u, axis[j], fx + j * fw, y, 13, 20,
              j == 0   ? 0xc88280ff
              : j == 1 ? 0x92ba7aff
                       : 0x7e9ebfff,
              11);
        field(u, u->e->focus == v ? u->e->edit : u->e->values[v], fx + 13 + j * fw, y + 1, fw - 17,
              u->e->selected == 3 && row < 3 ? ED_FIELD : ED_NONE, v);
    }
}
static void inspector(Ui *u) {
    EditorState *e = u->e;
    float x = e->width - e->right, w = e->right, y = 108 - e->inspector_scroll;
    rect(u, x, 82, w, e->height - 104, PANEL, 0);
    dock_header(u, "Inspector", x, 82, w, 10);
    icon(u, 11, x + w - 31, 88, 10, MUTED);
    clip_begin(u, (EditorRect){x + 1, 107, w - 2, e->height - 130});
    const char *name = e->asset >= 0      ? asset_name(e, e->asset)
                       : e->selected >= 0 ? objects[e->selected]
                                          : "SampleScene";
    icon(u,
         e->asset >= 0      ? 3
         : e->selected == 0 ? 1
         : e->selected == 1 ? 2
                            : 0,
         x + 14, y + 11, 25, TEXT);
    check(u, x + 52, y + 10, e->selected >= 0 && (e->enabled & (1 << e->selected)),
          e->asset < 0 ? ED_ACTIVE : ED_NONE, 0);
    field(u, name, x + 72, y + 7, w - 135, ED_NONE, -2);
    text(u, "Static", x + w - 54, y + 7, 49, 20);
    text(u, "Tag", x + 13, y + 36, 28, 20);
    field(u, "Untagged", x + 47, y + 37, (w - 94) / 2, ED_NONE, -2);
    text(u, "Layer", x + w * .52f, y + 36, 37, 20);
    field(u, "Default", x + w * .52f + 37, y + 37, w * .48f - 47, ED_NONE, -2);
    y += 66;
    if (e->asset >= 0) {
        const char *kind = e->folder < 0 && e->asset < 5 ? "Folder Asset"
                           : e->folder == 0              ? "Material Asset"
                           : e->folder == 1              ? "Prefab Asset"
                           : e->folder == 3              ? "Script Asset"
                           : e->folder == 4              ? "Texture Asset"
                                                         : "Scene Asset";
        text(u, kind, x + 17, y, w - 34, 23);
        text(u, "Open", x + 17, y + 32, w - 34, 24);
        text(u, "Asset Labels", x + 17, y + 80, w - 34, 24);
    } else if (e->selected >= 0) {
        y = component(u, "Transform", x, y, w, 0, 5);
        if (e->folds & 1) {
            float zero[] = {0, 0, 0}, one[] = {1, 1, 1};
            triple(u, "Position", x, y + 4, w, 0, e->selected == 3 ? e->position : zero);
            triple(u, "Rotation", x, y + 27, w, 1, e->selected == 3 ? e->rotation : zero);
            triple(u, "Scale", x, y + 50, w, 2, e->selected == 3 ? e->scale : one);
            y += 79;
        }
        if (e->selected == 0 || e->selected == 1) {
            y = component(u, e->selected == 0 ? "Camera" : "Light", x, y, w, 1,
                          e->selected == 0 ? 1 : 2);
            if (e->folds & 2) {
                const char *ks0[] = {"Clear Flags", "Background",    "Culling Mask",
                                     "Projection",  "Field of View", "Clipping Planes"};
                const char *vs0[] = {"Skybox",      "",   "Everything",
                                     "Perspective", "60", "Near  0.3     Far  1000"};
                const char *ks1[] = {
                    "Type", "Color", "Mode", "Intensity", "Indirect Multiplier", "Shadow Type"};
                const char *vs1[] = {"Directional", "", "Realtime", "1", "1", "Soft Shadows"};
                for (int i = 0; i < 6; ++i) {
                    text(u, e->selected == 0 ? ks0[i] : ks1[i], x + 16, y + 6 + i * 24, 118, 20);
                    field(u, e->selected == 0 ? vs0[i] : vs1[i], x + 141, y + 7 + i * 24, w - 154,
                          ED_NONE, -2);
                    if (i == 1)
                        rect(u, x + 144, y + 10 + i * 24, w - 160, 12,
                             e->selected == 0 ? 0x7491b1ff : 0xffefd2ff, 0);
                }
                y += 157;
            }
            y = component(u, e->selected == 0 ? "Audio Listener" : "Additional Light Data", x, y, w,
                          2, 2);
        } else {
            y = component(u,
                          e->selected == 3   ? "Cube (Mesh Filter)"
                          : e->selected == 4 ? "Sphere (Mesh Filter)"
                                             : "Plane (Mesh Filter)",
                          x, y, w, 1, 0);
            if (e->folds & 2) {
                text(u, "Mesh", x + 16, y + 5, 90, 20);
                field(u,
                      e->selected == 3   ? "Cube"
                      : e->selected == 4 ? "Sphere"
                                         : "Plane",
                      x + 112, y + 6, w - 125, ED_NONE, -2);
                y += 35;
            }
            y = component(u, "Mesh Renderer", x, y, w, 2, 9);
            if (e->folds & 4) {
                text(u, "Materials", x + 20, y + 4, 110, 21);
                triangle(u, x + 7, y + 12, 6, 1, MUTED);
                text(u, "Element 0", x + 28, y + 28, 90, 20);
                field(u, "Default-Material", x + 112, y + 28, w - 125, ED_NONE, -2);
                text(u, "Lighting", x + 20, y + 55, w - 32, 21);
                triangle(u, x + 7, y + 64, 6, 1, MUTED);
                text(u, "Cast Shadows", x + 28, y + 80, 108, 20);
                field(u, "On", x + 144, y + 80, w - 157, ED_NONE, -2);
                text(u, "Receive Shadows", x + 28, y + 104, 119, 20);
                check(u, x + 150, y + 108, 1, ED_NONE, 0);
                text(u, "Contribute GI", x + 28, y + 128, 119, 20);
                check(u, x + 150, y + 132, 0, ED_NONE, 0);
                text(u, "Probes", x + 20, y + 155, w - 32, 22);
                triangle(u, x + 7, y + 164, 6, 0, MUTED);
                text(u, "Additional Settings", x + 20, y + 178, w - 32, 22);
                triangle(u, x + 7, y + 187, 6, 0, MUTED);
                y += 209;
            }
            y = component(u, e->selected == 4 ? "Sphere Collider" : "Box Collider", x, y, w, 3, 0);
            if (e->folds & 8) {
                text(u, "Is Trigger", x + 16, y + 5, 100, 20);
                check(u, x + 150, y + 9, 0, ED_NONE, 0);
                text(u, "Material", x + 16, y + 29, 100, 20);
                field(u, "None (Physics Material)", x + 112, y + 30, w - 125, ED_NONE, -2);
                float zero[] = {0, 0, 0}, one[] = {1, 1, 1};
                triple(u, "Center", x, y + 55, w, 3, zero);
                triple(u, "Size", x, y + 78, w, 4, one);
                y += 108;
            }
        }
        button(u, "Add Component", x + 43, y + 15, w - 86, 25, ED_MENU, 7, 0);
    }
    clip_end(u);
    e->inspector_max = fmaxf(0, y + 55 + e->inspector_scroll - (e->height - 23));
    if (e->inspector_max > 0) {
        float track = e->height - 134, thumb = track * track / (track + e->inspector_max);
        rect(u, x + w - 7, 110, 6, track, 0x2b2b2bff, 2);
        rect(u, x + w - 7, 110 + (track - thumb) * e->inspector_scroll / e->inspector_max, 6, thumb,
             0x626262ff, 2);
    }
    rect(u, x, 82, 1, e->height - 104, EDGE, 0);
}

static void project(Ui *u, float top, float right) {
    EditorState *e = u->e;
    float height = e->height - top - 22;
    rect(u, 0, top, right, height, BAR, 0);
    rect(u, 0, top, right, 24, 0x292929ff, 0);
    int over = hit(u, 0, top, 95, 24, ED_CONSOLE, 0);
    rect(u, 0, top, 95, 23, !e->console ? PANEL : over ? 0x414141ff : BAR, 0);
    icon(u, 3, 9, top + 6, 13, MUTED);
    text(u, "Project", 29, top + 1, 65, 22);
    over = hit(u, 95, top, 98, 24, ED_CONSOLE, 1);
    rect(u, 95, top, 98, 23, e->console ? PANEL : over ? 0x414141ff : BAR, 0);
    icon(u, 10, 103, top + 6, 12, MUTED);
    text(u, "Console", 123, top + 1, 65, 22);
    rect(u, 0, top + 23, right, 1, EDGE, 0);
    rect(u, 0, top + 24, right, 24, 0x383838ff, 0);
    if (e->console) {
        button(u, "Clear", 7, top + 27, 47, 18, ED_CLEAR, 0, 0);
        text(u, "Collapse", 69, top + 26, 74, 21);
        text(u, "Clear on Play", 154, top + 26, 100, 21);
        text(u, "1", right - 103, top + 26, 24, 21);
        circle(u, right - 120, top + 32, 10, 0x9da5b1ff);
        text(u, "0", right - 67, top + 26, 24, 21);
        triangle(u, right - 82, top + 32, 10, 0, 0xd3b65fff);
        text(u, "0", right - 25, top + 26, 20, 21);
        circle(u, right - 39, top + 32, 10, 0xc35a5aff);
        rect(u, 0, top + 49, right, 40, 0x343434ff, 0);
        circle(u, 10, top + 63, 13, 0xa1b6caff);
        text(u, status_text(e), 34, top + 51, right - 48, 24);
        label(u, "UnityEditor.EditorApplication:Update ()", 34, top + 72, right - 48, 17, MUTED,
              11);
        return;
    }
    text(u, "+", 8, top + 25, 18, 22);
    triangle(u, 28, top + 33, 6, 1, MUTED);
    text(u, "<   >", 52, top + 25, 50, 22);
    rect(u, right - 330, top + 28, 242, 17, 0x242424ff, 7);
    icon(u, 4, right - 323, top + 30, 12, MUTED);
    text(u, "Search", right - 305, top + 26, 210, 22);
    text(u, "t:    l:", right - 78, top + 25, 70, 22);
    float split = 190;
    rect(u, 0, top + 48, split, height - 48, BG, 0);
    rect(u, split - 1, top + 48, 1, height - 48, EDGE, 0);
    clip_begin(u, (EditorRect){0, top + 48, split - 1, height - 49});
    float tree_top = top - e->project_scroll;
    triangle(u, 7, tree_top + 60, 7, 1, MUTED);
    text(u, "Favorites", 22, tree_top + 51, 155, 22);
    const char *favorites[] = {"All Materials", "All Models", "All Prefabs"};
    for (int i = 0; i < 3; ++i) {
        icon(u, 4, 25, tree_top + 78 + i * 19, 11, MUTED);
        text(u, favorites[i], 44, tree_top + 73 + i * 19, 141, 20);
    }
    float ay = tree_top + 135;
    hit(u, 0, ay, split, 21, ED_FOLDER, -1);
    if (e->folder < 0)
        rect(u, 0, ay, split, 21, BLUE, 0);
    triangle(u, 7, ay + 8, 7, 1, MUTED);
    icon(u, 3, 22, ay + 4, 13, TEXT);
    text(u, "Assets", 42, ay, 145, 21);
    for (int i = 0; i < 5; ++i) {
        float fy = ay + 21 + i * 19;
        int hover = hit(u, 0, fy, split, 19, ED_FOLDER, i);
        if (e->folder == i)
            rect(u, 0, fy, split, 19, BLUE, 0);
        else if (hover)
            rect(u, 0, fy, split, 19, 0x414141ff, 0);
        icon(u, 3, 37, fy + 3, 12, MUTED);
        text(u, folders[i], 56, fy, 129, 19);
    }
    clip_end(u);
    clip_begin(u, (EditorRect){split, top + 48, right - split, height - 69});
    text(u, "Assets", split + 10, top + 49, 53, 23);
    if (e->folder >= 0) {
        text(u, ">", split + 65, top + 49, 20, 23);
        text(u, folders[e->folder], split + 84, top + 49, 170, 23);
    }
    rect(u, split, top + 72, right - split, 1, 0x292929ff, 0);
    clip_end(u);
    clip_begin(u, (EditorRect){split, top + 74, right - split, height - 96});
    int count = e->folder < 0 ? 6 : e->folder == 2 ? 1 : e->folder == 0 ? 3 : 1;
    int cols = (int)((right - split - 24) / 96);
    if (cols < 1)
        cols = 1;
    e->asset_max = fmaxf(0, ((count + cols - 1) / cols) * 96 - (height - 112));
    for (int i = 0; i < count; ++i) {
        float xx = split + 22 + (i % cols) * 96, yy = top + 92 + (i / cols) * 96 - e->asset_scroll;
        int id = e->folder < 0 ? i : 5 + i;
        int hover = hit(u, xx - 7, yy - 5, 88, 87, ED_ASSET, id);
        if (e->asset == id)
            rect(u, xx - 7, yy - 5, 88, 87, 0x424242ff, 3);
        else if (hover)
            rect(u, xx - 7, yy - 5, 88, 87, 0x3b3b3bff, 3);
        if (e->folder < 0 && i < 5)
            icon(u, 3, xx + 12, yy + 2, 45, 0xb4b4b4ff);
        else if (e->folder == 0) {
            circle(u, xx + 12, yy + 3, 46, i == 0 ? 0x929bafff : i == 1 ? 0xaa8170ff : 0x749981ff);
            circle(u, xx + 20, yy + 8, 12, 0xffffff20);
        } else {
            icon(u, 0, xx + 16, yy + 4, 40, 0xc6d5e8ff);
        }
        const char *name = asset_name(e, id);
        if (e->asset == id)
            rect(u, xx - 7, yy + 60, 88, 18, BLUE, 1);
        centered(u, name, xx - 7, yy + 59, 88, 20, TEXT);
    }
    clip_end(u);
    rect(u, split, top + height - 22, right - split, 22, BAR, 0);
    text(u, e->folder < 0 ? "6 items" : "Assets", split + 9, top + height - 21, 150, 20);
    rect(u, right - 113, top + height - 11, 82, 2, 0x222222ff, 0);
    circle(u, right - 56, top + height - 15, 9, 0x8a8a8aff);
}

static void menus(Ui *u) {
    EditorState *e = u->e;
    if (e->menu < 0)
        return;
    static const char *file[] = {"New Scene",  "Open Scene...",     "Save",
                                 "Save As...", "Build Profiles...", "Exit"};
    static const char *edit[] = {"Undo", "Redo", "Cut", "Copy", "Paste", "Preferences..."};
    static const char *asset[] = {"Create",   "Import New Asset...", "Import Package", "Refresh",
                                  "Reimport", "Properties..."};
    static const char *object[] = {"Create Empty", "3D Object", "2D Object",
                                   "Light",        "Audio",     "Camera"};
    static const char *component_items[] = {"Transform",    "Mesh Filter",  "Mesh Renderer",
                                            "Box Collider", "Audio Source", "Script"};
    static const char *window[] = {"Layouts",   "General",    "Rendering",
                                   "Animation", "UI Toolkit", "Package Manager"};
    static const char *help[] = {"Unity Manual", "Scripting Reference", "Report a Bug...",
                                 "About Unity",  "Keyboard Shortcuts",  "Close"};
    const char **items = e->menu == 0                   ? file
                         : e->menu == 1                 ? edit
                         : e->menu == 2                 ? asset
                         : e->menu == 3                 ? object
                         : e->menu == 4 || e->menu == 7 ? component_items
                         : e->menu == 5                 ? window
                                                        : help;
    float mx[] = {0, 38, 74, 126, 212, 298, 355, 0};
    float x = e->menu == 7 ? e->width - e->right + 44 : mx[e->menu],
          y = e->menu == 7 ? e->height * .5f : 49;
    hit(u, x, y, 222, 168, ED_NONE, 0);
    rect(u, x + 4, y + 5, 222, 168, 0x00000060, 2);
    rect(u, x, y, 222, 168, 0x333333ff, 1);
    outline(u, x, y, 222, 168, 0x191919ff);
    for (int i = 0; i < 6; ++i) {
        float yy = y + 5 + i * 26;
        int enabled = (e->menu == 0 && (i == 0 || i == 5)) || (e->menu == 6 && i == 5);
        int hover = hit(u, x + 1, yy, 220, 26, enabled ? ED_MENU_ITEM : ED_NONE, i);
        if (hover && enabled)
            rect(u, x + 1, yy, 220, 26, BLUE, 0);
        label(u, items[i], x + 19, yy, 177, 26, enabled ? TEXT : 0x777777ff, 12);
        if (e->menu == 0 && i == 2)
            label(u, "Ctrl+S", x + 164, yy, 54, 26, MUTED, 11);
    }
}

uint32_t editor_build(EditorState *e, AsmUiNode *nodes, uint32_t capacity, float px_range) {
    Ui u = {.e = e, .nodes = nodes, .cap = capacity, .range = px_range, .parent = -1};
    e->hit_count = e->quad_count = e->text_count = 0;
    float w = e->width, h = e->height, left = e->left, right = w - e->right,
          bt = h - e->bottom - 22;
    rect(&u, 0, 0, w, h, BG, 0);
    rect(&u, 0, 0, w, 28, 0x202020ff, 0);
    icon(&u, 0, 10, 7, 15, 0xd0d0d0ff);
    text(&u, "SampleScene - unity_ui - Unity Editor", 34, 2, w - 160, 24);
    text(&u, "_", w - 112, 1, 28, 24);
    outline(&u, w - 67, 10, 10, 9, MUTED);
    text(&u, "x", w - 28, 1, 25, 24);
    rect(&u, 0, 28, w, 22, BAR, 0);
    const char *ms[] = {"File", "Edit", "Assets", "GameObject", "Component", "Window", "Help"};
    float xs[] = {7, 43, 80, 132, 218, 304, 361}, ws[] = {33, 34, 48, 82, 83, 56, 39};
    for (int i = 0; i < 7; ++i) {
        int hover = hit(&u, xs[i] - 5, 28, ws[i], 22, ED_MENU, i);
        if (hover || e->menu == i)
            rect(&u, xs[i] - 5, 28, ws[i], 22, 0x494949ff, 0);
        text(&u, ms[i], xs[i], 28, ws[i], 22);
    }
    rect(&u, 0, 50, w, 31, 0x3b3b3bff, 0);
    rect(&u, 0, 80, w, 2, EDGE, 0);
    button(&u, "Account", 8, 55, 78, 21, ED_NONE, 0, 0);
    button(&u, "Cloud", 93, 55, 62, 21, ED_NONE, 0, 0);
    icon(&u, 6, 169, 58, 15, MUTED);
    text(&u, "<", 197, 54, 22, 22);
    float mid = w * .5f - 48;
    button(&u, e->locale == EDITOR_ZH_CN ? "English" : "简体中文", w - 333, 55, 84, 21, ED_LOCALE,
           0, 0);
    for (int i = 0; i < 3; ++i) {
        button(&u, "", mid + i * 33, 54, 32, 24,
               i == 0   ? ED_PLAY
               : i == 1 ? ED_PAUSE
                        : ED_STEP,
               0,
               i == 0   ? e->playing
               : i == 1 ? e->paused
                        : 0);
    }
    if (e->playing)
        rect(&u, mid + 12, 61, 9, 10, 0xbcd8f0ff, 0);
    else
        triangle(&u, mid + 12, 61, 11, 0, TEXT);
    rect(&u, mid + 44, 61, 3, 10, TEXT, 0);
    rect(&u, mid + 50, 61, 3, 10, TEXT, 0);
    triangle(&u, mid + 77, 61, 10, 0, TEXT);
    rect(&u, mid + 86, 61, 2, 10, TEXT, 0);
    button(&u, "", w - 240, 55, 30, 21, ED_SEARCH, 0, 0);
    icon(&u, 4, w - 232, 59, 12, MUTED);
    button(&u, "Layers", w - 202, 55, 86, 21, ED_NONE, 0, 0);
    button(&u, "Default", w - 107, 55, 99, 21, ED_NONE, 0, 0);
    triangle(&u, w - 21, 64, 6, 1, MUTED);
    hierarchy(&u, bt);
    inspector(&u);
    rect(&u, left, 82, right - left, 24, BAR, 0);
    for (int i = 0; i < 2; ++i) {
        int hov = hit(&u, left + i * 85, 82, 85, 24, ED_GAME, i);
        rect(&u, left + i * 85, 82, 85, 23, e->game == i ? PANEL : hov ? 0x494949ff : BAR, 0);
        icon(&u, i ? 1 : 0, left + 9 + i * 85, 88, 12, MUTED);
        text(&u, i ? "Game" : "Scene", left + 29 + i * 85, 83, 56, 22);
    }
    rect(&u, left, 106, right - left, 25, 0x3b3b3bff, 0);
    if (e->game) {
        text(&u, "Display 1", left + 10, 107, 96, 22);
        text(&u, "Free Aspect", left + 117, 107, fminf(124, right - left - 215), 22);
        if (right - left > 460)
            text(&u, "Scale  1x", left + 251, 107, 98, 22);
    } else {
        text(&u, "Shaded", left + 10, 107, 80, 22);
        triangle(&u, left + 80, 116, 6, 1, MUTED);
        button(&u, "2D", left + 98, 109, 28, 18, ED_2D, 0, e->two_d);
        icon(&u, 2, left + 139, 111, 14, TEXT);
        icon(&u, 9, left + 170, 111, 14, TEXT);
        if (right - left > 440)
            text(&u, "Effects", left + 199, 107, 55, 22);
    }
    text(&u, "Gizmos", right - 86, 107, 60, 22);
    triangle(&u, right - 26, 116, 6, 1, MUTED);
    scene(&u, (EditorRect){left + 1, 132, right - left - 2, bt - 133});
    project(&u, bt, right);
    /* Splitter hit boxes come after pane controls, before popup controls. */
    hit(&u, left - 3, 82, 6, bt - 82, ED_SPLIT_LEFT, 0);
    hit(&u, right - 3, 82, 6, h - 104, ED_SPLIT_RIGHT, 0);
    hit(&u, 0, bt - 3, right, 6, ED_SPLIT_BOTTOM, 0);
    rect(&u, 0, h - 22, w, 22, 0x252525ff, 0);
    rect(&u, 0, h - 22, w, 1, 0x191919ff, 0);
    circle(&u, 8, h - 15, 8, 0x9aa6b5ff);
    text(&u, status_text(e), 25, h - 21, w - 240, 20);
    text(&u, "Auto Generate Lighting Off", w - 207, h - 21, 182, 20);
    icon(&u, 2, w - 21, h - 17, 12, MUTED);
    menus(&u);
    e->node_count = u.n;
    return u.n;
}

void editor_patch_quads(const EditorState *e, AsmUiContext *ctx) {
    uint32_t q = 0;
    for (uint32_t i = 0; i < ctx->draw_count && q < e->quad_count; ++i) {
        if (ctx->draws[i].source_node != e->quads[q].node + 1)
            continue;
        memcpy(&ctx->draws[i].x0, e->quads[q].xy, 8 * sizeof(float));
        ++q;
    }
}
static void finish_edit(EditorState *e) {
    if (e->focus >= 0 && e->focus < 9 && e->selected == 3) {
        char *end = NULL;
        float v = strtof(e->edit, &end);
        if (end != e->edit && !*end && isfinite(v)) {
            float *dst = e->focus < 3 ? e->position : e->focus < 6 ? e->rotation : e->scale;
            dst[e->focus % 3] = clampf(v, e->focus >= 6 ? .05f : -100, 100);
        }
    }
    e->focus = -1;
    e->composition[0] = 0;
}
int editor_pointer(EditorState *e, float x, float y, int button, int down) {
    e->mouse_x = x;
    e->mouse_y = y;
    if (!down) {
        e->drag = 0;
        return 0;
    }
    int i = asm_ui_editor_hit(e->hits, e->hit_count, x, y);
    if (i < 0) {
        finish_edit(e);
        e->menu = -1;
        return 0;
    }
    EditorHit h = e->hits[i];
    finish_edit(e);
    e->drag_x = x;
    e->drag_y = y;
    if (e->menu >= 0 && h.action != ED_MENU && h.action != ED_MENU_ITEM && h.action != ED_NONE) {
        e->menu = -1;
        return 0;
    }
    switch (h.action) {
    case ED_SELECT:
        e->selected = h.value;
        e->asset = -1;
        e->inspector_scroll = 0;
        break;
    case ED_TOOL:
        e->tool = h.value;
        break;
    case ED_PLAY:
        e->playing = !e->playing;
        e->paused = 0;
        snprintf(e->status, sizeof(e->status),
                 e->playing ? "Entered Play Mode" : "Exited Play Mode");
        break;
    case ED_PAUSE:
        if (e->playing)
            e->paused = !e->paused;
        break;
    case ED_STEP:
        if (e->playing) {
            e->paused = 1;
            ++e->frame;
            snprintf(e->status, sizeof(e->status), "Frame %d", e->frame);
        }
        break;
    case ED_GAME:
        e->game = h.value;
        break;
    case ED_CONSOLE:
        e->console = h.value;
        break;
    case ED_FOLDER:
        e->folder = h.value;
        e->asset = -1;
        e->asset_scroll = 0;
        break;
    case ED_ASSET:
        e->asset = h.value;
        break;
    case ED_FOLD:
        e->folds ^= 1u << h.value;
        break;
    case ED_ACTIVE:
        if (e->selected >= 0)
            e->enabled ^= 1 << e->selected;
        break;
    case ED_SCENE_OPEN:
        e->scene_open = !e->scene_open;
        break;
    case ED_ENV_OPEN:
        e->env_open = !e->env_open;
        break;
    case ED_SEARCH:
        e->focus = 100;
        break;
    case ED_FIELD:
        if (h.value < 9) {
            e->focus = h.value;
            snprintf(e->edit, sizeof(e->edit), "%s", e->values[h.value]);
        }
        break;
    case ED_MENU:
        e->menu = e->menu == h.value ? -1 : h.value;
        return 0;
    case ED_MENU_ITEM:
        if (e->menu == 0 && h.value == 5)
            return 1;
        if (e->menu == 0 && h.value == 0) {
            float w = e->width, ht = e->height;
            EditorLocale locale = e->locale;
            editor_init(e, w, ht);
            e->locale = locale;
        }
        e->menu = -1;
        break;
    case ED_2D:
        e->two_d = !e->two_d;
        break;
    case ED_SPLIT_LEFT:
    case ED_SPLIT_RIGHT:
    case ED_SPLIT_BOTTOM:
        e->drag = h.action;
        break;
    case ED_SCENE:
        if ((button == 3 || button == 2) && !e->two_d)
            e->drag = ED_SCENE;
        else if (x >= e->object_rect.x && x <= e->object_rect.x + e->object_rect.w &&
                 y >= e->object_rect.y && y <= e->object_rect.y + e->object_rect.h) {
            e->selected = 3;
            e->asset = -1;
        }
        break;
    case ED_CLEAR:
        e->status[0] = 0;
        break;
    case ED_LOCALE:
        e->locale = e->locale == EDITOR_ZH_CN ? EDITOR_EN_US : EDITOR_ZH_CN;
        break;
    default:
        break;
    }
    if (h.action != ED_MENU_ITEM)
        e->menu = -1;
    return 0;
}
void editor_motion(EditorState *e, float x, float y) {
    if (e->drag == ED_SPLIT_LEFT)
        e->left = clampf(x, 160, fminf(e->width * .32f, e->width - e->right - 320));
    if (e->drag == ED_SPLIT_RIGHT)
        e->right = clampf(e->width - x, 280, fminf(e->width * .4f, e->width - e->left - 320));
    if (e->drag == ED_SPLIT_BOTTOM)
        e->bottom = clampf(e->height - y - 22, 150, e->height * .55f);
    if (e->drag == ED_SCENE) {
        e->yaw += (x - e->drag_x) * .008f;
        e->pitch = clampf(e->pitch + (y - e->drag_y) * .006f, .08f, 1.53f);
    }
    e->drag_x = x;
    e->drag_y = y;
    e->mouse_x = x;
    e->mouse_y = y;
}
void editor_wheel(EditorState *e, float amount) {
    EditorRect r = e->scene_rect;
    if (e->mouse_x >= r.x && e->mouse_x < r.x + r.w && e->mouse_y >= r.y && e->mouse_y < r.y + r.h)
        e->zoom = clampf(e->zoom - amount * .45f, 2.5f, 18);
    else if (e->mouse_x >= e->width - e->right && e->mouse_y > 107 && e->mouse_y < e->height - 22)
        e->inspector_scroll = clampf(e->inspector_scroll - amount * 30, 0, e->inspector_max);
    else if (e->mouse_y > e->height - e->bottom + 26 && e->mouse_y < e->height - 22) {
        if (e->mouse_x < 190)
            e->project_scroll =
                clampf(e->project_scroll - amount * 25, 0, fmaxf(0, 252 - e->bottom));
        else
            e->asset_scroll = clampf(e->asset_scroll - amount * 25, 0, e->asset_max);
    }
}
static void append_utf8(char *dst, size_t capacity, const char *s) {
    size_t n = strlen(dst);
    for (size_t i = 0; s[i] && n + 1 < capacity;) {
        unsigned char c = (unsigned char)s[i];
        size_t bytes = c < 128                  ? 1
                       : c >= 0xc2 && c < 0xe0  ? 2
                       : c < 0xf0 && c >= 0xe0  ? 3
                       : c >= 0xf0 && c <= 0xf4 ? 4
                                                : 0;
        if (!bytes) {
            ++i;
            continue;
        }
        if (n + bytes >= capacity)
            break;
        int valid = 1;
        for (size_t k = 1; k < bytes; ++k)
            if (!s[i + k] || ((unsigned char)s[i + k] & 0xc0) != 0x80) {
                valid = 0;
                break;
            }
        unsigned char second = (unsigned char)s[i + 1];
        if ((c == 0xe0 && second < 0xa0) || (c == 0xed && second >= 0xa0) ||
            (c == 0xf0 && second < 0x90) || (c == 0xf4 && second >= 0x90))
            valid = 0;
        if (!valid) {
            ++i;
            continue;
        }
        if (c >= 32) {
            memcpy(dst + n, s + i, bytes);
            n += bytes;
        }
        i += bytes;
    }
    dst[n] = 0;
}
void editor_text(EditorState *e, const char *s) {
    char *dst = e->focus == 100 ? e->search : e->focus >= 0 ? e->edit : NULL;
    if (dst) {
        append_utf8(dst, sizeof(e->search), s);
        e->composition[0] = 0;
    }
}
void editor_composition(EditorState *e, const char *s) {
    e->composition[0] = 0;
    if (e->focus >= 0)
        append_utf8(e->composition, sizeof(e->composition), s);
}
void editor_key(EditorState *e, int key, int ctrl) {
    if (key == 27) {
        e->menu = -1;
        e->focus = -1;
        e->drag = 0;
        e->composition[0] = 0;
        return;
    }
    if (e->focus >= 0) {
        /* The IME consumes Enter/Backspace while a preedit string is active. */
        if (e->composition[0])
            return;
        char *dst = e->focus == 100 ? e->search : e->edit;
        if (key == 8) {
            size_t n = strlen(dst);
            if (n) {
                --n;
                while (n && ((unsigned char)dst[n] & 0xc0) == 0x80)
                    --n;
                dst[n] = 0;
            }
        } else if (key == 13)
            finish_edit(e);
        else if (ctrl && (key == 'a' || key == 'A'))
            dst[0] = 0;
        return;
    }
    if (ctrl && (key == 'p' || key == 'P')) {
        e->playing = !e->playing;
        e->paused = 0;
    }
    if (key == 'f' || key == 'F') {
        e->zoom = 7.2f;
        e->yaw = .70f;
        e->pitch = .36f;
    }
    const char *keys = "qwerty";
    const char *k = key > 0 && key < 128 ? strchr(keys, key) : NULL;
    if (k)
        e->tool = (int)(k - keys);
}
