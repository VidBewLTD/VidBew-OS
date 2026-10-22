BITS 32
section .text

global idt_load
global default_isr_stub
global irq0_stub
global irq1_stub
global irq12_stub
global exc_table

[extern unhandled_interrupt_c]
[extern keyboard_handler_c]
[extern timer_handler_c]
[extern mouse_handler_c]
[extern exception_c]

; void idt_load(unsigned int idt_ptr_addr)
idt_load:
    mov eax, [esp + 4]
    lidt [eax]
    ret

; Generic fallback for any interrupt we haven't specifically written a
; handler for.
default_isr_stub:
    pusha
    call unhandled_interrupt_c
    popa
    iret

; IRQ0 (PIT timer), vector 0x20.
irq0_stub:
    pusha
    call timer_handler_c
    popa
    iret

; IRQ1 (keyboard), remapped to vector 0x21.
irq1_stub:
    pusha
    call keyboard_handler_c
    popa
    iret

; IRQ12 (mouse, on the slave PIC), remapped to vector 0x2C.
irq12_stub:
    pusha
    call mouse_handler_c
    popa
    iret

; ---- CPU exceptions 0-19 -------------------------------------------------
; Some exceptions push an error code, some don't; normalise so the C handler
; always sees [vector][error][eip][cs][eflags] above the pusha block.
%macro EXC_NOERR 1
exc%1:
    push dword 0
    push dword %1
    jmp exc_common
%endmacro
%macro EXC_ERR 1
exc%1:
    push dword %1
    jmp exc_common
%endmacro

EXC_NOERR 0
EXC_NOERR 1
EXC_NOERR 2
EXC_NOERR 3
EXC_NOERR 4
EXC_NOERR 5
EXC_NOERR 6
EXC_NOERR 7
EXC_ERR   8
EXC_NOERR 9
EXC_ERR   10
EXC_ERR   11
EXC_ERR   12
EXC_ERR   13
EXC_ERR   14
EXC_NOERR 15
EXC_NOERR 16
EXC_ERR   17
EXC_NOERR 18
EXC_NOERR 19

exc_common:
    pusha
    mov eax, esp
    push eax
    call exception_c
    add esp, 4
    popa
    add esp, 8
    iret

section .data
exc_table:
    dd exc0, exc1, exc2, exc3, exc4, exc5, exc6, exc7, exc8, exc9
    dd exc10, exc11, exc12, exc13, exc14, exc15, exc16, exc17, exc18, exc19

section .note.GNU-stack noalloc noexec nowrite progbits
