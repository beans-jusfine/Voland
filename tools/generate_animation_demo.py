#!/usr/bin/env python3
"""Generate a guest-driven 32x32 moving-square animation NRO for Voland."""
import struct
from pathlib import Path

W = H = 32
TEXT_OFF = 0x1000
FRAME_INSTRUCTIONS = 10_000
DATA_SIZE = W * H * 4
SQUARE = 5
# Move right, then left again. One complete frame is one 10,000-cycle
# scheduler slice (about 16 ms at Voland's current web tick interval).
POSITIONS = list(range(2, W - SQUARE)) + list(range(W - SQUARE - 2, 2, -1))
FRAME_COUNT = len(POSITIONS)


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


def rgba(red, green, blue):
    return (255 << 24) | (blue << 16) | (green << 8) | red


frame_blocks = []
for frame_index, square_x in enumerate(POSITIONS):
    instructions = []
    if frame_index == 0:
        data_page_delta = (0)  # filled after DATA_OFF is known below
        instructions.extend((0, 0))  # ADRP X1; ADD X1, X1, #0

    for y in range(H):
        for x in range(W):
            # Subtle dark-blue backdrop, with a restrained grid.
            shade = 1 if ((x // 4 + y // 4) & 1) else 0
            red, green, blue = (8 + shade * 3, 13 + shade * 4, 27 + shade * 7)
            if square_x <= x < square_x + SQUARE and 13 <= y < 13 + SQUARE:
                red, green, blue = (20, 205, 255)
                if square_x + 1 <= x < square_x + 4 and 14 <= y < 17:
                    red, green, blue = (180, 245, 255)
            pixel = rgba(red, green, blue)
            instructions.extend((movz_w(pixel), movk_w16(pixel >> 16),
                                 str_w((y * W + x) * 4)))

    # Every frame block is exactly one scheduler slice long. This makes
    # each published image a complete frame rather than a partially drawn one.
    instructions.append(0)  # branch patched after padding
    if len(instructions) > FRAME_INSTRUCTIONS:
        raise RuntimeError("frame drawing code exceeds its instruction budget")
    instructions[-1:-1] = [0xd503201f] * (FRAME_INSTRUCTIONS - len(instructions))
    frame_blocks.append(instructions)

TEXT_SIZE = FRAME_COUNT * FRAME_INSTRUCTIONS * 4
DATA_OFF = align(TEXT_OFF + TEXT_SIZE, 0x1000)
page_delta = (DATA_OFF - TEXT_OFF) // 0x1000
frame_blocks[0][0] = adrp_x1(page_delta)
frame_blocks[0][1] = 0x91000021  # ADD X1, X1, #0

# The last instruction in each block branches to the next block; the final
# block loops to the first. Branch offsets are relative to the branch PC.
for frame_index, block in enumerate(frame_blocks):
    branch_pc_word = frame_index * FRAME_INSTRUCTIONS + FRAME_INSTRUCTIONS - 1
    target_word = ((frame_index + 1) % FRAME_COUNT) * FRAME_INSTRUCTIONS
    displacement = target_word - branch_pc_word
    block[-1] = 0x14000000 | (displacement & 0x03ffffff)

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

cursor = TEXT_OFF
for block in frame_blocks:
    for instruction in block:
        put32(cursor, instruction)
        cursor += 4

Path("platform/web/public/homebrew/voland-animation-demo.nro").write_bytes(image)
print(f"Wrote {file_size} bytes: {FRAME_COUNT} guest-rendered animation frames.")
