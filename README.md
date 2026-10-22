# VidBew/OS

A hobby desktop operating system written from scratch in C and x86 assembly —
no Linux, no COSMOS, no borrowed kernel. (*NovaOS* was its codename; **VidBew/OS**
is the name. **Nova** is still the desktop shell and **Heroine** the kernel.)

This release adds a taskbar, a top menu bar, real window multitasking,
networking, selectable display modes and a Control Panel.

## Run it

```sh
make            # builds os.img  (needs nasm, gcc with -m32 support, ld, mtools, dosfstools)
make run        # qemu-system-i386 -drive format=raw,file=os.img
```

Useful QEMU variations:

| What | Flags |
|---|---|
| Sound (Control Panel > Sound, `resonate`, double-click a WAV) | `-device sb16` |
| Realtek NIC instead of the default Intel e1000 | `-nic user,model=rtl8139` |
| No network adapter | `-nic none` |
| More memory (the heap uses up to 32 MB of it) | `-m 256` |

The default QEMU command already gives the guest an e1000 NIC on user-mode
networking, so DHCP (10.0.2.15), ping to the gateway and DNS work with no extra flags.

## What you get

**Desktop.** A menu bar across the top (`VidBew  File  Edit  View  Special  Help`,
input mode and clock on the right) and a taskbar along the bottom (Start menu,
one button per window, network and input-mode tray). File, Edit and View change
to match the window you are working in; Cut/Copy/Paste items are only enabled
where an app implements them.

**True window multitasking.** Every window belongs to an app that runs as its
own preemptively scheduled task. Apps draw into private surfaces; only the UI
task touches the screen, so windows never fight over pixels. Move, resize
(lower-right corner), minimize, maximize (double-click the title), cascade,
tile, Alt+Tab. A window that stops answering is force-closed after about three
seconds; an app that crashes (CPU exception) loses only its own window.
Try the Terminal command `shatter` to see that happen.

**Apps.** Program Manager, File Explorer (browse / preview / delete / play WAV),
Terminal (several at once), Control Panel, Task Manager, Clock, On-Screen
Keyboard (never steals focus), Help, About, and — new in this release —
**Calculator** (two decimal places, keyboard or mouse), **Calendar**,
**Alarm** (four alarms; rings once when the clock reaches the minute, so keep the
window open or minimized), **Paint** (pen, line, rectangle, filled rectangle, oval,
flood fill, eraser, brush size, 16 colours; Save/Open as `PAINT.BMP`) and
**Image Viewer** (uncompressed BMP at 1, 4, 8, 24 and 32 bits per pixel; sample
images are on the disk). They are in the VidBew menu, the Program Manager, and
`portal calc|calendar|alarm|paint|images`.

**Display types.** The boot loader asks the video BIOS for every VESA mode with
a linear framebuffer (8, 15, 16, 24 and 32 bits per pixel, 640 to 1600 wide)
and hands the list to the kernel. *Control Panel > Display* shows what your
BIOS offers. Everything is drawn in one 256-colour indexed format and converted
when it reaches the screen, so every depth looks the same and works with no
per-mode code. Where the emulator exposes the Bochs VBE ports (QEMU, Bochs,
VirtualBox) the mode changes **live**, with a "keep these settings?" countdown
that reverts automatically. Elsewhere the choice is saved and applied by the boot
loader on the next restart. Six colour schemes are included.

