#ifndef ASMUI_SONG_SELECT_SCENE_TEXT_H
#define ASMUI_SONG_SELECT_SCENE_TEXT_H

#include "demo/song_select_scene.h"
#include <stddef.h>

/* v0.12 scene variant: identical visual structure, but labels are retained
 * ASMUI_NODE_TEXT objects instead of hundreds of 5x7 pixel quads. */
#define ASMUI_SONG_CAROUSEL_COUNT 7u

typedef struct AsmUiSongMeta {
    const char *title;
    const char *artist;
    const char *subtitle;
    const char *difficulty;
    const char *level;
    const char *score;
    const char *combo;
    float gauge;
    uint32_t accent_rgba;
    uint32_t badge_rgba;
} AsmUiSongMeta;

static inline const AsmUiSongMeta *asmui_song_catalog(uint32_t *count) {
    static const AsmUiSongMeta songs[ASMUI_SONG_CAROUSEL_COUNT] = {
        { "AIR",     "KEY",    "SUMMER SKY",     "HARD",   "9+",  "986431", "742",  0.62f, 0x5DE0FFFFu, 0x2563EBFFu },
        { "夏影",    "KEY",    "SUMMER LIGHT",   "EXPERT", "11",  "992875", "906",  0.71f, 0xA775FFFFu, 0x7C3AEDFFu },
        { "NEON",    "LUMINA", "NEON RHAPSODY",  "MASTER", "14+", "998742", "1284", 0.84f, 0xFF5BC8FFu, 0xB62C8FFFu },
        { "鳥の詩",  "LIA",    "AIR ORIGINAL",   "MASTER", "12+", "997318", "1186", 0.79f, 0x63E6BEFFu, 0x0F766EFFu },
        { "ORBIT",   "ASTER",  "GRAVITY RING",   "EXPERT", "13",  "995104", "1042", 0.76f, 0xF59E0BFFu, 0xB45309FFu },
        { "LUMINA",  "SYNC",   "BLUE SIGNAL",    "MASTER", "15",  "999102", "1430", 0.91f, 0x7C6DFFFFu, 0x6D28D9FFu },
        { "ASTER",   "ORBIT",  "STAR TRACE",     "HARD",   "10+", "989763", "831",  0.68f, 0x60A5FAFFu, 0x1D4ED8FFu },
    };
    if (count) *count = ASMUI_SONG_CAROUSEL_COUNT;
    return songs;
}

typedef struct AsmUiSongTextSceneInfo {
    AsmUiSongSceneInfo base;
    uint32_t text_node_count;
    uint32_t carousel_count;
    uint32_t carousel_root[ASMUI_SONG_CAROUSEL_COUNT];
    uint32_t carousel_stripe[ASMUI_SONG_CAROUSEL_COUNT];
    uint32_t carousel_jacket[ASMUI_SONG_CAROUSEL_COUNT];
    uint32_t carousel_title[ASMUI_SONG_CAROUSEL_COUNT];
    uint32_t carousel_viewport_node;

    uint32_t title_node;
    uint32_t artist_node;
    uint32_t subtitle_node;
    uint32_t difficulty_node;
    uint32_t difficulty_panel_node;
    uint32_t level_node;
    uint32_t score_value_node;
    uint32_t combo_value_node;
} AsmUiSongTextSceneInfo;

static inline const char *const *asmui_song_text_samples(size_t *count) {
    static const char *const text[] = {
        "SYNC", "128",
        "AIR", "夏影", "NEON", "鳥の詩", "ORBIT", "LUMINA", "ASTER",
        "KEY", "LIA",
        "SUMMER SKY", "SUMMER LIGHT", "NEON RHAPSODY", "AIR ORIGINAL",
        "GRAVITY RING", "BLUE SIGNAL", "STAR TRACE",
        "HARD", "EXPERT", "MASTER",
        "9+", "10+", "11", "12+", "13", "14+", "15",
        "SCORE", "COMBO", "PLAY",
        "986431", "992875", "998742", "997318", "995104", "999102", "989763",
        "742", "906", "1284", "1186", "1042", "1430", "831"
    };
    if (count) *count = sizeof(text) / sizeof(text[0]);
    return text;
}

