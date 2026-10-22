#!/usr/bin/env python3
import struct, sys, shutil
src, dst, w, h, bpp = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5])
shutil.copy(src, dst)
sec = bytearray(512)
sec[0:4] = b'VBOS'
struct.pack_into('<HHHB', sec, 4, 1, w, h, bpp)
with open(dst, 'r+b') as f:
    f.seek(2047 * 512)
    f.write(sec)
