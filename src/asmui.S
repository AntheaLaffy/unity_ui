.intel_syntax noprefix
.text

// Linux x86-64 System V ABI.
// Core intentionally uses only the C ABI + libm, so the rendering backend can be swapped.

.equ NODE_STRIDE, 256
.equ DRAW_STRIDE, 168
.equ TWEEN_STRIDE, 32

// Context offsets
.equ C_NODES, 0
.equ C_NODE_COUNT, 8
.equ C_NODE_CAP, 12
.equ C_DRAWS, 16
.equ C_DRAW_COUNT, 24
.equ C_DRAW_CAP, 28
.equ C_TWEENS, 32
.equ C_TWEEN_COUNT, 40
.equ C_TWEEN_CAP, 44
.equ C_VIEW_W, 48
.equ C_VIEW_H, 52
.equ C_TIME, 56
.equ C_DT, 60

// Node offsets
.equ N_TYPE, 0
.equ N_FLAGS, 4
.equ N_PARENT, 8
.equ N_RESOURCE, 12
.equ N_AMIN_X, 16
.equ N_AMIN_Y, 20
.equ N_AMAX_X, 24
.equ N_AMAX_Y, 28
.equ N_PIVOT_X, 32
.equ N_PIVOT_Y, 36
.equ N_ANCH_X, 40
.equ N_ANCH_Y, 44
.equ N_SIZE_X, 48
.equ N_SIZE_Y, 52
.equ N_ROT, 56
.equ N_SCALE_X, 60
.equ N_SCALE_Y, 64
.equ N_ALPHA, 68
.equ N_RGBA, 72
.equ N_LAYER_FLAGS, 76
.equ N_PAYLOAD, 80
.equ N_M00, 88
.equ N_M01, 92
.equ N_M02, 96
.equ N_M10, 100
.equ N_M11, 104
.equ N_M12, 108
.equ N_WIDTH, 112
.equ N_HEIGHT, 116
.equ N_WORLD_ALPHA, 120
.equ N_CLIP_X, 124
.equ N_CLIP_Y, 128
.equ N_CLIP_W, 132
.equ N_CLIP_H, 136
.equ N_FONT_SIZE, 140
.equ N_IMAGE_MODE, 144
.equ N_MATERIAL, 148
.equ N_UV_X0, 152
.equ N_UV_Y0, 156
.equ N_UV_X1, 160
.equ N_UV_Y1, 164
.equ N_SOURCE_W, 168
.equ N_SOURCE_H, 172
.equ N_BORDER_L, 176
.equ N_BORDER_R, 180
.equ N_BORDER_B, 184
.equ N_BORDER_T, 188
.equ N_MAT0, 192
.equ N_MAT1, 196
.equ N_MAT2, 200
.equ N_MAT3, 204
.equ N_TEXT_ALIGN, 208
.equ N_FILL, 212
.equ N_MASK_MODE, 216
.equ N_MASK_RADIUS, 220
.equ N_MASK_SOFTNESS, 224
.equ N_MASK_X, 228
.equ N_MASK_Y, 232
.equ N_MASK_W, 236
.equ N_MASK_H, 240
.equ N_MASK_WORLD_RADIUS, 244
.equ N_MASK_WORLD_SOFTNESS, 248
.equ N_MASK_FLAGS, 252

// Tween offsets
.equ T_NODE, 0
.equ T_PROP, 4
.equ T_EASING, 8
.equ T_FLAGS, 12
.equ T_START, 16
.equ T_DURATION, 20
.equ T_FROM, 24
.equ T_TO, 28

// Draw offsets
.equ D_TYPE, 0
.equ D_RESOURCE, 4
.equ D_RGBA, 8
.equ D_FLAGS, 12
.equ D_X0, 16
.equ D_Y0, 20
.equ D_X1, 24
.equ D_Y1, 28
.equ D_X2, 32
.equ D_Y2, 36
.equ D_X3, 40
.equ D_Y3, 44
.equ D_ALPHA, 48
.equ D_U0, 52
.equ D_V0, 56
.equ D_U1, 60
.equ D_V1, 64
.equ D_CLIP_X, 68
.equ D_CLIP_Y, 72
.equ D_CLIP_W, 76
.equ D_CLIP_H, 80
.equ D_FONT_SIZE, 84
.equ D_PAYLOAD, 88
.equ D_MATERIAL, 96
.equ D_SOURCE_NODE, 100
.equ D_MAT0, 104
.equ D_MAT1, 108
.equ D_MAT2, 112
.equ D_MAT3, 116
.equ D_TEXT_ALIGN, 120
.equ D_STENCIL_DEPTH, 124
.equ D_LOCAL_W, 128
.equ D_LOCAL_H, 132
.equ D_MASK_X, 136
.equ D_MASK_Y, 140
.equ D_MASK_W, 144
.equ D_MASK_H, 148
.equ D_MASK_RADIUS, 152
.equ D_MASK_SOFTNESS, 156
.equ D_MASK_FLAGS, 160
.equ D_STENCIL_PARENT, 164

.global asm_ui_init
.type asm_ui_init,@function
asm_ui_init:
    // rdi=ctx rsi=nodes edx=node_cap rcx=draws r8d=draw_cap r9=tweens, 7th arg on stack
    mov [rdi + C_NODES], rsi
    mov dword ptr [rdi + C_NODE_COUNT], 0
    mov [rdi + C_NODE_CAP], edx
    mov [rdi + C_DRAWS], rcx
    mov dword ptr [rdi + C_DRAW_COUNT], 0
    mov [rdi + C_DRAW_CAP], r8d
    mov [rdi + C_TWEENS], r9
    mov dword ptr [rdi + C_TWEEN_COUNT], 0
    mov eax, dword ptr [rsp + 8]
    mov [rdi + C_TWEEN_CAP], eax
    xor eax, eax
    mov [rdi + C_VIEW_W], eax
    mov [rdi + C_VIEW_H], eax
    mov [rdi + C_TIME], eax
    mov [rdi + C_DT], eax
    ret

.global asm_ui_set_viewport
.type asm_ui_set_viewport,@function
asm_ui_set_viewport:
    movss [rdi + C_VIEW_W], xmm0
    movss [rdi + C_VIEW_H], xmm1
    ret

.global asm_ui_begin_frame
.type asm_ui_begin_frame,@function
asm_ui_begin_frame:
    movss [rdi + C_DT], xmm0
    movss xmm1, [rdi + C_TIME]
    addss xmm1, xmm0
    movss [rdi + C_TIME], xmm1
    mov dword ptr [rdi + C_DRAW_COUNT], 0
    ret

// xmm0=t in [0,1], eax=easing -> xmm0=eased
.type .ease,@function
.ease:
    cmp eax, 1
    je .ease_out_cubic
    cmp eax, 2
    je .ease_in_out_cubic
    ret
.ease_out_cubic:
    movss xmm1, dword ptr [rip + .LC_ONE]
    subss xmm1, xmm0
    movaps xmm2, xmm1
    mulss xmm2, xmm1
    mulss xmm2, xmm1
    movss xmm0, dword ptr [rip + .LC_ONE]
    subss xmm0, xmm2
    ret
.ease_in_out_cubic:
    movss xmm1, dword ptr [rip + .LC_HALF]
    ucomiss xmm0, xmm1
    jae .ease_io_hi
    movaps xmm1, xmm0
    mulss xmm1, xmm0
    mulss xmm1, xmm0
    mulss xmm1, dword ptr [rip + .LC_FOUR]
    movaps xmm0, xmm1
    ret
.ease_io_hi:
    // 1 - ((-2t + 2)^3)/2
    movss xmm1, dword ptr [rip + .LC_TWO]
    movss xmm2, dword ptr [rip + .LC_TWO]
    mulss xmm2, xmm0
    subss xmm1, xmm2
    movaps xmm2, xmm1
    mulss xmm2, xmm1
    mulss xmm2, xmm1
    mulss xmm2, dword ptr [rip + .LC_HALF]
    movss xmm0, dword ptr [rip + .LC_ONE]
    subss xmm0, xmm2
    ret

.global asm_ui_tick_tweens
.type asm_ui_tick_tweens,@function
asm_ui_tick_tweens:
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov r15, rdi
    mov r12, [r15 + C_TWEENS]
    mov r13d, [r15 + C_TWEEN_COUNT]
    xor r14d, r14d
