#version 450

layout(set=0,binding=0) uniform sampler2D tex0;
layout(push_constant) uniform Push {
    vec2 viewport;
    uint material_id;
    uint flags;
    vec4 mask_rect;
    vec2 mask_params;
    uint mask_flags;
    uint pad0;
} pc;

layout(location=0) in vec2 v_uv;
layout(location=1) in vec2 v_local;
layout(location=2) in vec4 v_color;
layout(location=3) in vec4 v_material;
layout(location=4) noperspective in vec2 v_screen;
layout(location=0) out vec4 out_color;

const uint MAT_ROUNDED   = 0x101u;
const uint MAT_OUTLINE   = 0x102u;
const uint MAT_GRAYSCALE = 0x103u;
const uint MAT_HSV       = 0x104u;
const uint MAT_GLOW      = 0x105u;
const uint MAT_MSDF_TEXT = 0x106u;
const uint MAT_LINE      = 0x107u;
const uint MAT_GRID      = 0x108u;
const uint MAT_GROUND    = 0x109u;
const uint MAT_GRID_2D   = 0x10au;
const uint MAT_ATLAS_RGB_MIX = 0x10bu;
const uint HAS_SOFT_MASK = 1u;

float median3(float r, float g, float b) {
    return max(min(r, g), min(max(r, g), b));
}

vec3 rgb2hsv(vec3 c) {
    vec4 K = vec4(0.0, -1.0/3.0, 2.0/3.0, -1.0);
    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return vec3(abs(q.z + (q.w-q.y)/(6.0*d+e)), d/(q.x+e), q.x);
}

vec3 hsv2rgb(vec3 c) {
    vec3 p = abs(fract(c.xxx + vec3(0.0,2.0/3.0,1.0/3.0))*6.0-3.0);
    return c.z * mix(vec3(1.0), clamp(p-1.0,0.0,1.0), c.y);
}

float rounded_mask(vec2 uv, float radius_px) {
    vec2 px_size = 1.0 / max(fwidth(uv), vec2(1.0e-6));
    vec2 half_size = 0.5 * px_size;
    float r = clamp(radius_px, 0.0, min(half_size.x, half_size.y));
    vec2 p = (uv - 0.5) * px_size;
    vec2 q = abs(p) - (half_size - vec2(r));
    float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
    return 1.0 - smoothstep(-0.75, 0.75, d);
}

float soft_rect_mask(vec2 screen, vec4 rect, float radius, float softness) {
    vec2 half_size = rect.zw * 0.5;
    vec2 center = rect.xy + half_size;
    float r = clamp(radius, 0.0, min(half_size.x, half_size.y));
    vec2 q = abs(screen - center) - (half_size - vec2(r));
    float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
    float feather = max(softness, 0.5);
    return 1.0 - smoothstep(-feather, feather, d);
}

float neighbor_alpha(vec2 texel, float radius) {
    vec2 d = texel * max(radius, 1.0);
    float a = 0.0;
    a = max(a, texture(tex0, v_uv + vec2( d.x, 0)).a);
    a = max(a, texture(tex0, v_uv + vec2(-d.x, 0)).a);
    a = max(a, texture(tex0, v_uv + vec2(0, d.y)).a);
    a = max(a, texture(tex0, v_uv + vec2(0,-d.y)).a);
    a = max(a, texture(tex0, v_uv + vec2( d.x, d.y)).a);
    a = max(a, texture(tex0, v_uv + vec2(-d.x, d.y)).a);
    a = max(a, texture(tex0, v_uv + vec2( d.x,-d.y)).a);
    a = max(a, texture(tex0, v_uv + vec2(-d.x,-d.y)).a);
    return a;
}

float msdf_opacity(vec3 sample_rgb, float px_range) {
    float sd = median3(sample_rgb.r, sample_rgb.g, sample_rgb.b) - 0.5;
    vec2 unit_range = vec2(max(px_range, 1.0)) / vec2(textureSize(tex0, 0));
    vec2 screen_tex_size = 1.0 / max(fwidth(v_uv), vec2(1.0e-6));
    float screen_px_range = max(0.5 * dot(unit_range, screen_tex_size), 1.0);
    return clamp(sd * screen_px_range + 0.5, 0.0, 1.0);
}

vec4 orthographic_grid(vec2 uv, float scale) {
    vec2 box=1.0/max(fwidth(uv),vec2(1e-6));
    vec2 p=(uv-0.5)*vec2(box.x/box.y,1.0)*scale+vec2(0.0,0.35);
    vec2 fw=max(fwidth(p),vec2(1e-4));
    vec2 g=abs(fract(p-0.5)-0.5)/fw;
    float line=1.0-min(min(g.x,g.y),1.0);
    vec3 color=vec3(0.365)+vec3(line*0.11);
    color=mix(color,vec3(0.57,0.32,0.29),1.0-smoothstep(0.0,fw.y*1.25,abs(p.y)));
    color=mix(color,vec3(0.35,0.52,0.29),1.0-smoothstep(0.0,fw.x*1.25,abs(p.x)));
    return vec4(color,1.0);
}

