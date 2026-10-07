#include "asmui.h"
#include <stdint.h>

static int32_t nearest_offscreen_owner(const AsmUiContext *ctx, uint32_t source_plus_one) {
    if (!ctx || !source_plus_one) return -1;
    uint32_t idx = source_plus_one - 1u;
    uint32_t guard = 0;
    while (idx < ctx->node_count && guard++ < 128u) {
        const AsmUiNode *n = &ctx->nodes[idx];
        if (n->layer_flags & ASMUI_LAYER_OFFSCREEN) return (int32_t)idx;
        if (n->parent < 0 || (uint32_t)n->parent >= ctx->node_count) break;
        idx = (uint32_t)n->parent;
    }
    return -1;
}

static int emit(AsmUiRenderOp *ops, uint32_t cap, uint32_t *count,
                uint32_t type, uint32_t node_index,
                uint32_t first_draw, uint32_t draw_count) {
    if (*count >= cap) return 0;
    ops[*count] = (AsmUiRenderOp){ type, node_index, first_draw, draw_count };
    ++*count;
    return 1;
}

uint32_t asmui_build_render_ops(const AsmUiContext *ctx,
                                AsmUiRenderOp *ops,
                                uint32_t op_capacity) {
    if (!ctx || !ops || !op_capacity) return 0;
    uint32_t out = 0;
    uint32_t i = 0;

    while (i < ctx->draw_count) {
        int32_t owner = nearest_offscreen_owner(ctx, ctx->draws[i].source_node);
        uint32_t first = i;
        ++i;
        while (i < ctx->draw_count &&
               nearest_offscreen_owner(ctx, ctx->draws[i].source_node) == owner) {
            ++i;
        }
        uint32_t count = i - first;

        if (owner < 0) {
            if (!emit(ops, op_capacity, &out, ASMUI_RENDER_DRAW_RANGE,
                      UINT32_MAX, first, count)) return 0;
            continue;
        }

        /* v0.6 deliberately rejects a layer whose subtree is not contiguous in
           painter order. That keeps offscreen composition deterministic and
           catches malformed/non-preorder node arrays early. */
        for (uint32_t k = 0; k < out; ++k) {
            if (ops[k].type == ASMUI_RENDER_COMPOSITE_LAYER &&
                ops[k].node_index == (uint32_t)owner) return 0;
        }

        if (!emit(ops, op_capacity, &out, ASMUI_RENDER_BEGIN_LAYER,
                  (uint32_t)owner, first, 0)) return 0;
        if (!emit(ops, op_capacity, &out, ASMUI_RENDER_DRAW_RANGE,
                  (uint32_t)owner, first, count)) return 0;
        if (!emit(ops, op_capacity, &out, ASMUI_RENDER_END_LAYER,
                  (uint32_t)owner, first, count)) return 0;
        if (!emit(ops, op_capacity, &out, ASMUI_RENDER_COMPOSITE_LAYER,
                  (uint32_t)owner, first, count)) return 0;
    }
    return out;
}