.tween_loop:
    cmp r14d, r13d
    jae .tween_done
    mov eax, r14d
    imul rax, TWEEN_STRIDE
    lea rbx, [r12 + rax]
    mov eax, [rbx + T_FLAGS]
    test eax, 1
    jz .tween_next

    // t = (ctx.time - start) / duration
    movss xmm0, [r15 + C_TIME]
    subss xmm0, [rbx + T_START]
    movss xmm1, [rbx + T_DURATION]
    xorps xmm7, xmm7
    ucomiss xmm1, xmm7
    jbe .tween_force_one
    divss xmm0, xmm1
    jmp .tween_clamp
.tween_force_one:
    movss xmm0, dword ptr [rip + .LC_ONE]
.tween_clamp:
    xorps xmm1, xmm1
    maxss xmm0, xmm1
    movss xmm1, dword ptr [rip + .LC_ONE]
    minss xmm0, xmm1

    // save raw t in xmm6, ease it
    movaps xmm6, xmm0
    mov eax, [rbx + T_EASING]
    call .ease

    // value = from + (to-from)*eased
    movss xmm1, [rbx + T_TO]
    subss xmm1, [rbx + T_FROM]
    mulss xmm1, xmm0
    addss xmm1, [rbx + T_FROM]

    // node pointer
    mov eax, [rbx + T_NODE]
    cmp eax, [r15 + C_NODE_COUNT]
    jae .tween_finish_check
    imul rax, NODE_STRIDE
    add rax, [r15 + C_NODES]
    mov edx, [rbx + T_PROP]
    cmp edx, 1
    je .tw_ax
    cmp edx, 2
    je .tw_ay
    cmp edx, 3
    je .tw_sx
    cmp edx, 4
    je .tw_sy
    cmp edx, 5
    je .tw_rot
    cmp edx, 6
    je .tw_alpha
    cmp edx, 7
    je .tw_w
    cmp edx, 8
    je .tw_h
    cmp edx, 9
    je .tw_fill
    jmp .tween_finish_check
.tw_ax:    movss [rax + N_ANCH_X], xmm1; jmp .tween_finish_check
.tw_ay:    movss [rax + N_ANCH_Y], xmm1; jmp .tween_finish_check
.tw_sx:    movss [rax + N_SCALE_X], xmm1; jmp .tween_finish_check
.tw_sy:    movss [rax + N_SCALE_Y], xmm1; jmp .tween_finish_check
.tw_rot:   movss [rax + N_ROT], xmm1; jmp .tween_finish_check
.tw_alpha: movss [rax + N_ALPHA], xmm1; jmp .tween_finish_check
.tw_w:     movss [rax + N_SIZE_X], xmm1; jmp .tween_finish_check
.tw_h:     movss [rax + N_SIZE_Y], xmm1; jmp .tween_finish_check
.tw_fill:  movss [rax + N_FILL], xmm1

.tween_finish_check:
    // if raw t >= 1: loop or disable
    movss xmm0, dword ptr [rip + .LC_ONE]
    ucomiss xmm6, xmm0
    jb .tween_next
    mov eax, [rbx + T_FLAGS]
    test eax, 2
    jz .tween_disable
    movss xmm0, [r15 + C_TIME]
    movss [rbx + T_START], xmm0
    jmp .tween_next
.tween_disable:
    and eax, ~1
    mov [rbx + T_FLAGS], eax
.tween_next:
    inc r14d
    jmp .tween_loop
.tween_done:
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    ret

// Helper: apply 2x3 matrix at rdi to local point xmm0=x, xmm1=y.
// Matrix offsets expected m00..m12 contiguous at rdi.
// Returns world x in xmm2, world y in xmm3.
.type .apply_matrix,@function
.apply_matrix:
    movss xmm2, [rdi + 0]
    mulss xmm2, xmm0
    movss xmm4, [rdi + 4]
    mulss xmm4, xmm1
    addss xmm2, xmm4
    addss xmm2, [rdi + 8]
    movss xmm3, [rdi + 12]
    mulss xmm3, xmm0
    movss xmm4, [rdi + 16]
    mulss xmm4, xmm1
    addss xmm3, xmm4
    addss xmm3, [rdi + 20]
    ret

.global asm_ui_layout
.type asm_ui_layout,@function
asm_ui_layout:
    push rbp
    mov rbp, rsp
    push rbx
    push r12
    push r13
    push r14
    push r15
    sub rsp, 56                 // aligned locals; includes saved parent pointer
    mov r15, rdi                // ctx
    mov r12, [r15 + C_NODES]
    mov r13d, [r15 + C_NODE_COUNT]
    xor r14d, r14d
.layout_loop:
    cmp r14d, r13d
    jae .layout_done
    mov eax, r14d
    imul rax, NODE_STRIDE
    lea rbx, [r12 + rax]        // node

    // Parent geometry and matrix. Locals:
    // [rsp+0] pw, +4 ph, +8 pAlpha, +12 clipx,+16 clipy,+20 clipw,+24 cliph
    // parent matrix at +? Instead access parent directly or identity branch.
    mov eax, [rbx + N_PARENT]
    test eax, eax
    js .root_parent
    cmp eax, r14d               // parent must have been laid out already
    jae .root_parent
    imul rax, NODE_STRIDE
    lea r11, [r12 + rax]
    movss xmm0, [r11 + N_WIDTH]
    movss [rsp+0], xmm0
    movss xmm0, [r11 + N_HEIGHT]
    movss [rsp+4], xmm0
    movss xmm0, [r11 + N_WORLD_ALPHA]
    movss [rsp+8], xmm0
    movss xmm0, [r11 + N_CLIP_X]
    movss [rsp+12], xmm0
    movss xmm0, [r11 + N_CLIP_Y]
    movss [rsp+16], xmm0
    movss xmm0, [r11 + N_CLIP_W]
    movss [rsp+20], xmm0
    movss xmm0, [r11 + N_CLIP_H]
    movss [rsp+24], xmm0
    mov [rsp+48], r11
    jmp .have_parent
.root_parent:
    movss xmm0, [r15 + C_VIEW_W]
    movss [rsp+0], xmm0
    movss xmm0, [r15 + C_VIEW_H]
    movss [rsp+4], xmm0
    movss xmm0, dword ptr [rip + .LC_ONE]
    movss [rsp+8], xmm0
    xorps xmm0, xmm0
    movss [rsp+12], xmm0
    movss [rsp+16], xmm0
    movss xmm0, [r15 + C_VIEW_W]
    movss [rsp+20], xmm0
    movss xmm0, [r15 + C_VIEW_H]
    movss [rsp+24], xmm0
    xor r11d, r11d              // null => canvas-to-screen parent matrix
    mov [rsp+48], r11
