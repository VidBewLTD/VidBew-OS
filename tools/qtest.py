#!/usr/bin/env python3
"""Tiny QEMU driver: boot an image headless, script input via the HMP monitor,
grab screendumps (PPM->PNG) and capture the serial log."""
import os, socket, subprocess, sys, time

class VM:
    def __init__(self, img, extra=None, serial="/tmp/serial.log", sock="/tmp/qmon.sock", mem=128):
        self.sock_path = sock
        self.serial = serial
        for p in (sock, serial):
            try: os.remove(p)
            except FileNotFoundError: pass
        args = ["qemu-system-i386", "-drive", f"format=raw,file={img}",
                "-m", str(mem), "-display", "none",
                "-serial", f"file:{serial}",
                "-monitor", f"unix:{sock},server,nowait"]
        if extra: args += extra
        self.p = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        for _ in range(100):
            if os.path.exists(sock): break
            time.sleep(0.05)
        self.s = socket.socket(socket.AF_UNIX)
        for _ in range(100):
            try: self.s.connect(sock); break
            except Exception: time.sleep(0.05)
        self.s.settimeout(2.0)
        self._drain()

    def _drain(self):
        self.s.settimeout(0.03)
        try:
            while True:
                d = self.s.recv(65536)
                if not d: break
        except Exception: pass
        self.s.settimeout(2.0)

    def cmd(self, c, wait=0.05):
        self.s.sendall((c + "\n").encode())
        time.sleep(wait)
        self._drain()

    def key(self, k, wait=0.12): self.cmd(f"sendkey {k}", wait)
    def type(self, text, wait=0.1):
        m = {" ": "spc", "\n": "ret", ".": "dot", "-": "minus", "/": "slash", "_": "shift-minus", ":": "shift-semicolon", "'": "apostrophe", ",": "comma"}
        for ch in text:
            if ch in m: self.key(m[ch], wait)
            elif ch.isupper(): self.key("shift-" + ch.lower(), wait)
            else: self.key(ch, wait)
    def move(self, dx, dy, wait=0.05):
        # relative mouse in <=100 chunks, one monitor command each with a short gap
        while dx or dy:
            sx = max(-100, min(100, dx)); sy = max(-100, min(100, dy))
            self.s.sendall(f"mouse_move {sx} {sy}\n".encode())
            time.sleep(0.025)
            dx -= sx; dy -= sy
        time.sleep(wait)
        self._drain()
    def button(self, state, wait=0.08): self.cmd(f"mouse_button {state}", wait)
    def click(self, wait=0.15):
        self.button(1); self.button(0, wait)
    def dblclick(self, wait=0.4):
        for st in (1, 0, 1, 0):
            self.s.sendall(f"mouse_button {st}\n".encode())
            time.sleep(0.04)
        time.sleep(wait)
        self._drain()
    def screendump(self, png):
        ppm = png.replace(".png", ".ppm")
        self.cmd(f"screendump {ppm}", 0.4)
        try:
            from PIL import Image
            Image.open(ppm).save(png)
            os.remove(ppm)
        except Exception as e:
            print("screendump convert failed:", e)
    def log(self):
        try: return open(self.serial, errors="replace").read()
        except FileNotFoundError: return ""
    def stop(self):
        try: self.cmd("quit", 0.1)
        except Exception: pass
        try: self.p.wait(timeout=3)
        except Exception: self.p.kill()

if __name__ == "__main__":
    img = sys.argv[1]; out = sys.argv[2]; delay = float(sys.argv[3]) if len(sys.argv) > 3 else 3
    vm = VM(img)
    time.sleep(delay)
    vm.screendump(out)
    print(vm.log())
    vm.stop()
