#!/usr/bin/env python3
"""Generate a 128x72, 100-particle framebuffer stress-test NRO for Voland."""
import math
import struct
from pathlib import Path

W, H = 128, 72
TEXT_OFF = 0x1000
FRAME_INSTRUCTIONS = 32_000
FB_HEADER_SIZE = 16
DATA_SIZE = FB_HEADER_SIZE + W * H * 4
FRAME_COUNT = 24
PARTICLE_COUNT = 100


def align(value, alignment):
    return (value + alignment - 1) & ~(alignment - 1)


def movz_w(imm):
    return 0x52800000 | ((imm & 0xffff) << 5)


def movk_w16(imm):
    return 0x72a00000 | ((imm & 0xffff) << 5)


def str_w(byte_off):
    return 0xb9000000 | (((byte_off // 4) & 0xfff) << 10) | (1 << 5)


def adrp_x1(page_delta):
    imm = page_delta & 0x1fffff
    return 0x90000001 | ((imm & 3) << 29) | (((imm >> 2) & 0x7ffff) << 5)


def rgba(r, g, b):
    return (255 << 24) | (b << 16) | (g << 8) | r


def render_frame(frame):
    pixels = []
    centers = []
    for i in range(PARTICLE_COUNT):
        phase = i * 2.399963229728653
        speed = 0.65 + (i % 9) * 0.075
        x = W / 2 + math.sin(frame * 0.19 * speed + phase) * (W * (0.18 + (i % 5) * 0.025))
        y = H / 2 + math.cos(frame * 0.13 * speed + phase * 1.7) * (H * (0.20 + (i % 7) * 0.022))
        centers.append((x, y, i))
    for y in range(H):
        for x in range(W):
            grid = 4 if x % 16 == 0 or y % 12 == 0 else 0
            r = 4 + x * 10 // W + grid
            g = 7 + y * 12 // H + grid
            b = 20 + ((x + y) % 24) + grid * 2
            for px, py, i in centers:
                dx, dy = x - px, y - py
                d2 = dx * dx + dy * dy
                radius = 1.7 + (i % 3) * 0.45
                if d2 <= radius * radius:
                    strength = max(0.0, 1.0 - d2 / (radius * radius + 0.01))
                    r = min(255, r + int((80 + (i * 31) % 150) * strength))
                    g = min(255, g + int((100 + (i * 17) % 145) * strength))
                    b = min(255, b + int((130 + (i * 13) % 125) * strength))
            pixels.append(rgba(r, g, b))
    return pixels


frame_blocks = []
for frame in range(FRAME_COUNT):
    instructions = []
    if frame == 0:
        instructions.extend((0, 0))  # ADRP X1; ADD X1, X1, #16
    for index, pixel in enumerate(render_frame(frame)):
        instructions.extend((movz_w(pixel), movk_w16(pixel >> 16),
                             str_w(FB_HEADER_SIZE + index * 4)))
    if len(instructions) + 1 > FRAME_INSTRUCTIONS:
        raise RuntimeError("frame drawing code exceeds its instruction budget")
    instructions.extend([0xd503201f] * (FRAME_INSTRUCTIONS - len(instructions) - 1))
    instructions.append(0)
    frame_blocks.append(instructions)

TEXT_SIZE = FRAME_COUNT * FRAME_INSTRUCTIONS * 4
DATA_OFF = align(TEXT_OFF + TEXT_SIZE, 0x1000)
page_delta = (DATA_OFF - TEXT_OFF) // 0x1000
frame_blocks[0][0] = adrp_x1(page_delta)
frame_blocks[0][1] = 0x91004021  # ADD X1, X1, #16

for frame, block in enumerate(frame_blocks):
    branch_word = frame * FRAME_INSTRUCTIONS + FRAME_INSTRUCTIONS - 1
    target_word = ((frame + 1) % FRAME_COUNT) * FRAME_INSTRUCTIONS
    block[-1] = 0x14000000 | ((target_word - branch_word) & 0x03ffffff)

file_size = DATA_OFF + DATA_SIZE
image = bytearray(file_size)


def put32(offset, value):
    struct.pack_into("<I", image, offset, value & 0xffffffff)


put32(0x10, 0x304f524e)  # NRO0
put32(0x18, file_size)
put32(0x20, TEXT_OFF)
put32(0x24, TEXT_SIZE)
put32(0x30, DATA_OFF)
put32(0x34, DATA_SIZE)
put32(DATA_OFF, 0x31424656)  # VFB1
put32(DATA_OFF + 4, W)
put32(DATA_OFF + 8, H)
put32(DATA_OFF + 12, 1)  # RGBA8

cursor = TEXT_OFF
for block in frame_blocks:
    for instruction in block:
        put32(cursor, instruction)
        cursor += 4

Path("platform/web/public/homebrew/voland-particle-storm.nro").write_bytes(image)
print(f"Wrote {file_size:,} bytes: {FRAME_COUNT} frames, {PARTICLE_COUNT} particles, {W}x{H} RGBA8.")
