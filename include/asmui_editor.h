#ifndef ASMUI_EDITOR_H
#define ASMUI_EDITOR_H
#include "asmui.h"
#include "asmui_editor_i18n.h"

#define EDITOR_MAX_NODES 8192u
#define EDITOR_MAX_DRAWS 16384u
#define EDITOR_MAX_HITS 256u
#define EDITOR_MAX_QUADS 128u

typedef struct EditorRect {
    float x, y, w, h;
} EditorRect;
typedef struct EditorHit {
    EditorRect rect;
    int action, value;
} EditorHit;
typedef struct EditorQuad {
    uint32_t node;
    float xy[8];
} EditorQuad;
typedef struct EditorState {
    EditorLocale locale;
    int selected, asset, folder, tool, game, two_d, console, playing, paused, frame;
    int menu, hover, focus, drag, enabled, scene_open, env_open;
    unsigned folds;
    float width, height, left, right, bottom;
    float mouse_x, mouse_y, drag_x, drag_y, yaw, pitch, zoom;
    float position[3], rotation[3], scale[3];
    float inspector_scroll, inspector_max, project_scroll, asset_scroll, asset_max;
    char search[64], edit[64], status[160];
    char composition[128], composed[256];
    char values[24][48];
    EditorHit hits[EDITOR_MAX_HITS];
    uint32_t hit_count;
    EditorQuad quads[EDITOR_MAX_QUADS];
    uint32_t quad_count;
    uint32_t text_count, node_count;
    EditorRect scene_rect, object_rect, input_rect;
    float caret_offset;
    float (*measure_text)(const char *text, float pixels, void *user);
    void *measure_user;
    uint32_t (*application_build)(void *user, AsmUiNode *nodes, uint32_t start,
                                  uint32_t capacity, int32_t parent, EditorRect viewport);
    void *application_user;
} EditorState;

enum {
    ED_NONE,
    ED_SELECT,
    ED_TOOL,
    ED_PLAY,
    ED_PAUSE,
    ED_STEP,
    ED_GAME,
    ED_CONSOLE,
    ED_FOLDER,
    ED_ASSET,
    ED_FOLD,
    ED_ACTIVE,
    ED_SCENE_OPEN,
    ED_ENV_OPEN,
    ED_SEARCH,
    ED_FIELD,
    ED_MENU,
    ED_MENU_ITEM,
    ED_2D,
    ED_SPLIT_LEFT,
    ED_SPLIT_RIGHT,
    ED_SPLIT_BOTTOM,
    ED_SCENE,
    ED_CLEAR,
    ED_LOCALE
};

void editor_init(EditorState *e, float width, float height);
uint32_t editor_build(EditorState *e, AsmUiNode *nodes, uint32_t capacity, float px_range);
void editor_patch_quads(const EditorState *e, AsmUiContext *ctx);
int editor_pointer(EditorState *e, float x, float y, int button, int down);
void editor_motion(EditorState *e, float x, float y);
void editor_wheel(EditorState *e, float amount);
void editor_text(EditorState *e, const char *text);
void editor_composition(EditorState *e, const char *text);
void editor_key(EditorState *e, int key, int ctrl);
void editor_resize(EditorState *e, float width, float height);
const char *editor_font(void);
const char *const *editor_font_samples(size_t *count);

/* Row-major homogeneous transform, +Y-up world -> +Y-down viewport.
   Input xyz triples, output framebuffer xy pairs. ASM preserves SysV ABI. */
void asm_ui_project_points(const float *xyz, uint32_t count, const float *matrix,
                           const EditorRect *viewport, float *xy);
/* Last painted hit wins, matching popup and dock overlay painter order. */
int asm_ui_editor_hit(const EditorHit *hits, uint32_t count, float x, float y);
#endif
