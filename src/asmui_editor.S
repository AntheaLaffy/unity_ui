.intel_syntax noprefix
.text
.global asm_ui_project_points
.type asm_ui_project_points,@function
asm_ui_project_points:
    // rdi xyz, esi count, rdx matrix, rcx viewport, r8 xy
    test esi, esi
    jz .project_done
    movss xmm8, [rcx+8]
    mulss xmm8, [rip+.half]
    movss xmm9, [rcx+12]
    mulss xmm9, [rip+.half]
    movss xmm10, [rcx]
    addss xmm10, xmm8
    movss xmm11, [rcx+4]
    addss xmm11, xmm9
.project_loop:
    movss xmm0, [rdi]
    shufps xmm0, xmm0, 0
    movss xmm1, [rdi+4]
    shufps xmm1, xmm1, 0
    movss xmm2, [rdi+8]
    shufps xmm2, xmm2, 0
    // Dot each row. SSE2 only; no host compiler-generated geometry loop.
    movups xmm3, [rdx]
    movups xmm4, [rdx+16]
    movups xmm5, [rdx+48]
    movaps xmm6, xmm3
    mulss xmm3, xmm0
    shufps xmm6, xmm6, 0x55
    mulss xmm6, xmm1
    addss xmm3, xmm6
    movups xmm6, [rdx]
    shufps xmm6, xmm6, 0xaa
    mulss xmm6, xmm2
    addss xmm3, xmm6
    addss xmm3, [rdx+12]
    movaps xmm6, xmm4
    mulss xmm4, xmm0
    shufps xmm6, xmm6, 0x55
    mulss xmm6, xmm1
    addss xmm4, xmm6
    movups xmm6, [rdx+16]
    shufps xmm6, xmm6, 0xaa
    mulss xmm6, xmm2
    addss xmm4, xmm6
    addss xmm4, [rdx+28]
    movaps xmm6, xmm5
    mulss xmm5, xmm0
    shufps xmm6, xmm6, 0x55
    mulss xmm6, xmm1
    addss xmm5, xmm6
    movups xmm6, [rdx+48]
    shufps xmm6, xmm6, 0xaa
    mulss xmm6, xmm2
    addss xmm5, xmm6
    addss xmm5, [rdx+60]
    maxss xmm5, [rip+.near]
    divss xmm3, xmm5
    divss xmm4, xmm5
    mulss xmm3, xmm8
    mulss xmm4, xmm9
    addss xmm3, xmm10
    movaps xmm6, xmm11
    subss xmm6, xmm4
    movss [r8], xmm3
    movss [r8+4], xmm6
    add rdi, 12
    add r8, 8
    dec esi
    jnz .project_loop
.project_done:
    ret

.global asm_ui_editor_hit
.type asm_ui_editor_hit,@function
asm_ui_editor_hit:
    // rdi hits, esi count, xmm0 x, xmm1 y; 24-byte EditorHit.
    mov eax, esi
.hit_loop:
    dec eax
    js .hit_done
    imul ecx, eax, 24
    lea rdx, [rdi+rcx]
    ucomiss xmm0, [rdx]
    jb .hit_loop
    ucomiss xmm1, [rdx+4]
    jb .hit_loop
    movss xmm2, [rdx]
    addss xmm2, [rdx+8]
    ucomiss xmm0, xmm2
    jae .hit_loop
    movss xmm2, [rdx+4]
    addss xmm2, [rdx+12]
    ucomiss xmm1, xmm2
    jae .hit_loop
.hit_done:
    ret
.section .rodata
.half: .float 0.5
.near: .float 0.00001
.section .note.GNU-stack,"",@progbits
