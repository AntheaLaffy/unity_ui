.intel_syntax noprefix
.text
.globl dse_mix_stereo
.type dse_mix_stereo, @function
dse_mix_stereo:
    /* rdi dst, rsi src, rdx count, rcx index, r8 total, r9 fade-in.
     * fade-out is the first stack argument; xmm0/1 are channel gains. */
    mov r10, QWORD PTR [rsp + 8]
    movaps xmm6, xmm0
    movaps xmm7, xmm1
    test rdx, rdx
    jz .Ldone
    cvtsi2ss xmm4, r9
    cvtsi2ss xmm5, r10
.Lframe:
    mov eax, 0x3f800000
    movd xmm2, eax
    test r9, r9
    jz .Loutfade
    cmp rcx, r9
    jae .Loutfade
    cvtsi2ss xmm2, rcx
    divss xmm2, xmm4
.Loutfade:
    test r10, r10
    jz .Laccumulate
    mov rax, r8
    sub rax, rcx
    dec rax
    cmp rax, r10
    jae .Laccumulate
    cvtsi2ss xmm3, rax
    divss xmm3, xmm5
    mulss xmm2, xmm3
.Laccumulate:
    movss xmm0, DWORD PTR [rsi]
    mulss xmm0, xmm6
    mulss xmm0, xmm2
    addss xmm0, DWORD PTR [rdi]
    movss DWORD PTR [rdi], xmm0
    movss xmm1, DWORD PTR [rsi + 4]
    mulss xmm1, xmm7
    mulss xmm1, xmm2
    addss xmm1, DWORD PTR [rdi + 4]
    movss DWORD PTR [rdi + 4], xmm1
    add rdi, 8
    add rsi, 8
    inc rcx
    dec rdx
    jnz .Lframe
.Ldone:
    ret
.size dse_mix_stereo, .-dse_mix_stereo
.section .note.GNU-stack,"",@progbits