.have_parent:
    // width = pw*(amax-amin) + sizeDelta
    movss xmm0, [rbx + N_AMAX_X]
    subss xmm0, [rbx + N_AMIN_X]
    mulss xmm0, [rsp+0]
    addss xmm0, [rbx + N_SIZE_X]
    maxss xmm0, dword ptr [rip + .LC_ZERO]
    movss [rbx + N_WIDTH], xmm0
    // height
    movss xmm1, [rbx + N_AMAX_Y]
    subss xmm1, [rbx + N_AMIN_Y]
    mulss xmm1, [rsp+4]
    addss xmm1, [rbx + N_SIZE_Y]
    maxss xmm1, dword ptr [rip + .LC_ZERO]
    movss [rbx + N_HEIGHT], xmm1

    // pivot position in parent local coordinates:
    // parentSize * (amin + (amax-amin)*pivot) + anchored
    movss xmm2, [rbx + N_AMAX_X]
    subss xmm2, [rbx + N_AMIN_X]
    mulss xmm2, [rbx + N_PIVOT_X]
    addss xmm2, [rbx + N_AMIN_X]
    mulss xmm2, [rsp+0]
    addss xmm2, [rbx + N_ANCH_X]  // px in parent local
    movss [rsp+28], xmm2

    movss xmm3, [rbx + N_AMAX_Y]
    subss xmm3, [rbx + N_AMIN_Y]
    mulss xmm3, [rbx + N_PIVOT_Y]
    addss xmm3, [rbx + N_AMIN_Y]
    mulss xmm3, [rsp+4]
    addss xmm3, [rbx + N_ANCH_Y]
    movss [rsp+32], xmm3

    // sin/cos(rotation) entirely in ASM via x87 FSINCOS.
    // FSINCOS leaves cos in ST(0), sin in ST(1).
    fld dword ptr [rbx + N_ROT]
    fsincos
    fstp dword ptr [rsp+36]     // cos
    fstp dword ptr [rsp+44]     // sin
    movss xmm0, [rsp+44]
    mov r11, [rsp+48]

    // Build local matrix L = T(pivotPos) * R * S * T(-pivot*size)
    // a=cos*sx, b=-sin*sy, c=sin*sx, d=cos*sy
    movss xmm1, [rsp+36]
    mulss xmm1, [rbx + N_SCALE_X] // a
    movss xmm2, xmm0
    mulss xmm2, dword ptr [rip + .LC_NEG_ONE]
    mulss xmm2, [rbx + N_SCALE_Y] // b
    movss xmm3, xmm0
    mulss xmm3, [rbx + N_SCALE_X] // c
    movss xmm4, [rsp+36]
    mulss xmm4, [rbx + N_SCALE_Y] // d

    // tx = pivotPosX - a*(pivotX*w) - b*(pivotY*h)
    movss xmm5, [rbx + N_PIVOT_X]
    mulss xmm5, [rbx + N_WIDTH]
    movss xmm6, xmm1
    mulss xmm6, xmm5
    movss xmm5, [rbx + N_PIVOT_Y]
    mulss xmm5, [rbx + N_HEIGHT]
    movss xmm7, xmm2
    mulss xmm7, xmm5
    addss xmm6, xmm7
    movss xmm5, [rsp+28]
    subss xmm5, xmm6             // tx

    // ty = pivotPosY - c*(pivotX*w) - d*(pivotY*h)
    movss xmm6, [rbx + N_PIVOT_X]
    mulss xmm6, [rbx + N_WIDTH]
    mulss xmm6, xmm3
    movss xmm7, [rbx + N_PIVOT_Y]
    mulss xmm7, [rbx + N_HEIGHT]
    mulss xmm7, xmm4
    addss xmm6, xmm7
    movss xmm7, [rsp+32]
    subss xmm7, xmm6             // ty
    test r11, r11
    jz .store_local_matrix
    // W = P * L. P=[p00 p01 p02; p10 p11 p12]
    // wm00=p00*a+p01*c
    movss xmm8, [r11 + N_M00]
    mulss xmm8, xmm1
    movss xmm9, [r11 + N_M01]
    mulss xmm9, xmm3
    addss xmm8, xmm9
    movss [rbx + N_M00], xmm8
    // wm01=p00*b+p01*d
    movss xmm8, [r11 + N_M00]
    mulss xmm8, xmm2
    movss xmm9, [r11 + N_M01]
    mulss xmm9, xmm4
    addss xmm8, xmm9
    movss [rbx + N_M01], xmm8
    // wm02=p00*tx+p01*ty+p02
    movss xmm8, [r11 + N_M00]
    mulss xmm8, xmm5
    movss xmm9, [r11 + N_M01]
    mulss xmm9, xmm7
    addss xmm8, xmm9
    addss xmm8, [r11 + N_M02]
    movss [rbx + N_M02], xmm8
    // wm10
    movss xmm8, [r11 + N_M10]
    mulss xmm8, xmm1
    movss xmm9, [r11 + N_M11]
    mulss xmm9, xmm3
    addss xmm8, xmm9
    movss [rbx + N_M10], xmm8
    // wm11
    movss xmm8, [r11 + N_M10]
    mulss xmm8, xmm2
    movss xmm9, [r11 + N_M11]
    mulss xmm9, xmm4
    addss xmm8, xmm9
    movss [rbx + N_M11], xmm8
    // wm12
    movss xmm8, [r11 + N_M10]
    mulss xmm8, xmm5
    movss xmm9, [r11 + N_M11]
    mulss xmm9, xmm7
    addss xmm8, xmm9
    addss xmm8, [r11 + N_M12]
    movss [rbx + N_M12], xmm8
    jmp .alpha_clip
.store_local_matrix:
    // RectTransform authoring and glyph planes are +Y up. Convert exactly
    // once at the root to framebuffer coordinates (top-left, +Y down).
    // Children compose with this reflected matrix; clip/mask AABBs therefore
    // already share the coordinate system used by both render backends.
    mulss xmm3, dword ptr [rip + .LC_NEG_ONE]
    mulss xmm4, dword ptr [rip + .LC_NEG_ONE]
    movss xmm8, [r15 + C_VIEW_H]
    subss xmm8, xmm7
    movaps xmm7, xmm8
    movss [rbx + N_M00], xmm1
    movss [rbx + N_M01], xmm2
    movss [rbx + N_M02], xmm5
    movss [rbx + N_M10], xmm3
    movss [rbx + N_M11], xmm4
    movss [rbx + N_M12], xmm7

.alpha_clip:
    movss xmm0, [rbx + N_ALPHA]
    mulss xmm0, [rsp+8]
    movss [rbx + N_WORLD_ALPHA], xmm0

    // Default inherited clip.
    movss xmm0, [rsp+12]; movss [rbx + N_CLIP_X], xmm0
    movss xmm0, [rsp+16]; movss [rbx + N_CLIP_Y], xmm0
    movss xmm0, [rsp+20]; movss [rbx + N_CLIP_W], xmm0
    movss xmm0, [rsp+24]; movss [rbx + N_CLIP_H], xmm0

    // If node clips children/self, intersect inherited clip with node world AABB.
    mov eax, [rbx + N_FLAGS]
    test eax, 2
    jz .layout_next

    // Compute four corners and AABB. x/y pairs in xmm0/xmm1 -> result xmm2/xmm3
    lea rdi, [rbx + N_M00]
    xorps xmm0, xmm0; xorps xmm1, xmm1
    call .apply_matrix
    movaps xmm8, xmm2 // minx
    movaps xmm9, xmm2 // maxx
    movaps xmm10, xmm3 // miny
    movaps xmm11, xmm3 // maxy

    movss xmm0, [rbx + N_WIDTH]; xorps xmm1, xmm1
    call .apply_matrix
    minss xmm8, xmm2; maxss xmm9, xmm2; minss xmm10, xmm3; maxss xmm11, xmm3
    movss xmm0, [rbx + N_WIDTH]; movss xmm1, [rbx + N_HEIGHT]
    call .apply_matrix
    minss xmm8, xmm2; maxss xmm9, xmm2; minss xmm10, xmm3; maxss xmm11, xmm3
    xorps xmm0, xmm0; movss xmm1, [rbx + N_HEIGHT]
    call .apply_matrix
    minss xmm8, xmm2; maxss xmm9, xmm2; minss xmm10, xmm3; maxss xmm11, xmm3

    // inherited max = origin+size
    movss xmm0, [rsp+12] // clip minx
    movss xmm1, [rsp+16] // clip miny
    movss xmm2, [rsp+20]; addss xmm2, xmm0 // clip maxx
    movss xmm3, [rsp+24]; addss xmm3, xmm1 // clip maxy
    maxss xmm8, xmm0
    maxss xmm10, xmm1
    minss xmm9, xmm2
    minss xmm11, xmm3
    subss xmm9, xmm8
    subss xmm11, xmm10
    maxss xmm9, dword ptr [rip + .LC_ZERO]
    maxss xmm11, dword ptr [rip + .LC_ZERO]
    movss [rbx + N_CLIP_X], xmm8
    movss [rbx + N_CLIP_Y], xmm10
    movss [rbx + N_CLIP_W], xmm9
    movss [rbx + N_CLIP_H], xmm11

.layout_next:
    inc r14d
    jmp .layout_loop
.layout_done:
    add rsp, 56
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret

