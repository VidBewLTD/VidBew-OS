ASM = nasm
CC = gcc
LD = ld

# -O2 plus "general registers only": the kernel never enables SSE/x87, so the
# compiler must not emit those instructions (it would fault). The loop
# distribution flag keeps GCC from turning our memcpy/memset loops into calls
# to themselves.
CFLAGS = -m32 -march=i686 -O2 -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
         -fno-strict-aliasing -fno-tree-loop-distribute-patterns -fno-asynchronous-unwind-tables \
         -mgeneral-regs-only -mpreferred-stack-boundary=2 \
         -nostdlib -nostartfiles -nodefaultlibs -Wall -Wextra -Wno-unused-parameter $(EXTRA_CFLAGS) -c
LDFLAGS = -m elf_i386 -T link.ld --oformat elf32-i386 --build-id=none -z noexecstack --no-warn-rwx-segments

C_SOURCES = $(wildcard *.c)
C_OBJECTS = $(C_SOURCES:.c=.o)
ASM_OBJECTS = kernel_entry.o isr.o switch_task.o
HEADERS = $(wildcard *.h)

# Disk layout (512-byte sectors, LBA on the whole image):
#   sector 0              -- boot.bin   (stage 1)
#   sectors 1..8          -- stage2.bin (loader: kernel load, video mode, 32-bit entry)
#   sectors 9..           -- kernel.bin
#   sector 2047           -- settings sector (display mode etc., written by the Control Panel)
#   sector 2048 onward    -- the FAT16 data partition (fat16.img)
STAGE2_SECTORS = 8
KERNEL_LBA = 9
SETTINGS_LBA = 2047
FAT_PARTITION_SECTOR = 2048
FAT_SIZE_MB = 15

all: os.img os.iso

iso: os.iso

boot.bin: boot.asm
	$(ASM) -f bin -DSTAGE2_SECTORS=$(STAGE2_SECTORS) boot.asm -o boot.bin

kernel_entry.o: kernel_entry.asm
	$(ASM) -f elf32 kernel_entry.asm -o kernel_entry.o

isr.o: isr.asm
	$(ASM) -f elf32 isr.asm -o isr.o

switch_task.o: switch_task.asm
	$(ASM) -f elf32 switch_task.asm -o switch_task.o

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) $< -o $@

kernel.elf: $(ASM_OBJECTS) $(C_OBJECTS) link.ld
	$(LD) $(LDFLAGS) -o kernel.elf $(ASM_OBJECTS) $(C_OBJECTS)

kernel.bin: kernel.elf
	objcopy -O binary kernel.elf kernel.bin
	@end=$$(nm kernel.elf | awk '/ _end$$/ {print $$1}'); \
	 echo "kernel image ends at 0x$$end (stack top is 0x9F000)"; \
	 test $$((0x$$end)) -lt $$((0x80000)) || { echo "ERROR: kernel image too large"; exit 1; }
	@test $$(( ($$(stat -c%s kernel.bin) + 511) / 512 + $(KERNEL_LBA) )) -lt $(SETTINGS_LBA) || { echo "ERROR: kernel overlaps settings sector"; exit 1; }

stage2.bin: stage2.asm kernel.bin
	$(ASM) -f bin -DKERNEL_SECTORS=$$(( ($$(stat -c%s kernel.bin) + 511) / 512 )) \
	    -DKERNEL_LBA=$(KERNEL_LBA) -DSETTINGS_LBA=$(SETTINGS_LBA) stage2.asm -o stage2.bin
	@test $$(stat -c%s stage2.bin) -le $$(( $(STAGE2_SECTORS) * 512 )) || { echo "ERROR: stage2 too large"; exit 1; }

fat16.img: $(wildcard disk_files/*)
	dd if=/dev/zero of=fat16.img bs=1M count=$(FAT_SIZE_MB) status=none
	mkfs.fat -F 16 -n VIDBEW_OS fat16.img > /dev/null
	for f in disk_files/*; do mcopy -i fat16.img $$f ::$$(basename $$f); done

os.img: boot.bin stage2.bin kernel.bin fat16.img
	dd if=/dev/zero of=os.img bs=512 count=$$(( $(FAT_PARTITION_SECTOR) + $(FAT_SIZE_MB) * 2048 )) status=none
	dd if=boot.bin of=os.img conv=notrunc status=none
	dd if=stage2.bin of=os.img bs=512 seek=1 conv=notrunc status=none
	dd if=kernel.bin of=os.img bs=512 seek=$(KERNEL_LBA) conv=notrunc status=none
	dd if=fat16.img of=os.img bs=512 seek=$(FAT_PARTITION_SECTOR) conv=notrunc status=none

os.iso: os.img
	genisoimage -b os.img -no-emul-boot -boot-load-size 4 -o os.iso .

# Default QEMU gives the guest an e1000 NIC with user-mode networking, so
# the Network applet works out of the box. Add -vga std (the default) for
# live resolution switching.
run: os.img
	qemu-system-i386 -drive format=raw,file=os.img

clean:
	rm -f *.o *.bin *.elf *.img *.iso

.PHONY: all run clean iso
