#include "abi.inc"
.intel_syntax noprefix
.text

.global dse_quantize
.type dse_quantize,@function
dse_quantize:
    xor eax,eax
    test edi,edi
    js 1f
    mov eax,edi
    cmp eax,10000000
    mov ecx,10000000
    cmovg eax,ecx
    cmp esi,1
    jle 1f
    mov ecx,esi
    shr ecx,1
    add eax,ecx
    xor edx,edx
    div esi
    imul eax,esi
    cmp eax,10000000
    mov ecx,10000000
    cmovg eax,ecx
1:  ret
.size dse_quantize,.-dse_quantize

.global dse_add_note
.type dse_add_note,@function
dse_add_note:
    mov eax,-1
    cmp esi,[rdi+S_PARTS]
    jae .add_out
    test edx,edx
    js .add_out
    cmp ecx,1
    jl .add_out
    cmp r8d,127
    ja .add_out
    mov r9d,edx
    add r9d,ecx
    jo .add_out
    cmp r9d,10000000
    jg .add_out
    mov r9d,[rdi+S_COUNT]
    cmp r9d,2048
    jae .add_out
    push rbx
    push rbp
    push r12
    push r13
    mov rbx,rdi
    mov ebp,esi
    mov r12d,edx
    mov r13d,ecx
    imul eax,r9d,N_SIZE
    lea rdi,[rbx+rax+S_NOTES]
    mov r10,rdi
    xor eax,eax
    mov ecx,20
    rep stosq
    mov eax,[rbx+S_NEXT]
    inc DWORD PTR [rbx+S_NEXT]
    mov [r10],eax
    mov [r10+N_POS],r12d
    mov [r10+N_LEN],r13d
    mov [r10+N_KEY],r8d
    mov [r10+N_PART],ebp
    mov DWORD PTR [r10+N_SELECTED],1
    mov BYTE PTR [r10+N_LYRIC],97
    mov DWORD PTR [r10+N_PHONEMES],0x612f687a
    mov DWORD PTR [r10+152],0x3f800000
    mov eax,[rbx+S_COUNT]
    inc DWORD PTR [rbx+S_COUNT]
    inc QWORD PTR [rbx+S_REV]
    pop r13
    pop r12
    pop rbp
    pop rbx
.add_out:
    ret
.size dse_add_note,.-dse_add_note

.global dse_select
.type dse_select,@function
dse_select:
    mov r8d,[rdi+S_COUNT]
    lea r9,[rdi+S_NOTES]
    xor ecx,ecx
.select_loop:
    cmp ecx,r8d
    jae .select_done
    cmp ecx,esi
    jne .not_target
    test edx,edx
    jz .target_one
    xor DWORD PTR [r9+N_SELECTED],1
    jmp .select_next
.target_one:
    mov DWORD PTR [r9+N_SELECTED],1
    jmp .select_next
.not_target:
    test edx,edx
    jnz .select_next
    mov DWORD PTR [r9+N_SELECTED],0
.select_next:
    add r9,N_SIZE
    inc ecx
    jmp .select_loop
.select_done:
    ret
.size dse_select,.-dse_select

.global dse_delete_selected
.type dse_delete_selected,@function
dse_delete_selected:
    push rbx
    mov rbx,rdi
    lea rsi,[rdi+S_NOTES]
    mov rdi,rsi
    mov r8d,[rbx+S_COUNT]
    xor r9d,r9d
    xor r10d,r10d
.delete_loop:
    cmp r9d,r8d
    jae .delete_done
    inc r9d
    cmp DWORD PTR [rsi+N_SELECTED],0
    jne .delete_skip
    mov ecx,20
    rep movsq
    inc r10d
    jmp .delete_loop
.delete_skip:
    add rsi,N_SIZE
    jmp .delete_loop
.delete_done:
    mov [rbx+S_COUNT],r10d
    mov eax,r8d
    sub eax,r10d
    jz 1f
    inc QWORD PTR [rbx+S_REV]
1:  pop rbx
    ret
.size dse_delete_selected,.-dse_delete_selected

/* Validate the whole selection before applying a group gesture, so a boundary
 * note cannot leave the rest of the selection partially moved. */
.global dse_transform_selected
.type dse_transform_selected,@function
dse_transform_selected:
    lea r9,[rdi+S_NOTES]
    mov r10d,[rdi+S_COUNT]
    xor r11d,r11d
    xor eax,eax
