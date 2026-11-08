#include "common/layout.h"
#include "common/log.h"
#include "emulator.h"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define EXPORT
#endif
#define VOLAND_FB_MAGIC 0x31424656u /* 'VFB1' */
#define VOLAND_FB_HEADER_BYTES 16u
#define DEMO_FRAME_WIDTH 32u
#define DEMO_FRAME_HEIGHT 32u
#define DEMO_FRAME_BYTES (DEMO_FRAME_WIDTH * DEMO_FRAME_HEIGHT * 4u)
#define FRAMEBUFFER_HEADER_BYTES 128u
#define FRAMEBUFFER_MAX_WIDTH 1920u
#define FRAMEBUFFER_MAX_HEIGHT 1080u
#define FRAMEBUFFER_SLOT_BYTES ((uint64_t)FRAMEBUFFER_MAX_WIDTH * FRAMEBUFFER_MAX_HEIGHT * 4u)
EXPORT int emulator_create_ffi(void);
EXPORT void emulator_destroy_ffi(void);
EXPORT int emulator_run_ffi(uint64_t cycle_budget);
EXPORT int emulator_step_ffi(void);
EXPORT int emulator_set_paused_ffi(int paused);
EXPORT int emulator_load_nro_ffi(uintptr_t data_ptr, uint32_t size);
EXPORT uint32_t emulator_get_last_nro_error_ffi(uintptr_t out_ptr, uint32_t capacity);
EXPORT uint64_t cpu_get_reg_ffi(uint32_t index);
EXPORT void cpu_set_reg_ffi(uint32_t index, uint64_t value);
EXPORT uint64_t cpu_get_pc_ffi(void);
EXPORT int cpu_backend_id_ffi(void);
EXPORT uintptr_t layout_get_ffi(void);
static Emulator g_emulator;
static int g_initialised = 0;
EXPORT int emulator_create_ffi(void) {
  if (g_initialised) return 1;
  const Error err = emulator_create(&g_emulator);
  if (err.code != RESULT_OK) { log_error("[wasm_entry] emulator_create failed: %s", err.message); return 0; }
  g_initialised = 1; return 1;
}
EXPORT void emulator_destroy_ffi(void) {
  if (!g_initialised) return;
  emulator_destroy(&g_emulator); g_initialised = 0;
}
/* Guest framebuffer ABI: a 16-byte VFB1 descriptor at the start of the
 * first module's .data segment: magic, width, height, format (1 = RGBA8).
 * Pixels immediately follow the descriptor. Legacy 32x32 pixel demos remain
 * readable while homebrew migrates to the descriptor. */
static void publish_guest_frame_if_ready(void) {
  if (!g_initialised || !g_emulator.program_loaded ||
      g_emulator.process.module_count == 0) return;
  const Process_Module *module = &g_emulator.process.modules[0];
  uint32_t width = DEMO_FRAME_WIDTH;
  uint32_t height = DEMO_FRAME_HEIGHT;
  uint32_t format = 1u;
  uint64_t pixel_offset = 0;
  if (module->data.size >= VOLAND_FB_HEADER_BYTES) {
    uint32_t descriptor[4];
    const Error header_read = vmm_read_block(g_emulator.vmm, module->data.base,
                                             descriptor, sizeof(descriptor));
    if (header_read.code != RESULT_OK) return;
    if (descriptor[0] == VOLAND_FB_MAGIC) {
      width = descriptor[1];
      height = descriptor[2];
      format = descriptor[3];
      pixel_offset = VOLAND_FB_HEADER_BYTES;
    }
  }
  if (width == 0 || width > FRAMEBUFFER_MAX_WIDTH ||
      height == 0 || height > FRAMEBUFFER_MAX_HEIGHT || format != 1u) return;
  const uint64_t byte_length = (uint64_t)width * height * 4u;
  if (byte_length > FRAMEBUFFER_SLOT_BYTES ||
      pixel_offset > module->data.size ||
      byte_length > module->data.size - pixel_offset) return;

  const Memory_Layout *layout = layout_get();
  if (!layout) return;
  uint8_t *region = (uint8_t *)(uintptr_t)layout->framebuffer_slot_base;
  _Atomic uint32_t *publish = (_Atomic uint32_t *)(void *)region;
  const uint32_t current = atomic_load_explicit(publish, memory_order_acquire);
  const uint32_t next = current + 1u;
  const uint32_t slot = (next - 1u) & 1u;
  uint32_t *metadata = (uint32_t *)(void *)(region + 8u + slot * 16u);
  uint8_t *pixels = region + FRAMEBUFFER_HEADER_BYTES + (uint64_t)slot * FRAMEBUFFER_SLOT_BYTES;
  const Error read = vmm_read_block(g_emulator.vmm, module->data.base + pixel_offset,
                                    pixels, byte_length);
  if (read.code != RESULT_OK) {
    log_error("[wasm_entry] could not read guest framebuffer: %s", read.message);
    return;
  }
  /* Only publish a genuinely changed guest image. The web FPS counter is
   * based on publication IDs, so unchanged scheduler ticks must not count. */
  if (current != 0u) {
    const uint32_t previous_slot = (current - 1u) & 1u;
    const uint8_t *previous = region + FRAMEBUFFER_HEADER_BYTES +
                              (uint64_t)previous_slot * FRAMEBUFFER_SLOT_BYTES;
    if (memcmp(previous, pixels, (size_t)byte_length) == 0) return;
  }
  metadata[0] = width;
  metadata[1] = height;
  metadata[2] = format;
  metadata[3] = (uint32_t)byte_length;
  atomic_store_explicit(publish, next, memory_order_release);
}