.type .emit_quad,@function
// Internal emitter.
// r10=node, r15=ctx, r14=draw base
// xmm0=left, xmm1=bottom, xmm2=right, xmm3=top
// xmm4=u0(left), xmm5=v0(top), xmm6=u1(right), xmm7=v1(bottom)
.emit_quad:
    sub rsp, 40
    movss [rsp+0], xmm0
    movss [rsp+4], xmm1
    movss [rsp+8], xmm2
    movss [rsp+12], xmm3
    movss [rsp+16], xmm4
    movss [rsp+20], xmm5
    movss [rsp+24], xmm6
    movss [rsp+28], xmm7

    mov ecx, [r15 + C_DRAW_COUNT]
    cmp ecx, [r15 + C_DRAW_CAP]
    jae .emit_quad_full
    mov [rsp+32], ecx               // preserve draw index across stencil walk
    mov eax, ecx
    imul rax, DRAW_STRIDE
    lea r11, [r14 + rax]

    mov eax, [r10 + N_TYPE]; mov [r11 + D_TYPE], eax
    mov eax, [r10 + N_RESOURCE]; mov [r11 + D_RESOURCE], eax
    mov eax, [r10 + N_RGBA]; mov [r11 + D_RGBA], eax
    mov eax, [r10 + N_FLAGS]; mov [r11 + D_FLAGS], eax
    movss xmm0, [r10 + N_WORLD_ALPHA]; movss [r11 + D_ALPHA], xmm0
    movss xmm0, [rsp+16]; movss [r11 + D_U0], xmm0
    movss xmm0, [rsp+20]; movss [r11 + D_V0], xmm0
    movss xmm0, [rsp+24]; movss [r11 + D_U1], xmm0
    movss xmm0, [rsp+28]; movss [r11 + D_V1], xmm0
    movss xmm0, [r10 + N_CLIP_X]; movss [r11 + D_CLIP_X], xmm0
    movss xmm0, [r10 + N_CLIP_Y]; movss [r11 + D_CLIP_Y], xmm0
    movss xmm0, [r10 + N_CLIP_W]; movss [r11 + D_CLIP_W], xmm0
    movss xmm0, [r10 + N_CLIP_H]; movss [r11 + D_CLIP_H], xmm0
    movss xmm0, [r10 + N_FONT_SIZE]; movss [r11 + D_FONT_SIZE], xmm0
    mov rax, [r10 + N_PAYLOAD]; mov [r11 + D_PAYLOAD], rax
    mov eax, [r10 + N_MATERIAL]; mov [r11 + D_MATERIAL], eax
    lea eax, [ebx + 1]
    mov [r11 + D_SOURCE_NODE], eax
    movss xmm0, [r10 + N_MAT0]; movss [r11 + D_MAT0], xmm0
    movss xmm0, [r10 + N_MAT1]; movss [r11 + D_MAT1], xmm0
    movss xmm0, [r10 + N_MAT2]; movss [r11 + D_MAT2], xmm0
    movss xmm0, [r10 + N_MAT3]; movss [r11 + D_MAT3], xmm0
    mov eax, [r10 + N_TEXT_ALIGN]; mov [r11 + D_TEXT_ALIGN], eax
    mov dword ptr [r11 + D_STENCIL_DEPTH], 0
    movss xmm0, [r10 + N_WIDTH]; movss [r11 + D_LOCAL_W], xmm0
    movss xmm0, [r10 + N_HEIGHT]; movss [r11 + D_LOCAL_H], xmm0
    movss xmm0, [r10 + N_MASK_X]; movss [r11 + D_MASK_X], xmm0
    movss xmm0, [r10 + N_MASK_Y]; movss [r11 + D_MASK_Y], xmm0
    movss xmm0, [r10 + N_MASK_W]; movss [r11 + D_MASK_W], xmm0
    movss xmm0, [r10 + N_MASK_H]; movss [r11 + D_MASK_H], xmm0
    movss xmm0, [r10 + N_MASK_WORLD_RADIUS]; movss [r11 + D_MASK_RADIUS], xmm0
    movss xmm0, [r10 + N_MASK_WORLD_SOFTNESS]; movss [r11 + D_MASK_SOFTNESS], xmm0
    mov eax, [r10 + N_MASK_FLAGS]; mov [r11 + D_MASK_FLAGS], eax

    // Resolve stencil ancestry for this visual. A stencil node clips descendants,
    // not its own visual. Store depth + nearest mask index(+1) in the draw command.
    xor edx, edx                    // stencil depth
    xor ecx, ecx                    // nearest stencil index + 1
    mov eax, [r10 + N_PARENT]
    xor r8d, r8d                    // corrupt-hierarchy guard
.emit_stencil_walk:
    test eax, eax
    js .emit_stencil_done
    cmp eax, [r15 + C_NODE_COUNT]
    jae .emit_stencil_done
    mov r9d, eax                    // preserve parent node index
    imul rax, NODE_STRIDE
    lea rax, [r12 + rax]
    cmp dword ptr [rax + N_MASK_MODE], 2
    jne .emit_stencil_parent
    inc edx
    test ecx, ecx
    jne .emit_stencil_parent
    lea ecx, [r9d + 1]
.emit_stencil_parent:
    mov eax, [rax + N_PARENT]
    inc r8d
    cmp r8d, 64
    jb .emit_stencil_walk
.emit_stencil_done:
    mov [r11 + D_STENCIL_DEPTH], edx
    mov [r11 + D_STENCIL_PARENT], ecx

    // Transform the four local corners.
    lea rdi, [r10 + N_M00]
    movss xmm0, [rsp+0]; movss xmm1, [rsp+4]
    call .apply_matrix
    movss [r11 + D_X0], xmm2; movss [r11 + D_Y0], xmm3

    movss xmm0, [rsp+8]; movss xmm1, [rsp+4]
    call .apply_matrix
    movss [r11 + D_X1], xmm2; movss [r11 + D_Y1], xmm3

    movss xmm0, [rsp+8]; movss xmm1, [rsp+12]
    call .apply_matrix
    movss [r11 + D_X2], xmm2; movss [r11 + D_Y2], xmm3

    movss xmm0, [rsp+0]; movss xmm1, [rsp+12]
    call .apply_matrix
    movss [r11 + D_X3], xmm2; movss [r11 + D_Y3], xmm3

    mov ecx, [rsp+32]
    inc ecx
    mov [r15 + C_DRAW_COUNT], ecx
.emit_quad_full:
    add rsp, 40
    ret

.macro EMIT_SLICE XL, YB, XR, YT, UL, VT, UR, VB
    movss xmm0, [rsp+\XL]
    movss xmm1, [rsp+\YB]
    movss xmm2, [rsp+\XR]
    movss xmm3, [rsp+\YT]
    movss xmm4, [rsp+\UL]
    movss xmm5, [rsp+\VT]
    movss xmm6, [rsp+\UR]
    movss xmm7, [rsp+\VB]
    call .emit_quad
.endm

.global asm_ui_resolve_soft_masks
.type asm_ui_resolve_soft_masks,@function
asm_ui_resolve_soft_masks:
    // Resolve the nearest inherited soft mask after layout. Root has no mask.
    // A mask affects descendants, not the mask node's own visual.
    push rbx
    push r12
    push r13
    push r14
    mov r12, [rdi + C_NODES]
    mov r13d, [rdi + C_NODE_COUNT]
    xor r14d, r14d
.mask_loop:
    cmp r14d, r13d
    jae .mask_done
    mov eax, r14d
    imul rax, NODE_STRIDE
    lea rbx, [r12 + rax]
    mov dword ptr [rbx + N_MASK_FLAGS], 0
    xorps xmm0, xmm0
    movss [rbx + N_MASK_X], xmm0
    movss [rbx + N_MASK_Y], xmm0
    movss [rbx + N_MASK_W], xmm0
    movss [rbx + N_MASK_H], xmm0
    movss [rbx + N_MASK_WORLD_RADIUS], xmm0
    movss [rbx + N_MASK_WORLD_SOFTNESS], xmm0

    mov eax, [rbx + N_PARENT]
    test eax, eax
    js .mask_next
    cmp eax, r14d
    jae .mask_next
    imul rax, NODE_STRIDE
    lea r11, [r12 + rax]
    cmp dword ptr [r11 + N_MASK_MODE], 1
    je .mask_from_parent_rect
    test dword ptr [r11 + N_MASK_FLAGS], 1
    jz .mask_next
    // Inherit an ancestor soft mask.
    movss xmm0, [r11 + N_MASK_X]; movss [rbx + N_MASK_X], xmm0
    movss xmm0, [r11 + N_MASK_Y]; movss [rbx + N_MASK_Y], xmm0
    movss xmm0, [r11 + N_MASK_W]; movss [rbx + N_MASK_W], xmm0
    movss xmm0, [r11 + N_MASK_H]; movss [rbx + N_MASK_H], xmm0
    movss xmm0, [r11 + N_MASK_WORLD_RADIUS]; movss [rbx + N_MASK_WORLD_RADIUS], xmm0
    movss xmm0, [r11 + N_MASK_WORLD_SOFTNESS]; movss [rbx + N_MASK_WORLD_SOFTNESS], xmm0
    mov dword ptr [rbx + N_MASK_FLAGS], 1
    jmp .mask_next
