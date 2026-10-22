BITS 32
section .text

global switch_task

; void switch_task(unsigned int *old_esp_store, unsigned int new_esp)
;
; Saves all general registers on the CURRENT stack, stashes the resulting
; ESP into *old_esp_store, then switches ESP to new_esp and restores
; whatever registers are sitting on THAT stack. If new_esp belongs to a task
; that has never run, task_create() pre-built that stack to look exactly like
; this layout, with the task's entry point sitting where `ret` will look for
; a return address -- so the same code path both resumes an old task and
; starts a new one.
;
; PUSHAD/POPAD don't touch EFLAGS and this ends in a plain `ret`, so a
; brand-new task would inherit IF=0 from the interrupt gate that got us here
; and never turn interrupts back on. The explicit `sti` below fixes that.
switch_task:
    push ebp
    mov ebp, esp
    pushad

    mov eax, [ebp + 8]      ; old_esp_store
    mov [eax], esp

    mov esp, [ebp + 12]     ; new_esp

    popad
    pop ebp
    sti
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
