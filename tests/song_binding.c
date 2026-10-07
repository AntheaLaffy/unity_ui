#include "demo/song_select_scene_text.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    static AsmUiNode nodes[ASMUI_SONG_MAX_NODES];
    AsmUiSongTextSceneInfo s = asmui_song_select_build_text(nodes, ASMUI_SONG_MAX_NODES, 8.0f);
    if (s.carousel_count != ASMUI_SONG_CAROUSEL_COUNT) return 1;

    for (uint32_t i = 0; i < s.carousel_count; ++i) {
        if (!asmui_song_select_bind(nodes, &s, i)) {
            fprintf(stderr, "bind failed at %u\n", i);
            return 2;
        }
        uint32_t n = 0;
        const AsmUiSongMeta *songs = asmui_song_catalog(&n);
        const AsmUiSongMeta *m = &songs[i];
        if (strcmp(nodes[s.title_node].payload, m->title) ||
            strcmp(nodes[s.artist_node].payload, m->artist) ||
            strcmp(nodes[s.subtitle_node].payload, m->subtitle) ||
            strcmp(nodes[s.difficulty_node].payload, m->difficulty) ||
            strcmp(nodes[s.level_node].payload, m->level) ||
            strcmp(nodes[s.score_value_node].payload, m->score) ||
            strcmp(nodes[s.combo_value_node].payload, m->combo) ||
            nodes[s.difficulty_panel_node].rgba != m->badge_rgba ||
            nodes[s.base.jacket_accent_node].rgba != m->accent_rgba ||
            nodes[s.base.gauge_node].fill_amount != m->gauge) {
            fprintf(stderr, "metadata mismatch at %u\n", i);
            return 3;
        }
    }

    if (!asmui_song_select_bind(nodes, &s, 999u) ||
        strcmp(nodes[s.title_node].payload, "ASTER")) {
        fprintf(stderr, "clamped selection binding failed\n");
        return 4;
    }

    puts("song detail binding OK: 7 selections -> title/artist/subtitle/difficulty/score/combo/gauge/accent");
    return 0;
}