static inline int asmui_song_push_text_node(AsmUiNode *nodes, uint32_t cap, uint32_t *count,
                                             const char *text,
                                             float x, float y, float w, float h,
                                             float font_size, uint32_t rgba,
                                             uint32_t align, float px_range,
                                             uint32_t *text_counter) {
    if (*count >= cap) return 0;
    AsmUiNode n = asmui_node(ASMUI_NODE_TEXT, -1, rgba);
    n.resource_id = 1;
    n.payload = text;
    n.anchored_x = x;
    n.anchored_y = y;
    n.size_delta_x = w;
    n.size_delta_y = h;
    n.font_size = font_size;
    n.text_align = align;
    asmui_text_msdf(&n, px_range, 0.0f, 0.0f);
    nodes[(*count)++] = n;
    if (text_counter) ++*text_counter;
    return 1;
}

static inline AsmUiSongTextSceneInfo asmui_song_select_build_text(AsmUiNode *nodes,
                                                                   uint32_t cap,
                                                                   float px_range) {
    uint32_t n = 0, panels = 0, texts = 0;
    uint32_t gauge_node = UINT32_MAX, play_panel_node = UINT32_MAX;
    uint32_t jacket_accent_node = UINT32_MAX, jacket_image_node = UINT32_MAX, nav_selected_node = UINT32_MAX, nav_selected_icon_node = UINT32_MAX;
    uint32_t title_node = UINT32_MAX, artist_node = UINT32_MAX, subtitle_node = UINT32_MAX;
    uint32_t difficulty_node = UINT32_MAX, difficulty_panel_node = UINT32_MAX, level_node = UINT32_MAX;
    uint32_t score_value_node = UINT32_MAX, combo_value_node = UINT32_MAX;
    uint32_t carousel_root[ASMUI_SONG_CAROUSEL_COUNT];
    uint32_t carousel_stripe[ASMUI_SONG_CAROUSEL_COUNT];
    uint32_t carousel_jacket[ASMUI_SONG_CAROUSEL_COUNT];
    uint32_t carousel_title[ASMUI_SONG_CAROUSEL_COUNT];
    uint32_t carousel_viewport_node = UINT32_MAX;
    for (uint32_t i = 0; i < ASMUI_SONG_CAROUSEL_COUNT; ++i) {
        carousel_root[i] = UINT32_MAX;
        carousel_stripe[i] = UINT32_MAX;
        carousel_jacket[i] = UINT32_MAX;
        carousel_title[i] = UINT32_MAX;
    }
#define PANEL(X,Y,W,H,C,R) do { if(!asmui_song_push_rect(nodes,cap,&n,(X),(Y),(W),(H),(C),ASMUI_MATERIAL_ROUNDED,(R),0.0f)) goto done; ++panels; } while(0)
#define RECT(X,Y,W,H,C) do { if(!asmui_song_push_rect(nodes,cap,&n,(X),(Y),(W),(H),(C),ASMUI_MATERIAL_DEFAULT,0.0f,0.0f)) goto done; } while(0)
#define TEXTN(S,X,Y,W,H,FS,C,A) do { if(!asmui_song_push_text_node(nodes,cap,&n,(S),(X),(Y),(W),(H),(FS),(C),(A),(px_range),&texts)) goto done; } while(0)

    RECT(0, 0, 1280, 720, 0x080D1FFFu);
    RECT(0, 318, 1280, 2, 0x25325D80u);
    RECT(0,-318, 1280, 2, 0x25325D80u);

    PANEL(-500, 300, 180, 58, 0x131C38E8u, 16.0f);
    TEXTN("SYNC", -500, 300, 160, 50, 30.0f, 0x66E6FFFFu, ASMUI_TEXT_CENTER);
    PANEL(510, 300, 170, 58, 0x131C38E8u, 16.0f);
    TEXTN("128", 510, 300, 150, 50, 30.0f, 0xFFFFFFFFu, ASMUI_TEXT_CENTER);

    PANEL(-548, 0, 116, 540, 0x101832F0u, 28.0f);
    for (int i = 0; i < 5; ++i) {
        float yy = 170.0f - (float)i * 86.0f;
        if (i == 2) nav_selected_node = n;
        PANEL(-548, yy, 72, 58, i == 2 ? 0x2E8FFFFFu : 0x182548FFu, 18.0f);
        if (i == 2) nav_selected_icon_node = n;
        RECT(-548, yy, 18, 18, i == 2 ? 0xD8FFFFFFu : 0x5F79A8FFu);
    }

    PANEL(-180, 30, 430, 500, 0x121B36FFu, 30.0f);
    PANEL(-180, 70, 362, 362, 0x202A55FFu, 24.0f);
    for (int i = 0; i < 6; ++i) {
        float s = 300.0f - i * 44.0f;
        if (i == 0) jacket_accent_node = n;
        PANEL(-180, 70, s, s, (i & 1) ? 0x172750D0u : 0x7B4CFFD0u, 18.0f);
    }
    if (n < cap) {
        jacket_image_node = n;
        AsmUiNode jacket = asmui_node(ASMUI_NODE_IMAGE, -1, 0xFFFFFFFFu);
        jacket.flags = 0; /* enabled by live demo when ASMUI_JACKET loads */
        jacket.resource_id = 2;
        jacket.anchored_x = -180.0f;
        jacket.anchored_y = 70.0f;
        jacket.size_delta_x = 320.0f;
        jacket.size_delta_y = 320.0f;
        jacket.image_mode = ASMUI_IMAGE_SIMPLE;
        nodes[n++] = jacket;
    }
    RECT(-180, 70, 330, 26, 0x5DE0FFFFu);
    RECT(-180, 70, 26, 330, 0xFF5BC8FFu);
    PANEL(-180, 70, 76, 76, 0xEEF9FFFFu, 38.0f);
    PANEL(-180, 70, 44, 44, 0x4E75FFFFu, 22.0f);

    title_node = n; TEXTN("NEON", -180, -176, 390, 54, 38.0f, 0xF4F8FFFFu, ASMUI_TEXT_LEFT);
    artist_node = n; TEXTN("LUMINA", -180, -222, 390, 38, 24.0f, 0x7EA2D8FFu, ASMUI_TEXT_LEFT);
    subtitle_node = n; TEXTN("NEON RHAPSODY", -180, -252, 390, 34, 21.0f, 0x88D9FFFFu, ASMUI_TEXT_LEFT);

    PANEL(330, 62, 470, 430, 0x101832F0u, 30.0f);
    difficulty_node = n; TEXTN("MASTER", 220, 224, 210, 52, 31.0f, 0xFF66D8FFu, ASMUI_TEXT_LEFT);
    difficulty_panel_node = n;
    PANEL(500, 218, 92, 62, 0xB62C8FFFu, 20.0f);
    level_node = n; TEXTN("14+", 500, 218, 86, 54, 33.0f, 0xFFFFFFFFu, ASMUI_TEXT_CENTER);

    TEXTN("SCORE", 220, 128, 210, 38, 22.0f, 0x6680AAFFu, ASMUI_TEXT_LEFT);
    score_value_node = n; TEXTN("998742", 220, 84, 220, 48, 31.0f, 0xF6FAFFFFu, ASMUI_TEXT_LEFT);
    TEXTN("COMBO", 220, 10, 210, 38, 22.0f, 0x6680AAFFu, ASMUI_TEXT_LEFT);
    combo_value_node = n; TEXTN("1284", 220, -34, 220, 48, 31.0f, 0xF6FAFFFFu, ASMUI_TEXT_LEFT);

    PANEL(330, -118, 380, 22, 0x1A2448FFu, 11.0f);
    if (n < cap) {
        gauge_node = n;
        AsmUiNode gauge = asmui_node(ASMUI_NODE_IMAGE, -1, 0x65E7FFFFu);
        gauge.resource_id = 0;
        gauge.anchored_x = 281.0f;
        gauge.anchored_y = -118.0f;
        gauge.size_delta_x = 282.0f;
        gauge.size_delta_y = 12.0f;
        gauge.image_mode = ASMUI_IMAGE_FILLED_HORIZONTAL;
        gauge.fill_amount = 0.84f;
        nodes[n++] = gauge;
    }

    play_panel_node = n;
    PANEL(330, -225, 300, 92, 0x33BDEFFFu, 28.0f);
    TEXTN("PLAY", 330, -225, 260, 72, 42.0f, 0xFFFFFFFFu, ASMUI_TEXT_CENTER);

    /* v0.18: the carousel now lives under a real clipped viewport. The seven
       retained cards are physical slots; a virtual list may recycle them for
       an arbitrarily large logical song catalog. */
    if (n >= cap) goto done;
    carousel_viewport_node = n;
    {
        AsmUiNode viewport = asmui_node(ASMUI_NODE_CONTAINER, -1, 0xffffffffu);
        viewport.anchored_x = 90.0f;
        viewport.anchored_y = -318.0f;
        viewport.size_delta_x = 1080.0f;
        viewport.size_delta_y = 104.0f;
        viewport.flags |= ASMUI_CLIP;
        nodes[n++] = viewport;
    }
    {
        static const char *const titles[ASMUI_SONG_CAROUSEL_COUNT] = {
            "AIR", "夏影", "NEON", "鳥の詩", "ORBIT", "LUMINA", "ASTER"
        };
        static const uint32_t accents[ASMUI_SONG_CAROUSEL_COUNT] = {
            0x5DE0FFFFu, 0xA775FFFFu, 0xFF5BC8FFu, 0x63E6BEFFu,
            0xF59E0BFFu, 0x7C6DFFFFu, 0x60A5FAFFu
        };
        for (uint32_t i = 0; i < ASMUI_SONG_CAROUSEL_COUNT; ++i) {
            if (n >= cap) goto done;
            carousel_root[i] = n;
            AsmUiNode card = asmui_node(ASMUI_NODE_IMAGE, (int32_t)carousel_viewport_node, 0x111A34F2u);
            card.anchored_x = ((float)i - 3.0f) * 156.0f;
            card.anchored_y = 0.0f;
            card.size_delta_x = 142.0f;
            card.size_delta_y = 66.0f;
            asmui_material_rounded(&card, 16.0f);
            nodes[n++] = card;
            ++panels;

            if (n >= cap) goto done;
            carousel_stripe[i] = n;
            AsmUiNode stripe = asmui_node(ASMUI_NODE_IMAGE, (int32_t)carousel_root[i], accents[i]);
            stripe.anchored_x = -60.0f;
            stripe.anchored_y = 0.0f;
            stripe.size_delta_x = 8.0f;
            stripe.size_delta_y = 42.0f;
            asmui_material_rounded(&stripe, 4.0f);
            nodes[n++] = stripe;

            /* v0.19: physical card slot for streamed jacket artwork. Hidden
               until an asset-cache residency entry is ready. */
            if (n >= cap) goto done;
            carousel_jacket[i] = n;
            AsmUiNode mini_jacket = asmui_node(ASMUI_NODE_IMAGE, (int32_t)carousel_root[i], 0xFFFFFFFFu);
            mini_jacket.flags = 0;
            mini_jacket.resource_id = 0;
            mini_jacket.anchored_x = -34.0f;
            mini_jacket.anchored_y = 0.0f;
            mini_jacket.size_delta_x = 42.0f;
            mini_jacket.size_delta_y = 42.0f;
            mini_jacket.image_mode = ASMUI_IMAGE_SIMPLE;
            nodes[n++] = mini_jacket;

            carousel_title[i] = n;
            if (!asmui_song_push_text_node(nodes, cap, &n, titles[i],
                                            22.0f, 0.0f, 74.0f, 48.0f,
                                            18.0f, 0xEAF4FFFFu, ASMUI_TEXT_CENTER,
                                            px_range, &texts)) goto done;
            nodes[n - 1u].parent = (int32_t)carousel_root[i];
        }
    }

done:
#undef PANEL
#undef RECT
#undef TEXTN
    AsmUiSongTextSceneInfo out;
    out.base.node_count = n;
    out.base.panel_count = panels;
    out.base.glyph_pixel_count = 0;
    out.base.gauge_node = gauge_node;
    out.base.play_panel_node = play_panel_node;
    out.base.jacket_accent_node = jacket_accent_node;
    out.base.jacket_image_node = jacket_image_node;
    out.base.nav_selected_node = nav_selected_node;
    out.base.nav_selected_icon_node = nav_selected_icon_node;
    out.text_node_count = texts;
    out.carousel_count = ASMUI_SONG_CAROUSEL_COUNT;
    out.carousel_viewport_node = carousel_viewport_node;
    for (uint32_t i = 0; i < ASMUI_SONG_CAROUSEL_COUNT; ++i) {
        out.carousel_root[i] = carousel_root[i];
        out.carousel_stripe[i] = carousel_stripe[i];
        out.carousel_jacket[i] = carousel_jacket[i];
        out.carousel_title[i] = carousel_title[i];
    }
    out.title_node = title_node;
    out.artist_node = artist_node;
    out.subtitle_node = subtitle_node;
    out.difficulty_node = difficulty_node;
    out.difficulty_panel_node = difficulty_panel_node;
    out.level_node = level_node;
    out.score_value_node = score_value_node;
    out.combo_value_node = combo_value_node;
    return out;
}

