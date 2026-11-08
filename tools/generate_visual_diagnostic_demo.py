#!/usr/bin/env python3
"""Generate a high-contrast moving-shapes framebuffer diagnostic NRO for Voland."""
import struct
from pathlib import Path

W, H = 96, 54
FRAMES = 16
FRAME_INSTRUCTIONS = 30_000
CHUNK_PIXELS = 4096
TEXT_OFF = 0x1000
HEADER_SIZE = 16


def align(value, alignment):
    return (value + alignment - 1) & ~(alignment - 1)


def movz_w(imm):
    return 0x52800000 | ((imm & 0xffff) << 5)


def movk_w16(imm):
    return 0x72a00000 | ((imm & 0xffff) << 5)


def str_w(index):
    if not 0 <= index < CHUNK_PIXELS:
        raise ValueError("STR offset exceeds scaled 12-bit immediate")
    return 0xb9000000 | ((index & 0xfff) << 10) | (1 << 5)


def adrp_x(reg, page_delta):
    imm = page_delta & 0x1fffff
    return 0x90000000 | ((imm & 3) << 29) | (((imm >> 2) & 0x7ffff) << 5) | reg


def add_x_imm(rd, rn, imm12, shift12=False):
    return 0x91000000 | (int(shift12) << 22) | ((imm12 & 0xfff) << 10) | (rn << 5) | rd


def rgba(r, g, b):
    return (255 << 24) | (b << 16) | (g << 8) | r


def render_frame(frame):
    pixels = []
    red_x = (frame * 5) % (W - 22)
    cyan_y = 5 + (frame * 3) % (H - 18)
    yellow_x = (frame * 7) % (W - 10)
    for y in range(H):
        for x in range(W):
            r, g, b = 7 + x % 9, 10 + y % 10, 27 + (x + y) % 12
            if x % 12 == 0 or y % 9 == 0:
                r, g, b = r + 5, g + 7, b + 10
            if red_x <= x < red_x + 22 and 7 <= y < 29:
                r, g, b = ((255, 255, 255) if x in (red_x, red_x + 21) or y in (7, 28)
                           else (255, 25, 190))
            if 66 <= x < 88 and cyan_y <= y < cyan_y + 18:
                r, g, b = ((255, 255, 255) if x in (66, 87) or y in (cyan_y, cyan_y + 17)
                           else (0, 235, 255))
            if yellow_x <= x < yellow_x + 10 and 37 <= y < 50:
                r, g, b = 255, 220, 0
            if 3 <= x < 9 and 3 <= y < 9:
                r, g, b = 30, 255, 70
            pixels.append(rgba(r, g, b))
    return pixels


data_size = HEADER_SIZE + W * H * 4
text_size = FRAMES * FRAME_INSTRUCTIONS * 4
data_off = align(TEXT_OFF + text_size, 0x1000)
page_delta = (data_off - TEXT_OFF) // 0x1000
blocks = []
for frame in range(FRAMES):
    code = []
    if frame == 0:
        code.extend((adrp_x(20, page_delta), add_x_imm(20, 20, HEADER_SIZE)))
    code.append(0xaa1403e1)  # MOV X1, X20
    for index, pixel in enumerate(render_frame(frame)):
        code.extend((movz_w(pixel), movk_w16(pixel >> 16), str_w(index % CHUNK_PIXELS)))
        if index % CHUNK_PIXELS == CHUNK_PIXELS - 1 and index + 1 < W * H:
            code.append(add_x_imm(1, 1, 4, shift12=True))
    if len(code) + 1 > FRAME_INSTRUCTIONS:
        raise RuntimeError("diagnostic frame exceeds instruction budget")
    code.extend([0xd503201f] * (FRAME_INSTRUCTIONS - len(code) - 1))
    code.append(0)
    blocks.append(code)

for frame, code in enumerate(blocks):
    branch_word = frame * FRAME_INSTRUCTIONS + FRAME_INSTRUCTIONS - 1
    target_word = ((frame + 1) % FRAMES) * FRAME_INSTRUCTIONS
    code[-1] = 0x14000000 | ((target_word - branch_word) & 0x03ffffff)

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
for code in blocks:
    for instruction in code:
        put32(cursor, instruction)
        cursor += 4
Path("platform/web/public/homebrew/voland-visual-diagnostic.nro").write_bytes(image)
print(f"Wrote {file_size:,} bytes: {FRAMES} frames, high-contrast moving shapes, {W}x{H} RGBA8.")
