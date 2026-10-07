#ifndef ASMUI_SONG_SELECT_SCENE_H
#define ASMUI_SONG_SELECT_SCENE_H

#include "asmui.h"
#include "asmui_builder.h"
#include <stdint.h>
#include <string.h>
#include <math.h>

#define ASMUI_SONG_W 1280u
#define ASMUI_SONG_H 720u
#define ASMUI_SONG_MAX_NODES 4096u

/* Asset-free rhythm-game style scene.  Everything uses the 1x1 white texture
 * and vertex tinting, so it is useful as a deterministic integration/perf
 * workload on any Vulkan machine. */

typedef struct AsmUiSongSceneInfo {
    uint32_t node_count;
    uint32_t panel_count;
    uint32_t glyph_pixel_count;
    /* v0.10 stable handles used by realtime animation/input. */
    uint32_t gauge_node;
    uint32_t play_panel_node;
    uint32_t jacket_accent_node;
    uint32_t jacket_image_node;
    uint32_t nav_selected_node;
    uint32_t nav_selected_icon_node;
} AsmUiSongSceneInfo;

static inline uint8_t asmui_font5x7_row(char c, int row) {
    /* 5-bit rows, MSB on the left. Only the demo charset is intentionally
       included; unknown characters become a compact box. */
    static const uint8_t box[7] = {31,17,17,17,17,17,31};
    const uint8_t *g = box;
    static const uint8_t A[7]={14,17,17,31,17,17,17};
    static const uint8_t B[7]={30,17,17,30,17,17,30};
    static const uint8_t C[7]={15,16,16,16,16,16,15};
    static const uint8_t D[7]={30,17,17,17,17,17,30};
    static const uint8_t E[7]={31,16,16,30,16,16,31};
    static const uint8_t F[7]={31,16,16,30,16,16,16};
    static const uint8_t G[7]={15,16,16,19,17,17,14};
    static const uint8_t H[7]={17,17,17,31,17,17,17};
    static const uint8_t I[7]={31,4,4,4,4,4,31};
    static const uint8_t J[7]={7,2,2,2,18,18,12};
    static const uint8_t K[7]={17,18,20,24,20,18,17};
    static const uint8_t L[7]={16,16,16,16,16,16,31};
    static const uint8_t M[7]={17,27,21,21,17,17,17};
    static const uint8_t N[7]={17,25,21,19,17,17,17};
    static const uint8_t O[7]={14,17,17,17,17,17,14};
    static const uint8_t P[7]={30,17,17,30,16,16,16};
    static const uint8_t Q[7]={14,17,17,17,21,18,13};
    static const uint8_t R[7]={30,17,17,30,20,18,17};
    static const uint8_t S[7]={15,16,16,14,1,1,30};
    static const uint8_t T[7]={31,4,4,4,4,4,4};
    static const uint8_t U[7]={17,17,17,17,17,17,14};
    static const uint8_t V[7]={17,17,17,17,17,10,4};
    static const uint8_t W[7]={17,17,17,21,21,21,10};
    static const uint8_t X[7]={17,17,10,4,10,17,17};
    static const uint8_t Y[7]={17,17,10,4,4,4,4};
    static const uint8_t Z[7]={31,1,2,4,8,16,31};
    static const uint8_t D0[7]={14,17,19,21,25,17,14};
    static const uint8_t D1[7]={4,12,4,4,4,4,14};
    static const uint8_t D2[7]={14,17,1,2,4,8,31};
    static const uint8_t D3[7]={30,1,1,14,1,1,30};
    static const uint8_t D4[7]={2,6,10,18,31,2,2};
    static const uint8_t D5[7]={31,16,16,30,1,1,30};
    static const uint8_t D6[7]={14,16,16,30,17,17,14};
    static const uint8_t D7[7]={31,1,2,4,8,8,8};
    static const uint8_t D8[7]={14,17,17,14,17,17,14};
    static const uint8_t D9[7]={14,17,17,15,1,1,14};
    static const uint8_t PLUS[7]={0,4,4,31,4,4,0};
    switch (c) {
        case 'A': g=A; break; case 'B': g=B; break; case 'C': g=C; break;
        case 'D': g=D; break; case 'E': g=E; break; case 'F': g=F; break;
        case 'G': g=G; break; case 'H': g=H; break; case 'I': g=I; break;
        case 'J': g=J; break; case 'K': g=K; break; case 'L': g=L; break;
        case 'M': g=M; break; case 'N': g=N; break; case 'O': g=O; break;
        case 'P': g=P; break; case 'Q': g=Q; break; case 'R': g=R; break;
        case 'S': g=S; break; case 'T': g=T; break; case 'U': g=U; break;
        case 'V': g=V; break; case 'W': g=W; break; case 'X': g=X; break;
        case 'Y': g=Y; break; case 'Z': g=Z; break;
        case '0': g=D0; break; case '1': g=D1; break; case '2': g=D2; break;
        case '3': g=D3; break; case '4': g=D4; break; case '5': g=D5; break;
        case '6': g=D6; break; case '7': g=D7; break; case '8': g=D8; break;
        case '9': g=D9; break; case '+': g=PLUS; break;
        case ' ': return 0;
        default: break;
    }
    return g[row];
}