.mask_from_parent_rect:
    // Parent's clip rectangle is the conservative screen-space mask bounds.
    movss xmm0, [r11 + N_CLIP_X]; movss [rbx + N_MASK_X], xmm0
    movss xmm0, [r11 + N_CLIP_Y]; movss [rbx + N_MASK_Y], xmm0
    movss xmm0, [r11 + N_CLIP_W]; movss [rbx + N_MASK_W], xmm0
    movss xmm0, [r11 + N_CLIP_H]; movss [rbx + N_MASK_H], xmm0
    movss xmm0, [r11 + N_MASK_RADIUS]; movss [rbx + N_MASK_WORLD_RADIUS], xmm0
    movss xmm0, [r11 + N_MASK_SOFTNESS]; movss [rbx + N_MASK_WORLD_SOFTNESS], xmm0
    mov dword ptr [rbx + N_MASK_FLAGS], 1
.mask_next:
    inc r14d
    jmp .mask_loop
.mask_done:
    pop r14
    pop r13
    pop r12
    pop rbx
    ret

.global asm_ui_build_drawlist
.type asm_ui_build_drawlist,@function
asm_ui_build_drawlist:
    push rbx
    push r12
    push r13
    push r14
    push r15
    sub rsp, 80
    mov r15, rdi
    mov rdi, r15
    call asm_ui_resolve_soft_masks
    mov r12, [r15 + C_NODES]
    mov r13d, [r15 + C_NODE_COUNT]
    mov r14, [r15 + C_DRAWS]
    xor ebx, ebx
.draw_loop:
    cmp ebx, r13d
    jae .draw_done
    mov eax, ebx
    imul rax, NODE_STRIDE
    lea r10, [r12 + rax]
    mov eax, [r10 + N_FLAGS]
    test eax, 1
    jz .draw_next

    mov eax, [r10 + N_TYPE]
    cmp eax, 2
    je .emit_single
    cmp eax, 1
    jne .draw_next
    mov eax, [r10 + N_IMAGE_MODE]
    cmp eax, 1
    je .emit_sliced
    cmp eax, 2
    je .emit_filled_h
    cmp eax, 3
    je .emit_filled_v

.emit_single:
    // Legacy zero-initialized nodes use a full texture. Explicit atlas UVs
    // may have zero span: a nine-slice center can stretch a single texel.
    xorps xmm0, xmm0
    movss [rsp+0], xmm0             // left
    movss [rsp+4], xmm0             // bottom
    movss xmm0, [r10 + N_WIDTH]; movss [rsp+8], xmm0
    movss xmm0, [r10 + N_HEIGHT]; movss [rsp+12], xmm0

    movss xmm4, [r10 + N_UV_X0]
    movss xmm5, [r10 + N_UV_Y0]
    movss xmm6, [r10 + N_UV_X1]
    movss xmm7, [r10 + N_UV_Y1]
    test dword ptr [r10 + N_FLAGS], 8 // ASMUI_UV_EXPLICIT
    jnz .single_uv_ok
    movaps xmm0, xmm6; subss xmm0, xmm4
    movaps xmm1, xmm7; subss xmm1, xmm5
    andps xmm0, xmmword ptr [rip + .LC_ABS_MASK]
    andps xmm1, xmmword ptr [rip + .LC_ABS_MASK]
    ucomiss xmm0, dword ptr [rip + .LC_EPS]
    ja .single_uv_ok
    ucomiss xmm1, dword ptr [rip + .LC_EPS]
    ja .single_uv_ok
    xorps xmm4, xmm4
    xorps xmm5, xmm5
    movss xmm6, dword ptr [rip + .LC_ONE]
    movss xmm7, dword ptr [rip + .LC_ONE]
.single_uv_ok:
    movss [rsp+16], xmm4
    movss [rsp+20], xmm5
    movss [rsp+24], xmm6
    movss [rsp+28], xmm7
    EMIT_SLICE 0,4,8,12,16,20,24,28
    jmp .draw_next

.emit_filled_h:
    // Left-to-right fill. Geometry and atlas U are cropped together.
    xorps xmm0, xmm0
    movss [rsp+0], xmm0
    movss [rsp+4], xmm0
    movss xmm1, [r10 + N_FILL]
    maxss xmm1, dword ptr [rip + .LC_ZERO]
    minss xmm1, dword ptr [rip + .LC_ONE]
    movss xmm2, [r10 + N_WIDTH]
    mulss xmm2, xmm1
    movss [rsp+8], xmm2
    movss xmm2, [r10 + N_HEIGHT]
    movss [rsp+12], xmm2
    movss xmm4, [r10 + N_UV_X0]
    movss xmm5, [r10 + N_UV_Y0]
    movss xmm6, [r10 + N_UV_X1]
    subss xmm6, xmm4
    mulss xmm6, xmm1
    addss xmm6, xmm4
    movss xmm7, [r10 + N_UV_Y1]
    movss [rsp+16], xmm4
    movss [rsp+20], xmm5
    movss [rsp+24], xmm6
    movss [rsp+28], xmm7
    EMIT_SLICE 0,4,8,12,16,20,24,28
    jmp .draw_next

.emit_filled_v:
    // Bottom-to-top fill. v1 is the authored bottom, v0 the authored top.
    xorps xmm0, xmm0
    movss [rsp+0], xmm0
    movss [rsp+4], xmm0
    movss xmm1, [r10 + N_FILL]
    maxss xmm1, dword ptr [rip + .LC_ZERO]
    minss xmm1, dword ptr [rip + .LC_ONE]
    movss xmm2, [r10 + N_WIDTH]
    movss [rsp+8], xmm2
    movss xmm2, [r10 + N_HEIGHT]
    mulss xmm2, xmm1
    movss [rsp+12], xmm2
    movss xmm4, [r10 + N_UV_X0]
    movss xmm6, [r10 + N_UV_X1]
    movss xmm7, [r10 + N_UV_Y1]
    movss xmm5, xmm7
    subss xmm5, [r10 + N_UV_Y0]
    mulss xmm5, xmm1
    movss xmm3, xmm7
    subss xmm3, xmm5
    movss xmm5, xmm3
    movss [rsp+16], xmm4
    movss [rsp+20], xmm5
    movss [rsp+24], xmm6
    movss [rsp+28], xmm7
    EMIT_SLICE 0,4,8,12,16,20,24,28
    jmp .draw_next

.emit_sliced:
    // Local X grid: 0, min(borderL,w/2), max(prev,w-min(borderR,w/2)), w
    xorps xmm0, xmm0
    movss [rsp+0], xmm0             // x0
    movss [rsp+16], xmm0            // y0
    movss xmm1, [r10 + N_WIDTH]
    movss [rsp+12], xmm1            // x3
    movss xmm2, [r10 + N_HEIGHT]
    movss [rsp+28], xmm2            // y3

    movss xmm3, xmm1; mulss xmm3, dword ptr [rip + .LC_HALF]
    movss xmm4, [r10 + N_BORDER_L]
    maxss xmm4, dword ptr [rip + .LC_ZERO]
    minss xmm4, xmm3
    movss [rsp+4], xmm4             // x1
    movss xmm5, [r10 + N_BORDER_R]
    maxss xmm5, dword ptr [rip + .LC_ZERO]
    minss xmm5, xmm3
    subss xmm1, xmm5
    maxss xmm1, xmm4
    movss [rsp+8], xmm1             // x2

    movss xmm3, xmm2; mulss xmm3, dword ptr [rip + .LC_HALF]
    movss xmm4, [r10 + N_BORDER_B]
    maxss xmm4, dword ptr [rip + .LC_ZERO]
    minss xmm4, xmm3
    movss [rsp+20], xmm4            // y1
    movss xmm5, [r10 + N_BORDER_T]
    maxss xmm5, dword ptr [rip + .LC_ZERO]
    minss xmm5, xmm3
    subss xmm2, xmm5
    maxss xmm2, xmm4
    movss [rsp+24], xmm2            // y2

    // U grid at rsp+32..44. V grid stored in bottom->top order at +48..60.
    movss xmm0, [r10 + N_UV_X0]
    movss xmm1, [r10 + N_UV_X1]
    movaps xmm2, xmm1; subss xmm2, xmm0
    movss [rsp+32], xmm0            // u0
    movss [rsp+44], xmm1            // u3

    movss xmm3, [r10 + N_SOURCE_W]
    ucomiss xmm3, dword ptr [rip + .LC_EPS]
    ja .source_w_ok
    movss xmm3, dword ptr [rip + .LC_ONE]
