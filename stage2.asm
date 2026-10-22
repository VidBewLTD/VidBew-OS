; stage2.asm -- VidBew/OS second-stage loader (real mode, loaded at 0x1000).
;
; Jobs, in order:
;   1. load the kernel from disk to 0x8000 (BIOS LBA reads, 4KB at a time so
;      no transfer ever straddles a 64KB DMA boundary)
;   2. read the settings sector (LBA SETTINGS_LBA) -- if the Control Panel has
;      saved a preferred display mode there, we try it first
;   3. enumerate every VBE mode the BIOS offers (linear framebuffer, 8/15/16/
;      24/32 bpp) into a table at 0x7000 for the kernel's Display applet
;   4. set the best mode: saved choice -> 800x600 -> 640x480 -> anything
;   5. ask the BIOS how much RAM there is, enable A20, enter 32-bit mode
;
; Hand-off block for the kernel at 0x7E00:
;   +0  dd framebuffer physical address     +20 dw number of modes in table
;   +4  dw width   +6 dw height             +22 dd extended memory (KB above 1MB)
;   +8  dw pitch (bytes per scanline)       +26 db boot drive
;   +10 db bits per pixel                   +27 db flags: 1=settings found,
;   +11 db memory model (4=packed, 6=RGB)       2=saved mode used, 4=fell back
;   +12 dw VBE mode number
;   +14 db red size, red pos, green size, green pos, blue size, blue pos
; Mode table entries (8 bytes): dw mode, dw width, dw height, db bpp, db model
BITS 16
ORG 0x1000

%ifndef KERNEL_SECTORS
%define KERNEL_SECTORS 128
%endif
%ifndef KERNEL_LBA
%define KERNEL_LBA 9
%endif
%ifndef SETTINGS_LBA
%define SETTINGS_LBA 2047
%endif

BOOTINFO   equ 0x7E00
MODETABLE  equ 0x7000
VBEINFO    equ 0x2000
MODEINFO   equ 0x2200
SETBUF     equ 0x2400
MAX_MODES  equ 64
CODE_SEG   equ 0x08
DATA_SEG   equ 0x10

stage2:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    cld
    mov [boot_drive], dl

    mov si, msg_banner
    call puts

    ; ---- clear the hand-off block ----
    mov di, BOOTINFO
    mov cx, 128
    xor ax, ax
    rep stosw
    mov al, [boot_drive]
    mov [BOOTINFO + 26], al

    ; ---- 1. load the kernel ----
    mov dword [k_lba], KERNEL_LBA
    mov dword [k_left], KERNEL_SECTORS
    mov word [k_seg], 0x0800
.kload:
    cmp dword [k_left], 0
    je .kdone
    mov eax, [k_left]
    cmp eax, 8
    jbe .kcount
    mov eax, 8
.kcount:
    mov [dap_count], ax
    mov word [dap_off], 0
    mov bx, [k_seg]
    mov [dap_seg], bx
    mov edx, [k_lba]
    mov [dap_lba], edx
    mov dword [dap_lba + 4], 0
    mov si, dap
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc disk_error
    movzx eax, word [dap_count]
    add [k_lba], eax
    sub [k_left], eax
    shl eax, 5                  ; 512-byte sectors -> 16-byte paragraphs
    add [k_seg], ax
    jmp .kload
.kdone:

    ; ---- 2. settings sector ----
    mov byte [have_settings], 0
    mov word [dap_count], 1
    mov word [dap_off], SETBUF
    mov word [dap_seg], 0
    mov dword [dap_lba], SETTINGS_LBA
    mov dword [dap_lba + 4], 0
    mov si, dap
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc .nosettings
    cmp dword [SETBUF], 0x534F4256      ; 'VBOS'
    jne .nosettings
    mov byte [have_settings], 1
    or byte [BOOTINFO + 27], 1
.nosettings:

    ; ---- 3. enumerate VBE modes ----
    mov di, VBEINFO
    mov dword [di], 'VBE2'
    mov ax, 0x4F00
    int 0x10
    cmp ax, 0x004F
    jne vbe_error
    cmp dword [VBEINFO], 'VESA'
    jne vbe_error
    mov si, [VBEINFO + 14]
    mov ax, [VBEINFO + 16]
    mov fs, ax
    mov word [mode_count], 0
    mov word [table_ptr], MODETABLE