static inline int asmui_song_select_bind(AsmUiNode *nodes,
                                          const AsmUiSongTextSceneInfo *scene,
                                          uint32_t index) {
    if (!nodes || !scene || !scene->carousel_count) return 0;
    if (index >= scene->carousel_count) index = scene->carousel_count - 1u;

    uint32_t count = 0;
    const AsmUiSongMeta *songs = asmui_song_catalog(&count);
    if (!songs || index >= count) return 0;
    const AsmUiSongMeta *m = &songs[index];

#define SET_PAYLOAD(H, S) do { if ((H) >= scene->base.node_count) return 0; nodes[(H)].payload = (S); } while (0)
    SET_PAYLOAD(scene->title_node, m->title);
    SET_PAYLOAD(scene->artist_node, m->artist);
    SET_PAYLOAD(scene->subtitle_node, m->subtitle);
    SET_PAYLOAD(scene->difficulty_node, m->difficulty);
    SET_PAYLOAD(scene->level_node, m->level);
    SET_PAYLOAD(scene->score_value_node, m->score);
    SET_PAYLOAD(scene->combo_value_node, m->combo);
#undef SET_PAYLOAD

    if (scene->difficulty_panel_node >= scene->base.node_count ||
        scene->base.gauge_node >= scene->base.node_count ||
        scene->base.jacket_accent_node >= scene->base.node_count)
        return 0;

    nodes[scene->difficulty_panel_node].rgba = m->badge_rgba;
    nodes[scene->base.gauge_node].fill_amount = m->gauge;
    nodes[scene->base.jacket_accent_node].rgba = m->accent_rgba;
    return 1;
}