.source_w_ok:
    movss xmm4, [r10 + N_BORDER_L]; divss xmm4, xmm3
    maxss xmm4, dword ptr [rip + .LC_ZERO]
    minss xmm4, dword ptr [rip + .LC_HALF]
    mulss xmm4, xmm2; addss xmm4, xmm0
    movss [rsp+36], xmm4            // u1
    movss xmm4, [r10 + N_BORDER_R]; divss xmm4, xmm3
    maxss xmm4, dword ptr [rip + .LC_ZERO]
    minss xmm4, dword ptr [rip + .LC_HALF]
    mulss xmm4, xmm2
    movaps xmm5, xmm1; subss xmm5, xmm4
    movss [rsp+40], xmm5            // u2

    movss xmm0, [r10 + N_UV_Y0]     // top
    movss xmm1, [r10 + N_UV_Y1]     // bottom
    movaps xmm2, xmm1; subss xmm2, xmm0
    movss [rsp+48], xmm1            // vb0 = bottom
    movss [rsp+60], xmm0            // vb3 = top

    movss xmm3, [r10 + N_SOURCE_H]
    ucomiss xmm3, dword ptr [rip + .LC_EPS]
    ja .source_h_ok
    movss xmm3, dword ptr [rip + .LC_ONE]
.source_h_ok:
    movss xmm4, [r10 + N_BORDER_B]; divss xmm4, xmm3
    maxss xmm4, dword ptr [rip + .LC_ZERO]
    minss xmm4, dword ptr [rip + .LC_HALF]
    mulss xmm4, xmm2
    movaps xmm5, xmm1; subss xmm5, xmm4
    movss [rsp+52], xmm5            // v at y1
    movss xmm4, [r10 + N_BORDER_T]; divss xmm4, xmm3
    maxss xmm4, dword ptr [rip + .LC_ZERO]
    minss xmm4, dword ptr [rip + .LC_HALF]
    mulss xmm4, xmm2; addss xmm4, xmm0
    movss [rsp+56], xmm4            // v at y2

    // 3x3 quads. For each local y interval, D_V0 is top and D_V1 is bottom.
    EMIT_SLICE 0,16,4,20,32,52,36,48
    EMIT_SLICE 4,16,8,20,36,52,40,48
    EMIT_SLICE 8,16,12,20,40,52,44,48
    EMIT_SLICE 0,20,4,24,32,56,36,52
    EMIT_SLICE 4,20,8,24,36,56,40,52
    EMIT_SLICE 8,20,12,24,40,56,44,52
    EMIT_SLICE 0,24,4,28,32,60,36,56
    EMIT_SLICE 4,24,8,28,36,60,40,56
    EMIT_SLICE 8,24,12,28,40,60,44,56

.draw_next:
    inc ebx
    jmp .draw_loop
.draw_done:
    add rsp, 80
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    ret

.section .rodata
.align 16
.LC_ZERO: .float 0.0
.LC_HALF: .float 0.5
.LC_ONE:  .float 1.0
.LC_TWO:  .float 2.0
.LC_FOUR: .float 4.0
.LC_NEG_ONE: .float -1.0
.LC_EPS: .float 0.000001
.align 16
.LC_ABS_MASK: .long 0x7fffffff,0x7fffffff,0x7fffffff,0x7fffffff
.text
// -----------------------------------------------------------------------------
// v0.3+ painter-order batching + GPU vertex packing
// -----------------------------------------------------------------------------
.equ BATCH_STRIDE, 72
.equ B_FIRST, 0
.equ B_COUNT, 4
.equ B_TYPE, 8
.equ B_RESOURCE, 12
.equ B_MATERIAL, 16
.equ B_MASK_FLAGS, 20
.equ B_CLIP_X, 24
.equ B_CLIP_Y, 28
.equ B_CLIP_W, 32
.equ B_CLIP_H, 36
.equ B_MASK_X, 40
.equ B_MASK_Y, 44
.equ B_MASK_W, 48
.equ B_MASK_H, 52
.equ B_MASK_RADIUS, 56
.equ B_MASK_SOFTNESS, 60
.equ B_STENCIL_DEPTH, 64
.equ B_STENCIL_PARENT, 68

.equ GPUV_STRIDE, 48
.equ V_X, 0
.equ V_Y, 4
.equ V_U, 8
.equ V_V, 12
.equ V_LX, 16
.equ V_LY, 20
.equ V_RGBA, 24
.equ V_ALPHA, 28
.equ V_MAT0, 32
.equ V_MAT1, 36
.equ V_MAT2, 40
.equ V_MAT3, 44

.global asm_ui_build_batches
.type asm_ui_build_batches,@function
asm_ui_build_batches:
    // rdi=ctx, rsi=batch array, edx=capacity; eax=batch count
    test edx, edx
    jz .batch_zero
    mov r8, [rdi + C_DRAWS]
    mov r9d, [rdi + C_DRAW_COUNT]
    test r9d, r9d
    jz .batch_zero
    xor r10d, r10d                 // draw index
    xor r11d, r11d                 // batch count

.batch_loop:
    cmp r10d, r9d
    jae .batch_done
    mov eax, r10d
    imul rax, DRAW_STRIDE
    lea rcx, [r8 + rax]            // current draw

    test r11d, r11d
    jz .batch_new
    mov eax, r11d
    dec eax
    imul rax, BATCH_STRIDE
    lea rax, [rsi + rax]           // previous batch

    mov edi, [rcx + D_TYPE]
    cmp edi, [rax + B_TYPE]
    jne .batch_new
    mov edi, [rcx + D_RESOURCE]
    cmp edi, [rax + B_RESOURCE]
    jne .batch_new
    mov edi, [rcx + D_MATERIAL]
    cmp edi, [rax + B_MATERIAL]
    jne .batch_new
    mov edi, [rcx + D_MASK_FLAGS]
    cmp edi, [rax + B_MASK_FLAGS]
    jne .batch_new
    // Bit-exact clip/mask equality: layout emits deterministic float values.
    mov edi, [rcx + D_CLIP_X]
    cmp edi, [rax + B_CLIP_X]
    jne .batch_new
    mov edi, [rcx + D_CLIP_Y]
    cmp edi, [rax + B_CLIP_Y]
    jne .batch_new
    mov edi, [rcx + D_CLIP_W]
    cmp edi, [rax + B_CLIP_W]
    jne .batch_new
    mov edi, [rcx + D_CLIP_H]
    cmp edi, [rax + B_CLIP_H]
    jne .batch_new
    mov edi, [rcx + D_MASK_X]
    cmp edi, [rax + B_MASK_X]
    jne .batch_new
    mov edi, [rcx + D_MASK_Y]
    cmp edi, [rax + B_MASK_Y]
    jne .batch_new
    mov edi, [rcx + D_MASK_W]
    cmp edi, [rax + B_MASK_W]
    jne .batch_new
    mov edi, [rcx + D_MASK_H]
    cmp edi, [rax + B_MASK_H]
    jne .batch_new
    mov edi, [rcx + D_MASK_RADIUS]
    cmp edi, [rax + B_MASK_RADIUS]
    jne .batch_new
    mov edi, [rcx + D_MASK_SOFTNESS]
    cmp edi, [rax + B_MASK_SOFTNESS]
    jne .batch_new
    mov edi, [rcx + D_STENCIL_DEPTH]
    cmp edi, [rax + B_STENCIL_DEPTH]
    jne .batch_new
    mov edi, [rcx + D_STENCIL_PARENT]
    cmp edi, [rax + B_STENCIL_PARENT]
    jne .batch_new

    inc dword ptr [rax + B_COUNT]
    inc r10d
    jmp .batch_loop

