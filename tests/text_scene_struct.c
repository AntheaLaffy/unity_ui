#include "demo/song_select_scene_text.h"
#include <stdio.h>

int main(void) {
    static AsmUiNode nodes[ASMUI_SONG_MAX_NODES];
    AsmUiSongTextSceneInfo s = asmui_song_select_build_text(nodes, ASMUI_SONG_MAX_NODES, 8.0f);
    if (s.base.node_count != 75u || s.text_node_count != 19u ||
        s.base.panel_count != 29u || s.carousel_count != 7u || s.base.glyph_pixel_count != 0u) {
        fprintf(stderr, "real-text retained scene structure mismatch: nodes=%u text=%u panels=%u pixel=%u\n",
                s.base.node_count, s.text_node_count, s.base.panel_count,
                s.base.glyph_pixel_count);
        return 1;
    }
    if (s.base.gauge_node >= s.base.node_count ||
        s.base.play_panel_node >= s.base.node_count ||
        s.base.jacket_accent_node >= s.base.node_count ||
        s.base.jacket_image_node >= s.base.node_count ||
        s.base.nav_selected_node >= s.base.node_count) {
        fprintf(stderr, "real-text scene animation handles invalid\n");
        return 2;
    }
    for (uint32_t i = 0; i < s.carousel_count; ++i) {
        if (s.carousel_root[i] >= s.base.node_count || s.carousel_jacket[i] >= s.base.node_count) {
            fprintf(stderr, "carousel root/jacket invalid at %u\n", i);
            return 3;
        }
    }
    puts("real-text retained scene OK: 75 nodes / 19 text nodes / 7 song cards / 7 jacket slots / 0 pixel-font quads");
    return 0;
}