static inline int asmui_song_push_rect(AsmUiNode *nodes, uint32_t cap, uint32_t *count,
                                       float x, float y, float w, float h,
                                       uint32_t rgba, uint32_t material, float p0, float p1) {
    if (*count >= cap) return 0;
    AsmUiNode n = asmui_node(ASMUI_NODE_IMAGE, -1, rgba);
    n.resource_id = 0;
    n.anchored_x = x;
    n.anchored_y = y;
    n.size_delta_x = w;
    n.size_delta_y = h;
    if (material != ASMUI_MATERIAL_DEFAULT)
        asmui_set_material4(&n, material, p0, p1, 0.0f, 0.0f);
    nodes[(*count)++] = n;
    return 1;
}

static inline int asmui_song_push_text(AsmUiNode *nodes, uint32_t cap, uint32_t *count,
                                       const char *text, float x, float y, float scale,
                                       uint32_t rgba, uint32_t *pixel_counter) {
    float advance = 6.0f * scale;
    for (size_t ci = 0; text[ci]; ++ci) {
        char ch = text[ci];
        for (int row = 0; row < 7; ++row) {
            uint8_t bits = asmui_font5x7_row(ch, row);
            for (int col = 0; col < 5; ++col) {
                if (!(bits & (1u << (4 - col)))) continue;
                if (!asmui_song_push_rect(nodes, cap, count,
                        x + (float)ci * advance + (float)col * scale,
                        y + (float)(6 - row) * scale,
                        scale * 0.82f, scale * 0.82f,
                        rgba, ASMUI_MATERIAL_DEFAULT, 0.0f, 0.0f)) return 0;
                ++*pixel_counter;
            }
        }
    }
    return 1;
}