.transform_check:
    cmp r11d,r10d
    jae .transform_apply_begin
    cmp DWORD PTR [r9+N_SELECTED],0
    je .transform_check_next
    inc eax
    mov r8d,[r9+N_POS]
    add r8d,esi
    jo .transform_bad
    test r8d,r8d
    js .transform_bad
    mov r8d,[r9+N_KEY]
    add r8d,edx
    cmp r8d,127
    ja .transform_bad
    mov r8d,[r9+N_LEN]
    add r8d,ecx
    jo .transform_bad
    cmp r8d,1
    jl .transform_bad
    add r8d,[r9+N_POS]
    jo .transform_bad
    add r8d,esi
    jo .transform_bad
    cmp r8d,10000000
    jg .transform_bad
.transform_check_next:
    inc r11d
    add r9,N_SIZE
    jmp .transform_check
.transform_apply_begin:
    test eax,eax
    jz .transform_done
    lea r9,[rdi+S_NOTES]
    xor r11d,r11d
.transform_apply:
    cmp r11d,r10d
    jae .transform_revision
    cmp DWORD PTR [r9+N_SELECTED],0
    je .transform_next
    add [r9+N_POS],esi
    add [r9+N_KEY],edx
    add [r9+N_LEN],ecx
.transform_next:
    inc r11d
    add r9,N_SIZE
    jmp .transform_apply
.transform_revision:
    inc QWORD PTR [rdi+S_REV]
.transform_done:
    ret
.transform_bad:
    mov eax,-1
    ret
.size dse_transform_selected,.-dse_transform_selected

.global dse_note_rect
.type dse_note_rect,@function
dse_note_rect:
    mov eax,[rdi+N_POS]
    sub eax,[rsi+V_SCROLL]
    cvtsi2ss xmm0,eax
    mulss xmm0,[rsi+V_BEAT]
    divss xmm0,[rip+.ppq]
    addss xmm0,[rsi+V_X]
    movss [rdx],xmm0
    mov eax,[rsi+V_TOP]
    sub eax,[rdi+N_KEY]
    cvtsi2ss xmm0,eax
    mulss xmm0,[rsi+V_ROW]
    addss xmm0,[rsi+V_Y]
    addss xmm0,[rip+.one]
    movss [rdx+4],xmm0
    cvtsi2ss xmm0,DWORD PTR [rdi+N_LEN]
    mulss xmm0,[rsi+V_BEAT]
    divss xmm0,[rip+.ppq]
    subss xmm0,[rip+.one]
    maxss xmm0,[rip+.two]
    movss [rdx+8],xmm0
    movss xmm0,[rsi+V_ROW]
    subss xmm0,[rip+.two]
    movss [rdx+12],xmm0
    ret
.size dse_note_rect,.-dse_note_rect

.global dse_grid_coord
.type dse_grid_coord,@function
dse_grid_coord:
    xor eax,eax
    ucomiss xmm0,xmm0
    jp .grid_out
    ucomiss xmm1,xmm1
    jp .grid_out
    subss xmm0,[rdi+V_X]
    subss xmm1,[rdi+V_Y]
    xorps xmm2,xmm2
    comiss xmm0,xmm2
    jb .grid_out
    comiss xmm1,xmm2
    jb .grid_out
    comiss xmm0,[rdi+V_W]
    jae .grid_out
    comiss xmm1,[rdi+V_H]
    jae .grid_out
    mulss xmm0,[rip+.ppq]
    divss xmm0,[rdi+V_BEAT]
    cvttss2si ecx,xmm0
    add ecx,[rdi+V_SCROLL]
    mov [rsi],ecx
    divss xmm1,[rdi+V_ROW]
    cvttss2si ecx,xmm1
    mov eax,[rdi+V_TOP]
    sub eax,ecx
    cmp eax,127
    ja .grid_invalid
    mov [rdx],eax
    mov eax,1
.grid_out:
    ret
.grid_invalid:
    xor eax,eax
    ret
.size dse_grid_coord,.-dse_grid_coord

.global dse_hit_note
.type dse_hit_note,@function
dse_hit_note:
    push rbx
    push r12
    push r13
    sub rsp,32
    mov r12,rdi
    mov r13,rsi
    movss [rsp+16],xmm0
    movss [rsp+20],xmm1
    mov rbx,rsp
    mov eax,[r12+S_COUNT]
    mov [rsp+24],eax