// The SSE2 projector uses the same spherical camera, keeping the procedural
// world grid registered with the mesh when orbiting or zooming.
vec4 perspective_grid(vec2 uv, vec4 camera) {
    float cy=cos(camera.x), sy=sin(camera.x), cp=cos(camera.y), sp=sin(camera.y);
    vec3 right=vec3(cy,0,-sy), up=vec3(-sy*sp,cp,-cy*sp), back=vec3(sy*cp,sp,cy*cp);
    vec2 box=1.0/max(fwidth(uv),vec2(1e-6));
    vec2 ndc=uv*2.0-1.0;
    vec3 origin=back*camera.z+vec3(0,0.35,0);
    vec3 ray=right*(ndc.x*box.x/box.y/camera.w)+up*(ndc.y/camera.w)-back;
    float t=origin.y/max(-ray.y,1e-5);
    vec2 p=(origin+ray*t).xz;
    vec2 fw=max(fwidth(p),vec2(1e-4));
    vec2 g=abs(fract(p-0.5)-0.5)/fw;
    vec2 major=abs(fract(p/5.0-0.5)-0.5)/(fw/5.0);
    float fine=1.0-min(min(g.x,g.y),1.0);
    float coarse=1.0-min(min(major.x,major.y),1.0);
    float fade=exp(-length(p)*0.035);
    float lines=pc.material_id == MAT_GRID ? 1.0 : 0.0;
    vec3 color=vec3(0.365)+vec3((fine*0.095+coarse*0.045)*fade*lines);
    float xaxis=1.0-smoothstep(0.0,fw.y*1.25,abs(p.y));
    float zaxis=1.0-smoothstep(0.0,fw.x*1.25,abs(p.x));
    color=mix(color,vec3(0.57,0.32,0.29),xaxis*fade*0.8*lines);
    color=mix(color,vec3(0.29,0.43,0.61),zaxis*fade*0.8*lines);
    return vec4(color,smoothstep(0.0,0.018,-ray.y));
}

void main() {
    vec4 sampled = texture(tex0, v_uv);
    vec4 base;

    if (pc.material_id == MAT_ATLAS_RGB_MIX) {
        vec3 alternate = texture(tex0, v_uv + v_material.yz).rgb;
        base = vec4(mix(sampled.rgb, alternate, clamp(v_material.x, 0.0, 1.0)), sampled.a) * v_color;
    } else if (pc.material_id == MAT_MSDF_TEXT) {
        float a = msdf_opacity(sampled.rgb, v_material.x);
        base = vec4(v_color.rgb, v_color.a * a);
    } else {
        base = sampled * v_color;
    }

    if (pc.material_id == MAT_ROUNDED) {
        base.a *= rounded_mask(v_local, v_material.x);
    } else if (pc.material_id == MAT_LINE) {
        float px=1.0/max(length(vec2(dFdx(v_local.y),dFdy(v_local.y))),1e-6);
        float d=abs(v_local.y-0.5)*px-v_material.x*0.5;
        base.a*=1.0-smoothstep(-0.5,0.5,d);
    } else if (pc.material_id == MAT_GRID_2D) {
        base = orthographic_grid(v_local, v_material.z);
    } else if (pc.material_id == MAT_GRID || pc.material_id == MAT_GROUND) {
        base=perspective_grid(v_local,v_material);
    } else if (pc.material_id == MAT_GRAYSCALE) {
        float y = dot(base.rgb, vec3(0.2126, 0.7152, 0.0722));
        base.rgb = mix(base.rgb, vec3(y), clamp(v_material.x, 0.0, 1.0));
    } else if (pc.material_id == MAT_HSV) {
        vec3 hsv = rgb2hsv(base.rgb);
        hsv.x = fract(hsv.x + v_material.x);
        hsv.y = clamp(hsv.y * v_material.y, 0.0, 1.0);
        hsv.z = max(0.0, hsv.z * v_material.z);
        base.rgb = hsv2rgb(hsv);
    } else if (pc.material_id == MAT_OUTLINE || pc.material_id == MAT_GLOW) {
        vec2 texel = 1.0 / vec2(textureSize(tex0, 0));
        float around = neighbor_alpha(texel, v_material.x);
        float edge = max(0.0, around - sampled.a);
        float intensity = max(v_material.y, 0.0);
        if (pc.material_id == MAT_OUTLINE) {
            base.rgb = mix(base.rgb, v_color.rgb, clamp(edge * intensity, 0.0, 1.0));
            base.a = max(base.a, edge * intensity * v_color.a);
        } else {
            float glow = clamp(around * intensity, 0.0, 1.0);
            base.rgb += v_color.rgb * glow * (1.0 - base.a);
            base.a = max(base.a, glow * v_color.a);
        }
    }

    if ((pc.mask_flags & HAS_SOFT_MASK) != 0u) {
        base.a *= soft_rect_mask(v_screen, pc.mask_rect, pc.mask_params.x, pc.mask_params.y);
    }

    out_color = base;
}