.mloop:
    mov cx, [fs:si]
    add si, 2
    cmp cx, 0xFFFF
    je .mdone
    mov [cur_mode], cx
    mov [list_si], si
    mov ax, 0x4F01
    mov di, MODEINFO
    int 0x10
    cmp ax, 0x004F
    jne .mnext
    mov ax, [MODEINFO]
    and ax, 0x0099                      ; supported + colour + graphics + linear FB
    cmp ax, 0x0099
    jne .mnext
    mov al, [MODEINFO + 0x1B]           ; memory model
    cmp al, 4
    je .model_ok
    cmp al, 6
    jne .mnext
.model_ok:
    mov al, [MODEINFO + 0x19]           ; bits per pixel
    cmp al, 8
    je .bpp_ok
    cmp al, 15
    je .bpp_ok
    cmp al, 16
    je .bpp_ok
    cmp al, 24
    je .bpp_ok
    cmp al, 32
    jne .mnext
.bpp_ok:
    mov ax, [MODEINFO + 0x12]
    cmp ax, 640
    jb .mnext
    cmp ax, 1600
    ja .mnext
    ; skip if this width/height/bpp is already in the table
    mov di, MODETABLE
    mov cx, [mode_count]
    test cx, cx
    jz .add
.dup:
    mov ax, [MODEINFO + 0x12]
    cmp ax, [di + 2]
    jne .dupnext
    mov ax, [MODEINFO + 0x14]
    cmp ax, [di + 4]
    jne .dupnext
    mov al, [MODEINFO + 0x19]
    cmp al, [di + 6]
    je .mnext
.dupnext:
    add di, 8
    loop .dup
.add:
    cmp word [mode_count], MAX_MODES
    jae .mdone
    mov di, [table_ptr]
    mov ax, [cur_mode]
    mov [di], ax
    mov ax, [MODEINFO + 0x12]
    mov [di + 2], ax
    mov ax, [MODEINFO + 0x14]
    mov [di + 4], ax
    mov al, [MODEINFO + 0x19]
    mov [di + 6], al
    mov al, [MODEINFO + 0x1B]
    mov [di + 7], al
    add word [table_ptr], 8
    inc word [mode_count]
.mnext:
    mov si, [list_si]
    jmp .mloop
.mdone:
    mov ax, [mode_count]
    mov [BOOTINFO + 20], ax
    test ax, ax
    jz vbe_error

    ; ---- 4. choose a mode ----
    cmp byte [have_settings], 0
    je .defaults
    mov ax, [SETBUF + 6]
    mov [want_w], ax
    mov ax, [SETBUF + 8]
    mov [want_h], ax
    mov al, [SETBUF + 10]
    mov [want_bpp], al
    call try_wanted
    jc .defaults
    or byte [BOOTINFO + 27], 2
    jmp .modeset
.defaults:
    mov si, default_modes
.defloop:
    mov ax, [si]
    test ax, ax
    jz .lastresort
    mov [want_w], ax
    mov ax, [si + 2]
    mov [want_h], ax
    mov al, [si + 4]
    mov [want_bpp], al
    push si
    call try_wanted
    pop si
    jnc .fellback
    add si, 6
    jmp .defloop
.lastresort:
    mov si, MODETABLE
    mov cx, [mode_count]
.lrloop:
    push si
    push cx
    mov ax, [si]
    call set_vbe_mode
    pop cx
    pop si
    jnc .fellback
    add si, 8
    loop .lrloop
    jmp vbe_error
.fellback:
    or byte [BOOTINFO + 27], 4
.modeset:

    ; ---- 5. memory size ----
    xor ecx, ecx
    xor edx, edx
    mov ax, 0xE801
    int 0x15
    jc .mem88
    test ax, ax
    jnz .e801ok
    mov ax, cx
    mov bx, dx
.e801ok:
    movzx eax, ax
    movzx ebx, bx
    shl ebx, 6
    add eax, ebx
    jmp .memdone
.mem88:
    mov ah, 0x88
    int 0x15
    movzx eax, ax
.memdone:
    mov [BOOTINFO + 22], eax

    ; ---- A20 + protected mode ----
    in al, 0x92
    or al, 2
    out 0x92, al
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp CODE_SEG:pm_entry

; ---------------------------------------------------------------------------
; try_wanted: look up want_w/want_h/want_bpp in the table and set it.
; CF=0 on success.
try_wanted:
    call find_mode
    jc .fail
    call set_vbe_mode
    ret
