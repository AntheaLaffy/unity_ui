#version 450

layout(set=0,binding=0) uniform sampler2D tex0;
layout(push_constant) uniform Push {
    vec2 texel_step;
    float radius;
    float strength;
    uint mode;
    uint pad0;
    uint pad1;
    uint pad2;
} pc;

layout(location=0) in vec2 v_uv;
layout(location=0) out vec4 out_color;

vec4 gaussian9(vec2 uv, vec2 stepv) {
    const float w0 = 0.2270270270;
    const float w1 = 0.1945945946;
    const float w2 = 0.1216216216;
    const float w3 = 0.0540540541;
    const float w4 = 0.0162162162;
    vec4 c = texture(tex0, uv) * w0;
    c += texture(tex0, uv + stepv * 1.0) * w1;
    c += texture(tex0, uv - stepv * 1.0) * w1;
    c += texture(tex0, uv + stepv * 2.0) * w2;
    c += texture(tex0, uv - stepv * 2.0) * w2;
    c += texture(tex0, uv + stepv * 3.0) * w3;
    c += texture(tex0, uv - stepv * 3.0) * w3;
    c += texture(tex0, uv + stepv * 4.0) * w4;
    c += texture(tex0, uv - stepv * 4.0) * w4;
    return c;
}

void main() {
    if (pc.mode == 1u) {
        vec2 stepv = pc.texel_step * max(pc.radius, 0.0);
        vec4 blurred = gaussian9(v_uv, stepv);
        out_color = blurred * max(pc.strength, 0.0);
    } else if (pc.mode == 2u) {
        vec4 src = texture(tex0, v_uv);
        float threshold = clamp(pc.radius, 0.0, 1.0);
        float peak = max(src.r, max(src.g, src.b));
        float gate = smoothstep(threshold, min(1.0, threshold + 0.08), peak);
        out_color = vec4(src.rgb * gate * max(pc.strength, 0.0), src.a * gate);
    } else {
        out_color = texture(tex0, v_uv) * max(pc.strength, 0.0);
    }
}