EXPORT int emulator_run_ffi(uint64_t cycle_budget) {
  if (!g_initialised) return -1;
  const int reason = (int)emulator_run(&g_emulator, cycle_budget);
  publish_guest_frame_if_ready();
  return reason;
}
EXPORT int emulator_set_paused_ffi(int paused) {
  if (!g_initialised) return 0;
  const Error err = emulator_set_paused(&g_emulator, paused != 0);
  if (err.code != RESULT_OK) { log_error("[wasm_entry] scheduler pause change failed: %s", err.message); return 0; }
  return 1;
}
static const char *g_last_nro_error = "";
EXPORT uint32_t emulator_get_last_nro_error_ffi(uintptr_t out_ptr, uint32_t capacity) {
  if (!out_ptr || capacity == 0) return 0;
  uint8_t *out = (uint8_t *)out_ptr;
  uint32_t length = 0;
  while (g_last_nro_error[length] != '\0' && length + 1u < capacity) {
    out[length] = (uint8_t)g_last_nro_error[length];
    length++;
  }
  out[length] = 0;
  return length;
}
EXPORT int emulator_load_nro_ffi(uintptr_t data_ptr, uint32_t size) {
  g_last_nro_error = "";
  if (!g_initialised || data_ptr == 0 || size == 0) {
    g_last_nro_error = "NRO load rejected: core is not initialised or input is empty";
    return RESULT_INVALID_ARGUMENT;
  }
  const Byte_Source source = byte_source_from_memory((const void *)data_ptr, size);
  const Error err = emulator_load_nro(&g_emulator, &source, 0);
  if (err.code != RESULT_OK) {
    g_last_nro_error = err.message ? err.message : "NRO loader returned an error without a message";
    log_error("[wasm_entry] NRO load failed (%d): %s", (int)err.code, g_last_nro_error);
    return (int)err.code;
  }
  return RESULT_OK;
}
EXPORT int emulator_step_ffi(void) {
  if (!g_initialised) return -1;
  return (int)emulator_step(&g_emulator);
}
EXPORT uint64_t cpu_get_reg_ffi(uint32_t index) {
  if (!g_initialised) return 0;
  return g_emulator.cpu_backend->get_reg(g_emulator.cpu_state, (uint8_t)index);
}
EXPORT void cpu_set_reg_ffi(uint32_t index, uint64_t value) {
  if (g_initialised) g_emulator.cpu_backend->set_reg(g_emulator.cpu_state, (uint8_t)index, value);
}
EXPORT uint64_t cpu_get_pc_ffi(void) {
  return g_initialised ? g_emulator.cpu_backend->get_pc(g_emulator.cpu_state) : 0;
}
EXPORT int cpu_backend_id_ffi(void) {
#if defined(SWITCH_CPU_BACKEND_INTERPRETER)
  return 1;
#elif defined(SWITCH_CPU_BACKEND_NOOP)
  return 0;
#else
  return -1;
#endif
}
EXPORT uintptr_t layout_get_ffi(void) { return (uintptr_t)layout_get(); }
#ifndef __EMSCRIPTEN__
int main(void) {
  log_info("[wasm_entry] native build, running smoke test");
  Emulator emu; const Error err = emulator_create(&emu);
  if (err.code != RESULT_OK) { log_error("emulator_create failed: %s", err.message); return 1; }
  const CPU_ExitReason reason = emulator_run(&emu, 1000);
  log_info("[wasm_entry] emulator_run exit reason=%d", (int)reason);
  emulator_destroy(&emu); return 0;
}
#endif