.fail:
    stc
    ret

find_mode:
    mov si, MODETABLE
    mov cx, [mode_count]
    test cx, cx
    jz .nf
.l:
    mov ax, [want_w]
    cmp ax, [si + 2]
    jne .next
    mov ax, [want_h]
    cmp ax, [si + 4]
    jne .next
    mov al, [want_bpp]
    cmp al, [si + 6]
    jne .next
    mov ax, [si]
    clc
    ret
.next:
    add si, 8
    loop .l
.nf:
    stc
    ret

; set_vbe_mode: AX = VBE mode number. CF=0 on success (hand-off filled in).
set_vbe_mode:
    mov [cur_mode], ax
    mov cx, ax
    mov ax, 0x4F01
    mov di, MODEINFO
    int 0x10
    cmp ax, 0x004F
    jne .fail
    mov bx, [cur_mode]
    or bx, 0x4000                       ; linear framebuffer
    mov ax, 0x4F02
    int 0x10
    cmp ax, 0x004F
    jne .fail
    mov eax, [MODEINFO + 0x28]
    mov [BOOTINFO + 0], eax
    mov ax, [MODEINFO + 0x12]
    mov [BOOTINFO + 4], ax
    mov ax, [MODEINFO + 0x14]
    mov [BOOTINFO + 6], ax
    mov ax, [MODEINFO + 0x10]
    mov [BOOTINFO + 8], ax
    mov al, [MODEINFO + 0x19]
    mov [BOOTINFO + 10], al
    mov al, [MODEINFO + 0x1B]
    mov [BOOTINFO + 11], al
    mov ax, [cur_mode]
    mov [BOOTINFO + 12], ax
    mov al, [MODEINFO + 0x1F]
    mov [BOOTINFO + 14], al
    mov al, [MODEINFO + 0x20]
    mov [BOOTINFO + 15], al
    mov al, [MODEINFO + 0x21]
    mov [BOOTINFO + 16], al
    mov al, [MODEINFO + 0x22]
    mov [BOOTINFO + 17], al
    mov al, [MODEINFO + 0x23]
    mov [BOOTINFO + 18], al
    mov al, [MODEINFO + 0x24]
    mov [BOOTINFO + 19], al
    ; the real scanline length (it can differ from the mode-info field)
    mov ax, 0x4F06
    mov bl, 1
    int 0x10
    cmp ax, 0x004F
    jne .keep
    test bx, bx
    jz .keep
    mov [BOOTINFO + 8], bx
.keep:
    clc
    ret
.fail:
    stc
    ret

puts:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    jmp puts
.done:
    ret

disk_error:
    mov si, msg_disk
    jmp fatal
vbe_error:
    mov si, msg_vbe
fatal:
    call puts
.hang:
    hlt
    jmp .hang

; ---------------------------------------------------------------------------
BITS 32
pm_entry:
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x9F000
    mov eax, 0x8000
    jmp eax

BITS 16
; ---------------------------------------------------------------------------
boot_drive:    db 0
have_settings: db 0
want_bpp:      db 0
want_w:        dw 0
want_h:        dw 0
cur_mode:      dw 0
list_si:       dw 0
mode_count:    dw 0
table_ptr:     dw 0
k_lba:         dd 0
k_left:        dd 0
k_seg:         dw 0

msg_banner: db 'VidBew/OS loader...', 13, 10, 0
msg_disk:   db 'VidBew/OS: disk read error.', 13, 10, 0
msg_vbe:    db 'VidBew/OS: no usable VESA video mode.', 13, 10, 0

; preferred modes when nothing is saved: width, height, bpp
default_modes:
    dw 800, 600
    db 8, 0
    dw 800, 600
    db 32, 0
    dw 800, 600
    db 16, 0
    dw 640, 480
    db 8, 0
    dw 640, 480
    db 32, 0
    dw 640, 480
    db 16, 0
    dw 0

align 4
dap:
dap_size:  db 0x10
           db 0
dap_count: dw 0
dap_off:   dw 0
dap_seg:   dw 0
dap_lba:   dd 0, 0

align 8
gdt_start:
    dq 0
gdt_code:
    dw 0xFFFF, 0x0000
    db 0x00, 10011010b, 11001111b, 0x00
gdt_data:
    dw 0xFFFF, 0x0000
    db 0x00, 10010010b, 11001111b, 0x00
gdt_end:
gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start
