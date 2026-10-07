#include "asmui_interaction.h"
#include "asmui_builder.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int nearf(float a, float b, float e) { return fabsf(a - b) <= e; }

int main(void) {
    AsmUiImagePlacement p;

    if (!asmui_image_place(1600, 900, 320, 320, ASMUI_ASPECT_CONTAIN, &p) ||
        !nearf(p.width, 320, .01f) || !nearf(p.height, 180, .01f) ||
        !nearf(p.u0, 0, .0001f) || !nearf(p.u1, 1, .0001f)) {
        fprintf(stderr, "aspect contain regression\n");
        return 1;
    }

    if (!asmui_image_place(1600, 900, 320, 320, ASMUI_ASPECT_COVER, &p) ||
        !nearf(p.width, 320, .01f) || !nearf(p.height, 320, .01f) ||
        !nearf(p.u0, 0.21875f, .0002f) || !nearf(p.u1, 0.78125f, .0002f) ||
        !nearf(p.v0, 0, .0001f) || !nearf(p.v1, 1, .0001f)) {
        fprintf(stderr, "aspect cover regression: %.5f..%.5f\n", p.u0, p.u1);
        return 2;
    }

    AsmUiNode node = asmui_node(ASMUI_NODE_IMAGE, -1, 0xffffffffu);
    asmui_apply_image_placement(&node, &p);
    if (!nearf(node.size_delta_x, 320, .01f) || !nearf(node.uv_x0, .21875f, .0002f)) {
        fprintf(stderr, "aspect apply regression\n");
        return 3;
    }

    AsmUiSnapScroll s;
    asmui_scroll_init(&s, 0.0f, -4.0f, 4.0f, 90.0f);
    asmui_scroll_set_target(&s, 3.0f);
    for (int i = 0; i < 240; ++i) asmui_scroll_step(&s, 1.0f / 120.0f);
    if (!nearf(s.position, 3.0f, .01f) || fabsf(s.velocity) > .03f) {
        fprintf(stderr, "snap-scroll convergence regression: pos=%.4f vel=%.4f\n", s.position, s.velocity);
        return 4;
    }
    asmui_scroll_nudge(&s, 100.0f);
    if (!nearf(s.target, 4.0f, .0001f)) {
        fprintf(stderr, "snap-scroll clamp regression\n");
        return 5;
    }

    AsmUiCarousel c;
    asmui_carousel_init(&c, 7u, 3u, 100.0f, -300.0f, 150.0f, 120.0f);
    AsmUiCarouselItemVisual center = asmui_carousel_item(&c, 3u);
    AsmUiCarouselItemVisual left = asmui_carousel_item(&c, 2u);
    if (!nearf(center.x, 100.0f, .001f) || !nearf(center.scale, 1.10f, .001f) ||
        !(center.alpha > left.alpha) || !(center.scale > left.scale)) {
        fprintf(stderr, "carousel initial visual regression\n");
        return 6;
    }
    asmui_carousel_nudge(&c, +2);
    for (int i = 0; i < 240; ++i) asmui_carousel_step(&c, 1.0f / 120.0f);
    if (c.selected != 5u || !nearf(c.scroll.position, 5.0f, .01f)) {
        fprintf(stderr, "carousel snap regression: selected=%u pos=%.4f\n", c.selected, c.scroll.position);
        return 7;
    }
    AsmUiNode card = asmui_node(ASMUI_NODE_IMAGE, -1, 0xffffffffu);
    asmui_carousel_apply_node(&c, 5u, &card);
    if (!nearf(card.anchored_x, 100.0f, .5f) || !nearf(card.scale_x, 1.10f, .01f) || card.alpha < .99f) {
        fprintf(stderr, "carousel node application regression\n");
        return 8;
    }

    float mx = 0.0f, my = 0.0f;
    asmui_pointer_to_canvas(730.0f, 678.0f, 1280.0f, 720.0f, &mx, &my);
    if (!nearf(mx, 90.0f, .001f) || !nearf(my, -318.0f, .001f)) {
        fprintf(stderr, "pointer coordinate conversion regression: %.2f %.2f\n", mx, my);
        return 9;
    }

    AsmUiCarousel pc;
    asmui_carousel_init(&pc, 7u, 3u, 90.0f, -318.0f, 156.0f, 125.0f);
    int hit = asmui_carousel_hit_test(&pc, 90.0f, -318.0f, 142.0f, 66.0f);
    if (hit != 3) {
        fprintf(stderr, "carousel hit-test regression: %d\n", hit);
        return 10;
    }
    asmui_carousel_pointer_begin(&pc, 90.0f, 0.0f);
    for (int i = 1; i <= 8; ++i) {
        float x = 90.0f - 156.0f * ((float)i / 8.0f);
        asmui_carousel_pointer_move(&pc, x, (float)i / 60.0f);
    }
    asmui_carousel_pointer_end(&pc);
    for (int i = 0; i < 240; ++i) asmui_carousel_step(&pc, 1.0f / 120.0f);
    if (pc.selected != 4u || !nearf(pc.scroll.position, 4.0f, .02f)) {
        fprintf(stderr, "carousel drag/snap regression: selected=%u pos=%.3f\n", pc.selected, pc.scroll.position);
        return 11;
    }
    asmui_carousel_wheel(&pc, -1.0f);
    if (pc.selected != 5u) {
        fprintf(stderr, "carousel wheel regression: selected=%u\n", pc.selected);
        return 12;
    }

    printf("interaction OK: contain=320x180 cover-u=[%.5f %.5f] spring-pos=%.3f target=%.1f carousel=%u@%.2f pointer-hit=%d drag->%u wheel->%u\n",
           p.u0, p.u1, s.position, s.target, c.selected, c.scroll.position, hit, 4u, pc.selected);
    return 0;
}