.batch_new:
    cmp r11d, edx
    jae .batch_done
    mov eax, r11d
    imul rax, BATCH_STRIDE
    lea rax, [rsi + rax]
    mov [rax + B_FIRST], r10d
    mov dword ptr [rax + B_COUNT], 1
    mov edi, [rcx + D_TYPE]
    mov [rax + B_TYPE], edi
    mov edi, [rcx + D_RESOURCE]
    mov [rax + B_RESOURCE], edi
    mov edi, [rcx + D_MATERIAL]
    mov [rax + B_MATERIAL], edi
    mov edi, [rcx + D_MASK_FLAGS]
    mov [rax + B_MASK_FLAGS], edi
    mov edi, [rcx + D_CLIP_X]
    mov [rax + B_CLIP_X], edi
    mov edi, [rcx + D_CLIP_Y]
    mov [rax + B_CLIP_Y], edi
    mov edi, [rcx + D_CLIP_W]
    mov [rax + B_CLIP_W], edi
    mov edi, [rcx + D_CLIP_H]
    mov [rax + B_CLIP_H], edi
    mov edi, [rcx + D_MASK_X]; mov [rax + B_MASK_X], edi
    mov edi, [rcx + D_MASK_Y]; mov [rax + B_MASK_Y], edi
    mov edi, [rcx + D_MASK_W]; mov [rax + B_MASK_W], edi
    mov edi, [rcx + D_MASK_H]; mov [rax + B_MASK_H], edi
    mov edi, [rcx + D_MASK_RADIUS]; mov [rax + B_MASK_RADIUS], edi
    mov edi, [rcx + D_MASK_SOFTNESS]; mov [rax + B_MASK_SOFTNESS], edi
    mov edi, [rcx + D_STENCIL_DEPTH]; mov [rax + B_STENCIL_DEPTH], edi
    mov edi, [rcx + D_STENCIL_PARENT]; mov [rax + B_STENCIL_PARENT], edi
    inc r11d
    inc r10d
    jmp .batch_loop

.batch_done:
    mov eax, r11d
    ret
.batch_zero:
    xor eax, eax
    ret

.macro GPU_VERTEX DST, XOFF, YOFF, UOFF, VOFF, LX, LY
    movss xmm0, [r10 + \XOFF]
    movss [r11 + \DST + V_X], xmm0
    movss xmm0, [r10 + \YOFF]
    movss [r11 + \DST + V_Y], xmm0
    movss xmm0, [r10 + \UOFF]
    movss [r11 + \DST + V_U], xmm0
    movss xmm0, [r10 + \VOFF]
    movss [r11 + \DST + V_V], xmm0
    movss xmm0, dword ptr [rip + \LX]
    movss [r11 + \DST + V_LX], xmm0
    movss xmm0, dword ptr [rip + \LY]
    movss [r11 + \DST + V_LY], xmm0
    mov eax, [r10 + D_RGBA]
    mov [r11 + \DST + V_RGBA], eax
    movss xmm0, [r10 + D_ALPHA]
    movss [r11 + \DST + V_ALPHA], xmm0
    movss xmm0, [r10 + D_MAT0]
    movss [r11 + \DST + V_MAT0], xmm0
    movss xmm0, [r10 + D_MAT1]
    movss [r11 + \DST + V_MAT1], xmm0
    movss xmm0, [r10 + D_MAT2]
    movss [r11 + \DST + V_MAT2], xmm0
    movss xmm0, [r10 + D_MAT3]
    movss [r11 + \DST + V_MAT3], xmm0
.endm

.global asm_ui_build_gpu_vertices
.type asm_ui_build_gpu_vertices,@function
asm_ui_build_gpu_vertices:
    // rdi=ctx, rsi=vertex array, edx=vertex capacity; eax=written vertices
    mov eax, edx
    shr eax, 2                      // capacity in quads
    mov ecx, [rdi + C_DRAW_COUNT]
    cmp ecx, eax
    cmova ecx, eax                  // draws to emit
    test ecx, ecx
    jz .gpuv_zero
    mov r8, [rdi + C_DRAWS]
    xor r9d, r9d
.gpuv_loop:
    cmp r9d, ecx
    jae .gpuv_done
    mov eax, r9d
    imul rax, DRAW_STRIDE
    lea r10, [r8 + rax]
    mov eax, r9d
    imul rax, 4 * GPUV_STRIDE
    lea r11, [rsi + rax]

    // Draw command corners are BL, BR, TR, TL. v0 is top; v1 is bottom.
    GPU_VERTEX 0,                 D_X0,D_Y0,D_U0,D_V1,.LC_ZERO,.LC_ZERO
    GPU_VERTEX GPUV_STRIDE,       D_X1,D_Y1,D_U1,D_V1,.LC_ONE,.LC_ZERO
    GPU_VERTEX 2*GPUV_STRIDE,     D_X2,D_Y2,D_U1,D_V0,.LC_ONE,.LC_ONE
    GPU_VERTEX 3*GPUV_STRIDE,     D_X3,D_Y3,D_U0,D_V0,.LC_ZERO,.LC_ONE

    // Projected planar quads carry one homogeneous W per corner. Keep the
    // established 48-byte GPU vertex ABI; the shader reads W from material3.
    cmp dword ptr [r10 + D_MATERIAL], 0x10c
    jne .gpuv_no_projective
    mov eax, [r10 + D_MAT0]
    mov [r11 + V_MAT3], eax
    mov eax, [r10 + D_MAT1]
    mov [r11 + GPUV_STRIDE + V_MAT3], eax
    mov eax, [r10 + D_MAT2]
    mov [r11 + 2*GPUV_STRIDE + V_MAT3], eax
    mov eax, [r10 + D_MAT3]
    mov [r11 + 3*GPUV_STRIDE + V_MAT3], eax
.gpuv_no_projective:

    // Color interpolation happens in the GPU; ASM resolves authoring flags
    // into real vertex colors without changing the node/draw/vertex ABI.
    test dword ptr [r10 + D_FLAGS], 4
    jz .gpuv_no_gradient
    mov eax, [r10 + D_MAT3]
    mov [r11 + 2*GPUV_STRIDE + V_RGBA], eax
    mov [r11 + 3*GPUV_STRIDE + V_RGBA], eax
.gpuv_no_gradient:

    inc r9d
    jmp .gpuv_loop
.gpuv_done:
    mov eax, ecx
    shl eax, 2
    ret
.gpuv_zero:
    xor eax, eax
    ret


// -----------------------------------------------------------------------------
// v0.5 node-quad packing. One quad is emitted for every node at the exact node
// index; Vulkan uses stencil-mask node indices as stable geometry addresses.
// -----------------------------------------------------------------------------
.macro NODE_MASK_VERTEX DST, XSEL, YSEL, LX, LY
    // x = m00*x + m01*y + m02
    .if \XSEL == 0
        xorps xmm0, xmm0
    .else
        movss xmm0, [r10 + N_WIDTH]
    .endif
    .if \YSEL == 0
        xorps xmm1, xmm1
    .else
        movss xmm1, [r10 + N_HEIGHT]
    .endif
    movss xmm2, [r10 + N_M00]
    mulss xmm2, xmm0
    movss xmm3, [r10 + N_M01]
    mulss xmm3, xmm1
    addss xmm2, xmm3
    addss xmm2, [r10 + N_M02]
    movss [r11 + \DST + V_X], xmm2
    movss xmm2, [r10 + N_M10]
    mulss xmm2, xmm0
    movss xmm3, [r10 + N_M11]
    mulss xmm3, xmm1
    addss xmm2, xmm3
    addss xmm2, [r10 + N_M12]
    movss [r11 + \DST + V_Y], xmm2
    movss xmm2, dword ptr [rip + \LX]
    movss [r11 + \DST + V_U], xmm2
    movss [r11 + \DST + V_LX], xmm2
    movss xmm2, dword ptr [rip + \LY]
    movss [r11 + \DST + V_V], xmm2
    movss [r11 + \DST + V_LY], xmm2
    mov dword ptr [r11 + \DST + V_RGBA], 0xffffffff
    movss xmm2, dword ptr [rip + .LC_ONE]
    movss [r11 + \DST + V_ALPHA], xmm2
    xorps xmm2, xmm2
    movss [r11 + \DST + V_MAT0], xmm2
    movss [r11 + \DST + V_MAT1], xmm2
    movss [r11 + \DST + V_MAT2], xmm2
    movss [r11 + \DST + V_MAT3], xmm2