/* Bind one physical carousel slot to an arbitrary logical song index. The
   demo catalog repeats every seven entries, but the retained-node lifetime is
   independent from the logical list size. Applications can replace this
   mapping with their own database/model layer. */
static inline int asmui_song_carousel_bind_slot(AsmUiNode *nodes,
                                                 const AsmUiSongTextSceneInfo *scene,
                                                 uint32_t slot,
                                                 uint32_t logical_index) {
    if (!nodes || !scene || slot >= scene->carousel_count) return 0;
    uint32_t count = 0;
    const AsmUiSongMeta *songs = asmui_song_catalog(&count);
    if (!songs || !count) return 0;
    const AsmUiSongMeta *m = &songs[logical_index % count];
    uint32_t t = scene->carousel_title[slot];
    uint32_t stripe = scene->carousel_stripe[slot];
    if (t >= scene->base.node_count || stripe >= scene->base.node_count) return 0;
    nodes[t].payload = m->title;
    nodes[stripe].rgba = m->accent_rgba;
    return 1;
}

static inline int asmui_song_select_bind_logical(AsmUiNode *nodes,
                                                  const AsmUiSongTextSceneInfo *scene,
                                                  uint32_t logical_index) {
    uint32_t count = 0;
    (void)asmui_song_catalog(&count);
    if (!count) return 0;
    return asmui_song_select_bind(nodes, scene, logical_index % count);
}

#endif
