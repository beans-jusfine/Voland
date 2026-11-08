#!/usr/bin/env python3
"""Generate an animated 96x54 VFB2 square-and-circle calibration NRO."""
import struct
from pathlib import Path

W, H = 96, 54
TEXT_OFF = 0x1000
INSTRUCTIONS = 18000
CHUNK_PIXELS = 4096
HEADER_SIZE = 20


def movz_w(imm):
    return 0x52800000 | ((imm & 0xffff) << 5)


def movk_w16(imm):
    return 0x72a00000 | ((imm & 0xffff) << 5)


def movz_w_reg(rd, imm):
    return 0x52800000 | ((imm & 0xffff) << 5) | rd


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
        if (x == 78 and 4 <= y <= 49) or (y == 27 and 72 <= x <= 92):
            r, g, b = 90, 255, 120
        pixels.append(rgba(r, g, b))

data_size = HEADER_SIZE + W * H * 4
text_size = INSTRUCTIONS * 4
data_off = (TEXT_OFF + text_size + 0xfff) & ~0xfff
page_delta = (data_off - TEXT_OFF) // 0x1000
code = [adrp_x(20, page_delta), add_x_imm(20, 20, 0), add_x_imm(1, 20, HEADER_SIZE)]
for index, pixel in enumerate(pixels):
    code.extend((movz_w(pixel), movk_w16(pixel >> 16), str_w(index % CHUNK_PIXELS)))
    if index % CHUNK_PIXELS == CHUNK_PIXELS - 1 and index + 1 < W * H:
        code.append(add_x_imm(1, 1, 4, shift12=True))
# Publish the initial calibration image only after all pixels are written.
code.extend((movz_w(1), 0xb9001280))  # STR W0, [X20, #16]

# Animate a visible 5x5 marker near the upper-right corner. The guest waits
# until the host clears frame_ready, toggles the marker color, and republishes.
marker_x, marker_y, marker_size = 84, 3, 5
marker_index = marker_y * W + marker_x
code.extend((add_x_imm(1, 20, HEADER_SIZE + marker_index * 4),
             movz_w_reg(10, 0), movz_w_reg(11, 1)))

wait_index = len(code)
code.append(0xb9400000 | (4 << 10) | (20 << 5) | 9)  # LDR W9, [X20, #16]
cbnz_index = len(code)
code.append(0x35000000 | (((wait_index - cbnz_index) & 0x7ffff) << 5) | 9)
code.append(0x4a000000 | (11 << 16) | (10 << 5) | 10)  # EOR W10, W10, W11
color_branch = len(code)
code.append(0)  # CBNZ W10, color_one
color_zero = rgba(0, 220, 255)
color_one = rgba(255, 45, 180)
code.extend((movz_w(color_zero), movk_w16(color_zero >> 16)))
skip_color_one = len(code)
code.append(0)  # B store_marker
color_one_index = len(code)
code.extend((movz_w(color_one), movk_w16(color_one >> 16)))
store_marker_index = len(code)
code[color_branch] = 0x35000000 | (((color_one_index - color_branch) & 0x7ffff) << 5) | 10
code[skip_color_one] = 0x14000000 | ((store_marker_index - skip_color_one) & 0x3ffffff)
for dy in range(marker_size):
    for dx in range(marker_size):
        code.append(str_w(dy * W + dx))
code.extend((movz_w(1), 0xb9001280))  # STR W0, [X20, #16]
branch_index = len(code)
code.append(0x14000000 | ((wait_index - branch_index) & 0x3ffffff))

if len(code) > INSTRUCTIONS:
    raise RuntimeError("animated calibration demo exceeds instruction budget")
code.extend([0xd503201f] * (INSTRUCTIONS - len(code)))

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
put32(data_off, 0x32424656)  # VFB2
put32(data_off + 4, W)
put32(data_off + 8, H)
put32(data_off + 12, 1)  # RGBA8
put32(data_off + 16, 0)  # frame_ready, set by guest after drawing
cursor = TEXT_OFF
for instruction in code:
    put32(cursor, instruction)
    cursor += 4
Path("platform/web/public/homebrew/voland-calibration.nro").write_bytes(image)
print(f"Wrote {file_size:,} bytes: {W}x{H} VFB2 calibration image.")
