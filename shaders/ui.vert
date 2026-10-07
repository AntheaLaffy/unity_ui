#version 450

layout(push_constant) uniform Push {
    vec2 viewport;
    uint material_id;
    uint flags;
    vec4 mask_rect;      // x,y,w,h in ASMUI screen coordinates (top-left origin)
    vec2 mask_params;    // radius, softness
    uint mask_flags;
    uint pad0;
} pc;

layout(location=0) in vec2 in_pos;
layout(location=1) in vec2 in_uv;
layout(location=2) in vec2 in_local;
layout(location=3) in uint in_rgba;
layout(location=4) in float in_alpha;
layout(location=5) in vec4 in_material;

layout(location=0) out vec2 v_uv;
layout(location=1) out vec2 v_local;
layout(location=2) out vec4 v_color;
layout(location=3) out vec4 v_material;
layout(location=4) noperspective out vec2 v_screen;

vec4 unpack_rgba(uint c) {
    return vec4(float((c >> 24) & 255u),
                float((c >> 16) & 255u),
                float((c >>  8) & 255u),
                float(c & 255u)) / 255.0;
}

void main() {
    // ASMUI screen coordinates use a top-left origin with +Y downward.
    // A Vulkan viewport with positive height maps NDC -1 to framebuffer y=0
    // and NDC +1 to framebuffer y=height, so DO NOT apply the OpenGL-style
    // Y inversion here.  The previous 1-2*y/H mapping vertically mirrored
    // every non-symmetric UI while center-only smoke tests still passed.
    vec2 ndc = vec2(in_pos.x / pc.viewport.x * 2.0 - 1.0,
                    in_pos.y / pc.viewport.y * 2.0 - 1.0);
    float w = pc.material_id == 0x10cu ? max(in_material.w, 0.000001) : 1.0;
    gl_Position = vec4(ndc*w, 0.0, w);
    v_uv = in_uv;
    v_local = in_local;
    v_color = unpack_rgba(in_rgba);
    v_color.a *= in_alpha;
    v_material = in_material;
    v_screen = in_pos;
}
