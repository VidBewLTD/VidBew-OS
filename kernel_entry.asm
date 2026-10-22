; kernel_entry.asm -- first bytes of the kernel binary (loaded at 0x8000).
BITS 32

section .text.entry

[extern kmain]
[extern _bss_start]
[extern _bss_end]
global _start

_start:
    ; objcopy strips .bss out of the flat binary and our bootloader is a
    ; raw sector copy with no ELF-loader logic, so zero it here before
    ; any C code runs (C guarantees zero-initialized statics).
    mov edi, _bss_start
    mov ecx, _bss_end
    sub ecx, edi
    xor eax, eax
    cld
    rep stosb

    call kmain

    cli
.hang:
    hlt
    jmp .hang

section .note.GNU-stack noalloc noexec nowrite progbits