.endm

.global asm_ui_build_node_vertices
.type asm_ui_build_node_vertices,@function
asm_ui_build_node_vertices:
    // rdi=ctx, rsi=vertex array, edx=vertex capacity; eax=written vertices
    mov eax, edx
    shr eax, 2
    mov ecx, [rdi + C_NODE_COUNT]
    cmp ecx, eax
    cmova ecx, eax
    test ecx, ecx
    jz .nodev_zero
    mov r8, [rdi + C_NODES]
    xor r9d, r9d
.nodev_loop:
    cmp r9d, ecx
    jae .nodev_done
    mov eax, r9d
    imul rax, NODE_STRIDE
    lea r10, [r8 + rax]
    mov eax, r9d
    imul rax, 4 * GPUV_STRIDE
    lea r11, [rsi + rax]

    NODE_MASK_VERTEX 0,                 0,0,.LC_ZERO,.LC_ZERO
    NODE_MASK_VERTEX GPUV_STRIDE,       1,0,.LC_ONE,.LC_ZERO
    NODE_MASK_VERTEX 2*GPUV_STRIDE,     1,1,.LC_ONE,.LC_ONE
    NODE_MASK_VERTEX 3*GPUV_STRIDE,     0,1,.LC_ZERO,.LC_ONE

    inc r9d
    jmp .nodev_loop
.nodev_done:
    mov eax, ecx
    shl eax, 2
    ret
.nodev_zero:
    xor eax, eax
    ret


// -----------------------------------------------------------------------------
// v0.4 shaped-text expansion. HarfBuzz/MSDF atlas lookup stays outside the core;
// final glyph geometry transform and DrawCmd emission are ASM.
// -----------------------------------------------------------------------------
.equ GLYPH_STRIDE, 40
.equ G_X0, 0
.equ G_Y0, 4
.equ G_X1, 8
.equ G_Y1, 12
.equ G_U0, 16
.equ G_V0, 20
.equ G_U1, 24
.equ G_V1, 28
.equ G_RESOURCE, 32

.global asm_ui_expand_text_draw
.type asm_ui_expand_text_draw,@function
asm_ui_expand_text_draw:
    // rdi=text draw, rsi=glyphs, edx=glyph_count, rcx=out, r8d=capacity
    push rbx
    push r12
    push r13
    push r14
    push r15
    sub rsp, 32
    mov r12, rdi
    mov r13, rsi
    mov r14d, edx
    mov r15, rcx
    mov ebx, r8d
    cmp r14d, ebx
    cmova r14d, ebx
    test r14d, r14d
    jz .text_expand_zero

    // Degenerate text box cannot define a local->world basis.
    movss xmm0, [r12 + D_LOCAL_W]
    ucomiss xmm0, dword ptr [rip + .LC_EPS]
    jbe .text_expand_zero
    movss xmm1, [r12 + D_LOCAL_H]
    ucomiss xmm1, dword ptr [rip + .LC_EPS]
    jbe .text_expand_zero

    // bx = (p1-p0)/local_w; by=(p3-p0)/local_h
    movss xmm2, [r12 + D_X1]
    subss xmm2, [r12 + D_X0]
    divss xmm2, xmm0
    movss [rsp+0], xmm2            // bx.x
    movss xmm2, [r12 + D_Y1]
    subss xmm2, [r12 + D_Y0]
    divss xmm2, xmm0
    movss [rsp+4], xmm2            // bx.y
    movss xmm2, [r12 + D_X3]
    subss xmm2, [r12 + D_X0]
    divss xmm2, xmm1
    movss [rsp+8], xmm2            // by.x
    movss xmm2, [r12 + D_Y3]
    subss xmm2, [r12 + D_Y0]
    divss xmm2, xmm1
    movss [rsp+12], xmm2           // by.y

    xor ebx, ebx
.text_expand_loop:
    cmp ebx, r14d
    jae .text_expand_done
    mov eax, ebx
    imul rax, GLYPH_STRIDE
    lea r9, [r13 + rax]            // glyph
    mov eax, ebx
    imul rax, DRAW_STRIDE
    lea r10, [r15 + rax]           // output draw

    // Start from all source state (color/alpha/clip/material params/mask).
    mov rsi, r12
    mov rdi, r10
    mov ecx, 21                    // 168 bytes
    rep movsq

    mov dword ptr [r10 + D_TYPE], 3
    mov eax, [r9 + G_RESOURCE]
    mov [r10 + D_RESOURCE], eax
    mov qword ptr [r10 + D_PAYLOAD], 0
    mov dword ptr [r10 + D_MATERIAL], 0x106
    // Preserve D_SOURCE_NODE copied from the source text draw.
    mov dword ptr [r10 + D_TEXT_ALIGN], 0
    movss xmm0, [r9 + G_U0]; movss [r10 + D_U0], xmm0
    movss xmm0, [r9 + G_V0]; movss [r10 + D_V0], xmm0
    movss xmm0, [r9 + G_U1]; movss [r10 + D_U1], xmm0
    movss xmm0, [r9 + G_V1]; movss [r10 + D_V1], xmm0

    // Macro-like inline transform for four corners.
    // p0 = base + bx*x0 + by*y0
    movss xmm0, [r9 + G_X0]
    movss xmm1, [r9 + G_Y0]
    movss xmm2, [rsp+0]; mulss xmm2, xmm0
    movss xmm3, [rsp+8]; mulss xmm3, xmm1
    addss xmm2, xmm3; addss xmm2, [r12 + D_X0]
    movss [r10 + D_X0], xmm2
    movss xmm2, [rsp+4]; mulss xmm2, xmm0
    movss xmm3, [rsp+12]; mulss xmm3, xmm1
    addss xmm2, xmm3; addss xmm2, [r12 + D_Y0]
    movss [r10 + D_Y0], xmm2

    // p1 = x1,y0
    movss xmm0, [r9 + G_X1]
    movss xmm1, [r9 + G_Y0]
    movss xmm2, [rsp+0]; mulss xmm2, xmm0
    movss xmm3, [rsp+8]; mulss xmm3, xmm1
    addss xmm2, xmm3; addss xmm2, [r12 + D_X0]
    movss [r10 + D_X1], xmm2
    movss xmm2, [rsp+4]; mulss xmm2, xmm0
    movss xmm3, [rsp+12]; mulss xmm3, xmm1
    addss xmm2, xmm3; addss xmm2, [r12 + D_Y0]
    movss [r10 + D_Y1], xmm2

    // p2 = x1,y1
    movss xmm0, [r9 + G_X1]
    movss xmm1, [r9 + G_Y1]
    movss xmm2, [rsp+0]; mulss xmm2, xmm0
    movss xmm3, [rsp+8]; mulss xmm3, xmm1
    addss xmm2, xmm3; addss xmm2, [r12 + D_X0]
    movss [r10 + D_X2], xmm2
    movss xmm2, [rsp+4]; mulss xmm2, xmm0
    movss xmm3, [rsp+12]; mulss xmm3, xmm1
    addss xmm2, xmm3; addss xmm2, [r12 + D_Y0]
    movss [r10 + D_Y2], xmm2

    // p3 = x0,y1
    movss xmm0, [r9 + G_X0]
    movss xmm1, [r9 + G_Y1]
    movss xmm2, [rsp+0]; mulss xmm2, xmm0
    movss xmm3, [rsp+8]; mulss xmm3, xmm1
    addss xmm2, xmm3; addss xmm2, [r12 + D_X0]
    movss [r10 + D_X3], xmm2
    movss xmm2, [rsp+4]; mulss xmm2, xmm0
    movss xmm3, [rsp+12]; mulss xmm3, xmm1
    addss xmm2, xmm3; addss xmm2, [r12 + D_Y0]
    movss [r10 + D_Y3], xmm2

    inc ebx
    jmp .text_expand_loop
.text_expand_done:
    mov eax, r14d
    add rsp, 32
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    ret
.text_expand_zero:
    xor eax, eax
    add rsp, 32
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    ret

.section .note.GNU-stack,"",@progbits
