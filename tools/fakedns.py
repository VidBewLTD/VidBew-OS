#!/usr/bin/env python3
# Tiny DNS server for testing the guest's resolver: answers every A query with 93.184.216.34
import socket, struct, sys
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("127.0.0.1", 53))
print("fakedns up", flush=True)
while True:
    data, addr = s.recvfrom(512)
    if len(data) < 12: continue
    tid = data[:2]
    # question section: from byte 12 to end of name + 4
    i = 12
    while data[i] != 0: i += data[i] + 1
    q = data[12:i+5]
    name = []
    j = 12
    while data[j] != 0:
        name.append(data[j+1:j+1+data[j]].decode()); j += data[j] + 1
    print("query", ".".join(name), "from", addr, flush=True)
    resp = tid + b"\x81\x80" + b"\x00\x01\x00\x01\x00\x00\x00\x00" + q
    resp += b"\xc0\x0c" + struct.pack(">HHIH", 1, 1, 60, 4) + bytes([93,184,216,34])
    s.sendto(resp, addr)