.hit_loop:
    dec DWORD PTR [rsp+24]
    js .hit_none
    imul eax,DWORD PTR [rsp+24],N_SIZE
    lea rdi,[r12+rax+S_NOTES]
    mov eax,[rdi+N_PART]
    cmp eax,[r13+V_PART]
    jne .hit_loop
    mov rsi,r13
    mov rdx,rbx
    call dse_note_rect
    movss xmm0,[rsp+16]
    subss xmm0,[rsp]
    xorps xmm2,xmm2
    comiss xmm0,xmm2
    jb .hit_loop
    comiss xmm0,[rsp+8]
    jae .hit_loop
    movss xmm1,[rsp+20]
    subss xmm1,[rsp+4]
    comiss xmm1,xmm2
    jb .hit_loop
    comiss xmm1,[rsp+12]
    jae .hit_loop
    mov eax,[rsp+24]
    jmp .hit_out
.hit_none:
    mov eax,-1
.hit_out:
    add rsp,32
    pop r13
    pop r12
    pop rbx
    ret
.size dse_hit_note,.-dse_hit_note

/* Sorted documents use a lower-bound search. The scan remains available for
 * caller-owned unsorted scores. Pitch uses MIDI; other lanes use native units. */
.global dse_curve_value
.type dse_curve_value,@function
dse_curve_value:
    mov ecx,[rdi+S_CURVE_COUNT]
    cmp DWORD PTR [rdi+S_CURVE_SORTED],1
    jne .curve_scan
    lea rdi,[rdi+S_CURVES]
    xor eax,eax
    mov r10d,ecx
.curve_search:
    cmp eax,r10d
    jae .curve_found
    mov r11d,r10d
    sub r11d,eax
    shr r11d,1
    add r11d,eax
    mov r8d,r11d
    shl r8,4
    add r8,rdi
    cmp [r8],esi
    jl .curve_search_low
    jg .curve_search_high
    cmp [r8+12],edx
    jb .curve_search_low
    ja .curve_search_high
    cvtsi2ss xmm2,DWORD PTR [r8+4]
    comiss xmm2,xmm0
    jb .curve_search_low
.curve_search_high:
    mov r10d,r11d
    jmp .curve_search
.curve_search_low:
    lea eax,[r11+1]
    jmp .curve_search
.curve_found:
    cmp eax,ecx
    jae .curve_fallback
    mov r8d,eax
    shl r8,4
    add r8,rdi
    cmp [r8],esi
    jne .curve_fallback
    cmp [r8+12],edx
    jne .curve_fallback
    cvtsi2ss xmm2,DWORD PTR [r8+4]
    comiss xmm2,xmm0
    je .curve_sorted_exact
    test eax,eax
    jz .curve_fallback
    lea r9,[r8-16]
    cmp [r9],esi
    jne .curve_fallback
    cmp [r9+12],edx
    jne .curve_fallback
    cvtsi2ss xmm3,DWORD PTR [r9+4]
    subss xmm0,xmm3
    subss xmm2,xmm3
    divss xmm0,xmm2
    movss xmm3,[r8+8]
    subss xmm3,[r9+8]
    mulss xmm0,xmm3
    addss xmm0,[r9+8]
    ret
.curve_sorted_exact:
    movss xmm0,[r8+8]
    ret
.curve_scan:
    lea rdi,[rdi+S_CURVES]
    mov r8d,-1
    mov r9d,10000001
    movaps xmm4,xmm1
    movaps xmm5,xmm1
.curve_loop:
    test ecx,ecx
    jz .curve_done
    cmp [rdi],esi
    jne .curve_next
    cmp [rdi+12],edx
    jne .curve_next
    cvtsi2ss xmm2,DWORD PTR [rdi+4]
    comiss xmm2,xmm0
    ja .curve_right
    mov eax,[rdi+4]
    cmp eax,r8d
    jl .curve_next
    mov r8d,eax
    movss xmm4,[rdi+8]
    jmp .curve_next
.curve_right:
    mov eax,[rdi+4]
    cmp eax,r9d
    jg .curve_next
    mov r9d,eax
    movss xmm5,[rdi+8]
.curve_next:
    add rdi,16
    dec ecx
    jmp .curve_loop
.curve_done:
    cvtsi2ss xmm2,r8d
    comiss xmm2,xmm0
    je .curve_exact
    cmp r8d,-1
    je .curve_fallback
    cmp r9d,10000001
    je .curve_fallback
    cvtsi2ss xmm2,r8d
    subss xmm0,xmm2
    sub r9d,r8d
    cvtsi2ss xmm2,r9d
    divss xmm0,xmm2
    subss xmm5,xmm4
    mulss xmm0,xmm5
    addss xmm0,xmm4
    ret
