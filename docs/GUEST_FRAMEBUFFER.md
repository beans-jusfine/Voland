# Guest framebuffer ABI

Voland's current web presenter accepts frames published by guest homebrew through
the first loaded module's writable `.data` segment. This is a small bring-up ABI
for testing the guest-to-WebGPU path; it is not yet the Switch's native NVN GPU
command interface.

## VFB1 descriptor

The first 16 bytes of `.data` are four little-endian unsigned 32-bit words:

| Offset | Field | Value |
|---:|---|---|
| 0 | Magic | `0x31424656` (ASCII `VFB1` in little-endian memory) |
| 4 | Width | 1–1920 pixels |
| 8 | Height | 1–1080 pixels |
| 12 | Format | `1` = RGBA8 |

The tightly packed RGBA8 pixel array begins immediately at byte offset 16.
Each pixel is four bytes in red, green, blue, alpha order. The descriptor and
pixel array must both fit inside the module's `.data` segment. Voland rejects
zero dimensions, unsupported formats, dimensions above the current 1920×1080
limit, and truncated pixel arrays.

The guest may update the pixels whenever it draws. Voland samples the buffer
after each CPU execution slice and publishes a completed copy through the
shared double-buffered framebuffer region. The WebGPU worker reads the published
width, height, format and byte length from slot metadata, resizes its texture
when needed, and presents the frame.

## Compatibility

The original 32×32 pixel and animation demos predate VFB1. Until regenerated
with the updated scripts, Voland recognizes a non-VFB1 `.data` segment as the
legacy 32×32 RGBA8 demo layout. New homebrew should always use VFB1.

## Demo generators

- `python tools/generate_pixel_demo.py`
- `python tools/generate_animation_demo.py`

Both scripts now emit a VFB1 descriptor and place guest-generated pixels after
the 16-byte header.
