#!/usr/bin/env python3
import os, struct, zlib, sys

out = sys.argv[1] if len(sys.argv) > 1 else 'build/jackets'
count = int(sys.argv[2]) if len(sys.argv) > 2 else 16
w = h = 256
os.makedirs(out, exist_ok=True)

def chunk(tag, data):
    return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff)

for n in range(count):
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        for x in range(w):
            # High-frequency but deterministic test art: gradients + rings.
            dx, dy = x - 128, y - 128
            ring = ((dx*dx + dy*dy) // 256 + n * 23) & 255
            r = (x + n * 37 + ring) & 255
            g = (y * 2 + n * 19) & 255
            b = ((x ^ y) + n * 53) & 255
            raw += bytes((r, g, b, 255))
    png = b'\x89PNG\r\n\x1a\n'
    png += chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(bytes(raw), 6))
    png += chunk(b'IEND', b'')
    with open(os.path.join(out, f'{n}.png'), 'wb') as f:
        f.write(png)
print(f'generated {count} streaming jackets in {out}')
