#!/usr/bin/env python3
"""Generate Voland's guest-pixel NRO smoke demo (32x32 RGBA8)."""
import math
import struct
from pathlib import Path

W = H = 32
TEXT_OFF = 0x1000
DATA_OFF = 0x5000

def movz_w(imm):
    return 0x52800000 | ((imm & 0xffff) << 5)

def movk_w16(imm):
    return 0x72a00000 | ((imm & 0xffff) << 5)

def str_w(byte_off):
    return 0xb9000000 | (((byte_off // 4) & 0xfff) << 10) | (1 << 5)

instructions = [0x90000021, 0x91000021]  # ADRP X1, data; ADD X1, X1, #0
for y in range(H):
    for x in range(W):
        dx, dy = x - 15.5, y - 15.5
        ring = math.sqrt(dx * dx + dy * dy)
        checker = ((x >> 2) ^ (y >> 2)) & 1
        red, green, blue = 18 + x * 4, 28 + y * 5, 90 + checker * 35
        if ring < 12:
            red, green, blue = 25, 165 + (x + y) % 70, 245
        if 12 < ring < 14:
            red, green, blue = 255, 150, 48
        if 13 <= x <= 18 and 13 <= y <= 18:
            red, green, blue = 255, 230, 120
        rgba = (255 << 24) | (blue << 16) | (green << 8) | red
        instructions.extend((movz_w(rgba), movk_w16(rgba >> 16),
                             str_w((y * W + x) * 4)))
instructions.append(0x14000000)  # B .

text_size = len(instructions) * 4
data_size = W * H * 4
file_size = DATA_OFF + data_size
image = bytearray(file_size)
def put32(offset, value):
    struct.pack_into("<I", image, offset, value)
put32(0x10, 0x304f524e)  # NRO0
put32(0x18, file_size)
put32(0x20, TEXT_OFF); put32(0x24, text_size)
put32(0x30, DATA_OFF); put32(0x34, data_size)
for i, instruction in enumerate(instructions):
    put32(TEXT_OFF + i * 4, instruction)
Path("platform/web/public/homebrew/voland-pixels-demo.nro").write_bytes(image)
print(f"Wrote {file_size} bytes; guest code draws {W}x{H} RGBA8 pixels.")
