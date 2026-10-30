#!/usr/bin/env python3
# tiny BMP(32bpp) -> PNG converter with integer upscale, no dependencies
import sys, struct, zlib
def conv(src, dst, scale=2):
    d = open(src, 'rb').read()
    off = struct.unpack_from('<I', d, 10)[0]
    w, h = struct.unpack_from('<ii', d, 18)
    bpp = struct.unpack_from('<H', d, 28)[0]
    flip = h > 0; h = abs(h); bp = bpp // 8
    stride = (w * bp + 3) & ~3
    rows = []
    for y in range(h):
        sy = h - 1 - y if flip else y
        r = d[off + sy * stride: off + sy * stride + w * bp]
        px = bytearray()
        for x in range(w):
            b, g, rr = r[x*bp], r[x*bp+1], r[x*bp+2]
            px += bytes((rr, g, b)) * scale
        for _ in range(scale): rows.append(b'\x00' + bytes(px))
    W, H = w * scale, h * scale
    def chunk(t, c): return struct.pack('>I', len(c)) + t + c + struct.pack('>I', zlib.crc32(t + c) & 0xffffffff)
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(b''.join(rows), 9)) + chunk(b'IEND', b'')
    open(dst, 'wb').write(png)
if __name__ == '__main__':
    conv(sys.argv[1], sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else 2)
