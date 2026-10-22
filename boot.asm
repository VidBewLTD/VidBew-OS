; boot.asm -- VidBew/OS stage 1: the 512-byte boot sector.
;
; The BIOS loads this at 0x7C00. A boot sector has no room for anything
; clever, so all it does is pull the real loader (stage2.asm, 8 sectors
; starting at LBA 1) into memory at 0x1000 using BIOS LBA reads, and jump to
; it. Stage 2 loads the kernel, picks the video mode and enters 32-bit mode.
BITS 16
ORG 0x7C00

%ifndef STAGE2_SECTORS
%define STAGE2_SECTORS 8
%endif

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    cld
    mov [boot_drive], dl

    ; INT 13h extensions (LBA addressing) are required.
    mov ah, 0x41
    mov bx, 0x55AA
    mov dl, [boot_drive]
    int 0x13
    jc .no_edd
    cmp bx, 0xAA55
    jne .no_edd

    mov si, dap
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc .disk_err

    mov dl, [boot_drive]
    jmp 0x0000:0x1000

.no_edd:
    mov si, msg_noedd
    jmp .fail
.disk_err:
    mov si, msg_disk
.fail:
    call puts
.hang:
    hlt
    jmp .hang

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

boot_drive: db 0
msg_noedd:  db 'VidBew/OS: BIOS has no LBA disk support.', 13, 10, 0
msg_disk:   db 'VidBew/OS: could not read the loader.', 13, 10, 0

align 4
dap:
    db 0x10, 0
    dw STAGE2_SECTORS
    dw 0x1000, 0x0000
    dd 1, 0

times 510 - ($ - $$) db 0
dw 0xAA55