static inline AsmUiSongSceneInfo asmui_song_select_build(AsmUiNode *nodes, uint32_t cap) {
    uint32_t n = 0, panels = 0, pixels = 0;
    uint32_t gauge_node = UINT32_MAX, play_panel_node = UINT32_MAX;
    uint32_t jacket_accent_node = UINT32_MAX, jacket_image_node = UINT32_MAX, nav_selected_node = UINT32_MAX, nav_selected_icon_node = UINT32_MAX;
#define PANEL(X,Y,W,H,C,R) do { if(!asmui_song_push_rect(nodes,cap,&n,(X),(Y),(W),(H),(C),ASMUI_MATERIAL_ROUNDED,(R),0.0f)) goto done; ++panels; } while(0)
#define RECT(X,Y,W,H,C) do { if(!asmui_song_push_rect(nodes,cap,&n,(X),(Y),(W),(H),(C),ASMUI_MATERIAL_DEFAULT,0.0f,0.0f)) goto done; } while(0)
#define TEXT(S,X,Y,Z,C) do { if(!asmui_song_push_text(nodes,cap,&n,(S),(X),(Y),(Z),(C),&pixels)) goto done; } while(0)

    /* Deep navy background, then subtle structural bars. */
    RECT(0, 0, 1280, 720, 0x080D1FFFu);
    RECT(0, 318, 1280, 2, 0x25325D80u);
    RECT(0,-318, 1280, 2, 0x25325D80u);

    /* top status */
    PANEL(-500, 300, 180, 58, 0x131C38E8u, 16.0f);
    TEXT("SYNC", -548, 293, 4.0f, 0x66E6FFFFu);
    PANEL(510, 300, 170, 58, 0x131C38E8u, 16.0f);
    TEXT("128", 468, 293, 4.0f, 0xFFFFFFFFu);

    /* Left navigation rail. */
    PANEL(-548, 0, 116, 540, 0x101832F0u, 28.0f);
    for (int i=0;i<5;++i) {
        float yy = 170.0f - (float)i * 86.0f;
        if (i == 2) nav_selected_node = n;
        PANEL(-548, yy, 72, 58, i==2 ? 0x2E8FFFFFu : 0x182548FFu, 18.0f);
        if (i == 2) nav_selected_icon_node = n;
        RECT(-548, yy, 18, 18, i==2 ? 0xD8FFFFFFu : 0x5F79A8FFu);
    }

    /* Main jacket card + fake artwork. */
    PANEL(-180, 30, 430, 500, 0x121B36FFu, 30.0f);
    PANEL(-180, 70, 362, 362, 0x202A55FFu, 24.0f);
    for (int i=0;i<6;++i) {
        float s = 300.0f - i*44.0f;
        if (i == 0) jacket_accent_node = n;
        PANEL(-180, 70, s, s, (i&1)?0x172750D0u:0x7B4CFFD0u, 18.0f);
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

    TEXT("NEON RHAPSODY", -350, -174, 5.0f, 0xF4F8FFFFu);
    TEXT("LUMINA", -350, -222, 3.0f, 0x7EA2D8FFu);

    /* Right difficulty/info card. */
    PANEL(330, 62, 470, 430, 0x101832F0u, 30.0f);
    TEXT("MASTER", 132, 226, 4.0f, 0xFF66D8FFu);
    PANEL(500, 218, 92, 62, 0xB62C8FFFu, 20.0f);
    TEXT("14+", 465, 210, 5.0f, 0xFFFFFFFFu);

    TEXT("SCORE", 136, 130, 3.0f, 0x6680AAFFu);
    TEXT("998742", 136, 84, 4.5f, 0xF6FAFFFFu);
    TEXT("COMBO", 136, 12, 3.0f, 0x6680AAFFu);
    TEXT("1284", 136, -34, 4.5f, 0xF6FAFFFFu);

    /* progress / filled-image semantic path */
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

    /* Play CTA deliberately bright so v0.8 glow path highlights it. */
    play_panel_node = n;
    PANEL(330, -225, 300, 92, 0x33BDEFFFu, 28.0f);
    TEXT("PLAY", 254, -236, 6.0f, 0xFFFFFFFFu);

    /* Song list peeking from lower right: tests repeated material batching. */
    for (int i=0;i<3;++i) {
        float yy = -320.0f - i*70.0f;
        PANEL(330, yy, 470, 54, i==0 ? 0x21315DFFu : 0x111A34FFu, 14.0f);
        RECT(138, yy, 34, 34, i==0 ? 0x69E7FFFFu : 0x415578FFu);
    }

done:
#undef PANEL
#undef RECT
#undef TEXT
    AsmUiSongSceneInfo info = {
        .node_count = n,
        .panel_count = panels,
        .glyph_pixel_count = pixels,
        .gauge_node = gauge_node,
        .play_panel_node = play_panel_node,
        .jacket_accent_node = jacket_accent_node,
        .jacket_image_node = jacket_image_node,
        .nav_selected_node = nav_selected_node,
        .nav_selected_icon_node = nav_selected_icon_node,
    };
    return info;
}

/* Small deterministic animation layer for the live demo. It deliberately only
 * mutates authored node state; layout/draw/batch stay in the ASM hot path. */
static inline void asmui_song_select_update(AsmUiNode *nodes,
                                             const AsmUiSongSceneInfo *info,
                                             float time_s,
                                             int focus_delta) {
    if (!nodes || !info) return;
    const float pulse = 0.5f + 0.5f * sinf(time_s * 5.0f);
    const float slow = sinf(time_s * 0.85f);
    if (info->gauge_node < info->node_count) {
        nodes[info->gauge_node].fill_amount = 0.68f + 0.20f * (0.5f + 0.5f * sinf(time_s * 1.3f));
    }
    if (info->play_panel_node < info->node_count) {
        const float s = 1.0f + 0.025f * pulse;
        nodes[info->play_panel_node].scale_x = s;
        nodes[info->play_panel_node].scale_y = s;
    }
    if (info->jacket_accent_node < info->node_count) {
        nodes[info->jacket_accent_node].rotation = slow * 0.035f;
    }
    if (info->nav_selected_node < info->node_count) {
        nodes[info->nav_selected_node].anchored_x = -548.0f + (float)focus_delta * 4.0f + 2.0f * slow;
    }
}

#endif