.curve_fallback:
    movaps xmm0,xmm1
    ret
.curve_exact:
    movaps xmm0,xmm4
    ret
.size dse_curve_value,.-dse_curve_value

.global dse_fill_f0
.type dse_fill_f0,@function
dse_fill_f0:
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    sub rsp,40
    mov r12,rdi
    mov ebp,esi
    mov r13,rdx
    mov r14d,ecx
    mov eax,r8d
    cvtsi2ss xmm0,eax
    cvtsi2ss xmm1,r9d
    divss xmm0,xmm1
    mulss xmm0,[rip+.sixty]
    divss xmm0,[r12+S_BPM]
    divss xmm0,[rip+.ppq]
    movss [rsp],xmm0
    movss xmm1,[rip+.one]
    divss xmm1,xmm0
    movss [rsp+4],xmm1
    mov rdi,r13
    mov ecx,r14d
    xor eax,eax
    rep stosd
    xor r15d,r15d
.f0_note:
    cmp r15d,[r12+S_COUNT]
    jae .f0_out
    imul eax,r15d,N_SIZE
    lea rbx,[r12+rax+S_NOTES]
    inc r15d
    cmp [rbx+N_PART],ebp
    jne .f0_note
    cvtsi2ss xmm0,DWORD PTR [rbx+N_POS]
    mulss xmm0,[rsp]
    addss xmm0,[rip+.half]
    cvttss2si eax,xmm0
    mov [rsp+8],eax
    mov eax,[rbx+N_POS]
    add eax,[rbx+N_LEN]
    cvtsi2ss xmm0,eax
    mulss xmm0,[rsp]
    addss xmm0,[rip+.half]
    cvttss2si eax,xmm0
    cmp eax,r14d
    cmova eax,r14d
    mov [rsp+12],eax
.f0_frame:
    mov eax,[rsp+8]
    cmp eax,[rsp+12]
    jae .f0_note
    cvtsi2ss xmm0,eax
    mulss xmm0,[rsp+4]
    cvtsi2ss xmm1,DWORD PTR [rbx+N_KEY]
    mov rdi,r12
    mov esi,ebp
    xor edx,edx
    call dse_curve_value
    subss xmm0,[rip+.a4]
    divss xmm0,[rip+.octave]
    call exp2f@PLT
    mulss xmm0,[rip+.hz]
    mov eax,[rsp+8]
    movss [r13+rax*4],xmm0
    inc DWORD PTR [rsp+8]
    jmp .f0_frame
.f0_out:
    add rsp,40
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    ret
.size dse_fill_f0,.-dse_fill_f0

.global dse_draw_notes
.type dse_draw_notes,@function
dse_draw_notes:
    push rbx
    push rbp
    push r12
    push r13
    push r14
    sub rsp,16
    mov r12,rdi
    mov r13,rsi
    mov r14,rdx
    xor ebp,ebp
.draw_note:
    cmp ebp,[r12+S_COUNT]
    jae .draw_out
    imul eax,ebp,N_SIZE
    lea rbx,[r12+rax+S_NOTES]
    inc ebp
    mov eax,[rbx+N_PART]
    cmp eax,[r13+V_PART]
    jne .draw_note
    mov rdi,rbx
    mov rsi,r13
    mov rdx,rsp
    call dse_note_rect
    movss xmm0,[rsp]
    addss xmm0,[rsp+8]
    comiss xmm0,[r13+V_X]
    jbe .draw_note
    movss xmm0,[r13+V_X]
    addss xmm0,[r13+V_W]
    comiss xmm0,[rsp]
    jbe .draw_note
    movss xmm0,[rsp+4]
    addss xmm0,[rsp+12]
    comiss xmm0,[r13+V_Y]
    jbe .draw_note
    movss xmm0,[r13+V_Y]
    addss xmm0,[r13+V_H]
    comiss xmm0,[rsp+4]
    jbe .draw_note
    mov rdi,r14
    mov rsi,rbx
    mov rdx,rsp
    call dse_paint_note@PLT
    jmp .draw_note
.draw_out:
    add rsp,16
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    ret
.size dse_draw_notes,.-dse_draw_notes
.section .rodata
.align 4
.ppq: .float 480.0
.one: .float 1.0
.two: .float 2.0
.half: .float 0.5
.sixty: .float 60.0
.a4: .float 69.0
.octave: .float 12.0
.hz: .float 440.0
.section .note.GNU-stack,"",@progbits
