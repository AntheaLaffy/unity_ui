#include "asmui_application.h"
#include "asmui_builder.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static AsmUiAppDraw fixture[] = {
    {.kind=APP_RECT, .rgba=0xff0000ff, .rect={0,0,10,10}, .clip={0,0,1920,1080}},
    {.kind=APP_QUAD, .rgba=0xffffffff, .reserved=4, .rect={300,100,600,400}, .clip={350,150,400,300},
     .uv={.8f,.1f,.2f,.9f}, .text="original-atlas.png", .xy={700,500,400,500,300,100,900,100}}
};
static int frame(void *instance, float dt, uint32_t locale, AsmUiAppFrame *out) {
    (void)instance; (void)dt; (void)locale;
    *out=(AsmUiAppFrame){.draws=fixture,.count=2,.width=1920,.height=1080};
    return 1;
}
static uint32_t resolve(void *user, const char *path) {
    (void)user; assert(!strcmp(path,"original-atlas.png")); return 7;
}
static int nearf(float a, float b) { return fabsf(a-b)<.0001f; }
int main(void) {
    AsmUiNode nodes[8]={0}; AsmUiDrawCmd draws[8]={0}; AsmUiGpuVertex vertices[8]={0};
    AsmUiContext ctx; asm_ui_init(&ctx,nodes,8,draws,8,NULL,0);
    AsmUiApplicationV5 api={.frame=frame};
    AsmUiAppHost host={.api=&api,.resolve_image=resolve};
    const float viewport[]={100,50,960,540};
    nodes[0]=asmui_node(ASMUI_NODE_CONTAINER,-1,0xffffffff);
    nodes[0].size_delta_x=960; nodes[0].size_delta_y=540;
    nodes[0].anchored_x=-20; nodes[0].anchored_y=30;
    ctx.node_count=asmui_app_build(&host,nodes,1,8,0,viewport);
    assert(ctx.node_count==5 && host.scale==.5f);
    asm_ui_set_viewport(&ctx,1200,700);
    asm_ui_begin_frame(&ctx,0); asm_ui_layout(&ctx); asm_ui_build_drawlist(&ctx);
    asmui_app_patch_quads(&host,&ctx);
    assert(ctx.draw_count==2);
    assert(draws[1].resource_id==7 && draws[1].payload==NULL);
    assert(draws[1].material_id==ASMUI_MATERIAL_ADDITIVE);
    assert(nearf(draws[1].clip_x,275) && nearf(draws[1].clip_y,125));
    assert(nearf(draws[1].clip_w,200) && nearf(draws[1].clip_h,150));
    assert(asm_ui_build_gpu_vertices(&ctx,vertices,8)==8);
    const float expected_uv[]={.8f,.9f,.2f,.9f,.2f,.1f,.8f,.1f};
    for(unsigned i=0;i<4;++i) {
        assert(nearf(vertices[4+i].x,100+fixture[1].xy[i*2]*.5f));
        assert(nearf(vertices[4+i].y,50+fixture[1].xy[i*2+1]*.5f));
        assert(nearf(vertices[4+i].u,expected_uv[i*2]));
        assert(nearf(vertices[4+i].v,expected_uv[i*2+1]));
    }
    fixture[1].reserved=8;
    fixture[1].font_size=.6f;
    fixture[1].min_font_size=-.25f;
    ctx.node_count=asmui_app_build(&host,nodes,1,8,0,viewport);
    asm_ui_begin_frame(&ctx,0); asm_ui_layout(&ctx); asm_ui_build_drawlist(&ctx);
    asmui_app_patch_quads(&host,&ctx);
    assert(ctx.draw_count==2 && draws[1].material_id==ASMUI_MATERIAL_ATLAS_RGB_MIX);
    assert(nearf(draws[1].material0,.6f) && nearf(draws[1].material1,0) && nearf(draws[1].material2,-.25f));
    assert(asm_ui_build_gpu_vertices(&ctx,vertices,8)==8);
    assert(nearf(vertices[4].material0,.6f) && nearf(vertices[4].material2,-.25f));
    /* World rectangle [-1,1]^2, camera depth z=4+y. The projected
       diagonals meet at the world center, with unequal screen fractions. */
    const float projected[]={640-200.f/3,360-200.f/3,640+200.f/3,360-200.f/3,
                             680,400,600,400};
    fixture[1].reserved=16;
    memcpy(fixture[1].xy,projected,sizeof(projected));
    ctx.node_count=asmui_app_build(&host,nodes,1,8,0,viewport);
    asm_ui_begin_frame(&ctx,0); asm_ui_layout(&ctx); asm_ui_build_drawlist(&ctx);
    asmui_app_patch_quads(&host,&ctx);
    assert(draws[1].material_id==ASMUI_MATERIAL_PROJECTIVE);
    assert(asm_ui_build_gpu_vertices(&ctx,vertices,8)==8);
    const float expected_w[]={.75f,.75f,1.25f,1.25f};
    for(unsigned i=0;i<4;++i) assert(nearf(vertices[4+i].material3,expected_w[i]));
    /* Perspective division maps the diagonal center to exactly half its UV,
       unlike affine interpolation (.625). */
    float t=.625f;
    float uv_center=(t/vertices[6].material3)/((1-t)/vertices[4].material3+t/vertices[6].material3);
    assert(nearf(uv_center,.5f));
    const float degenerate[]={100,100,200,100,300,100,400,100};
    memcpy(fixture[1].xy,degenerate,sizeof(degenerate));
    ctx.node_count=asmui_app_build(&host,nodes,1,8,0,viewport);
    asm_ui_begin_frame(&ctx,0); asm_ui_layout(&ctx); asm_ui_build_drawlist(&ctx);
    asmui_app_patch_quads(&host,&ctx);
    assert(asm_ui_build_gpu_vertices(&ctx,vertices,8)==8);
    for(unsigned i=0;i<4;++i) assert(vertices[4+i].material3==1);
    puts("Application quad: ABI, scaling, clipping, atlas UV and projective depth recovery OK");
    return 0;
}