**Networking.** PCI scan; drivers for Intel e1000 (QEMU's default) and Realtek
RTL8139; Ethernet, ARP, IPv4, ICMP (ping, and it answers pings), UDP, a DHCP
client and a DNS resolver. A background `netd` task receives; blocking calls
(ping, DNS, DHCP) run in the caller's task. *Control Panel > Network* shows the
adapter and lets you use DHCP or a fixed address (saved across restarts).

**Control Panel.** Display, Network, Input (pointing mode, pointer speed,
double-click speed), Sound (volume, test), Date & Time (set the clock), System
(CPU, memory, tasks, uptime).

## Keys

| Key | Action |
|---|---|
| Alt+Tab / Alt+F4 | switch windows / close the focused window |
| F1 | on-screen keyboard |
| F2 | cycle input mode: Mouse, Keyboard navigation, Touch |
| Alt+Arrows | move the pointer from the keyboard in any mode |
| Arrows + Left Ctrl | in Keyboard-navigation mode: move the pointer, Ctrl clicks |
| Windows key | Start menu |

In Mouse and Touch mode the arrow keys go to the focused window (list selection,
Terminal history). In Keyboard-navigation mode they move the pointer, as before.

## Terminal (Heroine) commands

The original vocabulary — `guide inventory inspect scribe proclaim chronicle
vigil horizon essence resonate` — plus:

| Command | Does |
|---|---|
| `portal <app>` | open a window (explorer, terminal, control, tasks, clock, keyboard, network, display, help, about, programs, calc, calendar, alarm, paint, images) |
| `legion` | list tasks |
| `vista` | current display mode and every mode the BIOS offers |
| `tether` | network status (adapter, link, address, packet counts) |
| `summon` | request an address by DHCP |
| `beacon <host>` | ping an address or hostname |
| `seek <name>` | DNS lookup |
| `anchor <ip> <mask> <gw> [dns]` | set a static address and save it |
| `rebirth` / `farewell` | restart / shut down |
| `shatter` | crash this Terminal on purpose |

Tab completes command names; Up/Down recall history; PageUp/PageDown scroll back.

## How it fits together

```
sector 0        boot.asm     stage 1: loads stage 2 with BIOS LBA reads
sectors 1-8     stage2.asm   loads the kernel, reads the settings sector, lists VBE
                             modes, sets one, sizes RAM, enters 32-bit mode
sectors 9...    kernel.bin   Heroine + Nova (flat binary at 0x8000)
sector 2047     settings     display mode, scheme, speeds, volume, static IP ('VBOS')
sector 2048...  FAT16 data partition (root directory, 8.3 names)
```

Hand-off block at 0x7E00 (framebuffer, size, pitch, depth, colour masks, mode
count, RAM size) and the mode table at 0x7000 are documented at the top of
`stage2.asm`.

| File | Role |
|---|---|
| `kernel.c` | start-up order and the UI loop |
| `task.c`, `switch_task.asm`, `timer.c`, `sync.c` | preemptive scheduler (sleep, wake, kill, reap), recursive mutexes |
| `idt.c`, `isr.asm` | interrupts and per-task CPU-exception containment |
| `heap.c`, `kstd.c` | allocator (up to 32 MB), mini libc and `ksnprintf` |
| `gfx.c`, `icons.c` | indexed surfaces, palette, multi-depth output, live mode switch, cursor |
| `wm.c` | windows, compositor with damage rectangles, menus, taskbar, Start menu, input routing |
| `ui.c` | classic 3D controls |
| `app_*.c`, `apps.c` | the apps and applets, registry, dialogs, power |
| `net.c`, `nic_*.c`, `pci.c` | the network stack and drivers |
| `fat16.c`, `ata.c` | filesystem (now replaces files on write, supports delete) and disk |
| `keyboard.c`, `mouse.c`, `input_mode.c` | input (event queues, so quick clicks are never lost) |
| `settings.c` | the settings sector |

## Honest scope notes

- **One address space.** CPU exceptions are contained per task, but there is no
  memory protection: a wild pointer in one app can still corrupt the kernel.
- **Image formats:** only uncompressed BMP so far (no PNG/JPEG decoder yet). Images
  are reduced to the 256-colour palette on load.
- **No TCP yet** (so no HTTP), no IPv6, no IP fragmentation. The NIC is polled
  at 100 Hz (pings busy-poll for 30 ms so the time shown is real).
- **Tested in QEMU only** (e1000 and RTL8139, standard VGA, SeaBIOS). Real
  hardware should work the same way through VBE, but it has not been tried.
  A 256-colour mode may not exist on modern firmware; the loader then falls back
  to 32- or 16-bit automatically.
- Live display switching needs the Bochs VBE ports; otherwise a restart applies it.
- FAT16 is root-directory only. The font is ASCII only. No mouse wheel.
- `make EXTRA_CFLAGS=-DWM_TRACE` logs every mouse event to the serial port.

## Verified

Booted headless in QEMU and driven through its monitor (scripted keys, mouse and
screenshots): all display modes (8/15/16/24/32 bpp, 640x480 to 1280x1024) from
the saved setting; live switch with Keep and with automatic revert; settings and
static IP surviving a restart; DHCP, ARP, ping (gateway), DNS against a local test
server, both NIC drivers; 50 open/close cycles of Explorer and Clock with no change
in heap use; crash containment (`shatter`); ACPI shutdown (`farewell`); on-screen
keyboard typing into a focused Terminal; maximize, tile, drag, Alt+Tab, menus.
