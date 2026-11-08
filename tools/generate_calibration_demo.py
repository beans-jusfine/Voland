#!/usr/bin/env python3
"""Generate a static 96x54 square-and-circle calibration framebuffer NRO."""
import struct
from pathlib import Path

W, H = 96, 54
TEXT_OFF = 0x1000
INSTRUCTIONS = 18000
CHUNK_PIXELS = 4096
HEADER_SIZE = 16


def movz_w(imm):
    return 0x52800000 | ((imm & 0xffff) << 5)


def movk_w16(imm):
    return 0x72a00000 | ((imm & 0xffff) << 5)


def str_w(index):
    return 0xb9000000 | ((index & 0xfff) << 10) | (1 << 5)


def adrp_x(reg, page_delta):
    imm = page_delta & 0x1fffff
    return 0x90000000 | ((imm & 3) << 29) | (((imm >> 2) & 0x7ffff) << 5) | reg


def add_x_imm(rd, rn, imm12, shift12=False):
    return 0x91000000 | (int(shift12) << 22) | ((imm12 & 0xfff) << 10) | (rn << 5) | rd


def rgba(r, g, b):
    return (255 << 24) | (b << 16) | (g << 8) | r


pixels = []
squares = [(4, 4, 10, (255, 35, 180)), (23, 4, 14, (0, 225, 255)), (46, 4, 18, (255, 205, 0))]
circles = [(11, 36, 5, (255, 35, 180)), (30, 36, 7, (0, 225, 255)), (55, 36, 9, (255, 205, 0))]
for y in range(H):
    for x in range(W):
        r, g, b = 10, 13, 24
        if x % 6 == 0 or y % 6 == 0:
            r, g, b = 25, 30, 48
        for sx, sy, side, color in squares:
            if sx <= x < sx + side and sy <= y < sy + side:
                r, g, b = color
                if x in (sx, sx + side - 1) or y in (sy, sy + side - 1):
                    r, g, b = 255, 255, 255
        for cx, cy, radius, color in circles:
            distance = (x - cx) ** 2 + (y - cy) ** 2
            if radius * radius - radius <= distance <= radius * radius + radius:
                r, g, b = color
        if x == 78 and 4 <= y <= 49 or y == 27 and 72 <= x <= 92:
            r, g, b = 90, 255, 120
        pixels.append(rgba(r, g, b))

data_size = HEADER_SIZE + W * H * 4
text_size = INSTRUCTIONS * 4
data_off = (TEXT_OFF + text_size + 0xfff) & ~0xfff
page_delta = (data_off - TEXT_OFF) // 0x1000
code = [adrp_x(20, page_delta), add_x_imm(20, 20, HEADER_SIZE), 0xaa1403e1]
for index, pixel in enumerate(pixels):
    code.extend((movz_w(pixel), movk_w16(pixel >> 16), str_w(index % CHUNK_PIXELS)))
    if index % CHUNK_PIXELS == CHUNK_PIXELS - 1 and index + 1 < W * H:
        code.append(add_x_imm(1, 1, 4, shift12=True))
if len(code) + 1 > INSTRUCTIONS:
    raise RuntimeError("calibration frame exceeds instruction budget")
code.extend([0xd503201f] * (INSTRUCTIONS - len(code) - 1))
code.append(0x14000000)  # branch to itself after publishing the static image

file_size = data_off + data_size
image = bytearray(file_size)


def put32(offset, value):
    struct.pack_into("<I", image, offset, value & 0xffffffff)


put32(0x10, 0x304f524e)  # NRO0
put32(0x18, file_size)
put32(0x20, TEXT_OFF)
put32(0x24, text_size)
put32(0x30, data_off)
put32(0x34, data_size)
put32(data_off, 0x31424656)  # VFB1
put32(data_off + 4, W)
put32(data_off + 8, H)
put32(data_off + 12, 1)  # RGBA8
cursor = TEXT_OFF
for instruction in code:
    put32(cursor, instruction)
    cursor += 4
Path("platform/web/public/homebrew/voland-calibration.nro").write_bytes(image)
print(f"Wrote {file_size:,} bytes: {W}x{H} square-and-circle calibration image.")
