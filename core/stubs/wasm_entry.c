#include "common/layout.h"
#include "common/log.h"
#include "emulator.h"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdatomic.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define EXPORT
#endif
#define DEMO_FRAME_WIDTH 1280u
#define DEMO_FRAME_HEIGHT 720u
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
EXPORT int emulator_publish_demo_frame_ffi(void);
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
EXPORT int emulator_run_ffi(uint64_t cycle_budget) {
  if (!g_initialised) return -1;
  return (int)emulator_run(&g_emulator, cycle_budget);
}
EXPORT int emulator_set_paused_ffi(int paused) {
  if (!g_initialised) return 0;
  const Error err = emulator_set_paused(&g_emulator, paused != 0);
  if (err.code != RESULT_OK) { log_error("[wasm_entry] scheduler pause change failed: %s", err.message); return 0; }
  return 1;
}
EXPORT int emulator_load_nro_ffi(uintptr_t data_ptr, uint32_t size) {
  if (!g_initialised || data_ptr == 0 || size == 0) return RESULT_INVALID_ARGUMENT;
  const Byte_Source source = byte_source_from_memory((const void *)data_ptr, size);
  const Error err = emulator_load_nro(&g_emulator, &source, 0);
  if (err.code != RESULT_OK) { log_error("[wasm_entry] NRO load failed (%d): %s", (int)err.code, err.message); return (int)err.code; }
  return RESULT_OK;
}
/* Host-seeded framebuffer transport test. The current demo NRO verifies ARM64
 * execution only; it does not issue GPU commands or write pixels itself. */
EXPORT int emulator_publish_demo_frame_ffi(void) {
  const Memory_Layout *layout = layout_get();
  if (!g_initialised || !layout) return 0;
  uint8_t *region = (uint8_t *)(uintptr_t)layout->framebuffer_slot_base;
  _Atomic uint32_t *publish = (_Atomic uint32_t *)(void *)region;
  const uint32_t next = atomic_load_explicit(publish, memory_order_relaxed) + 1u;
  const uint32_t slot = (next - 1u) & 1u;
  uint32_t *metadata = (uint32_t *)(void *)(region + 8u + slot * 16u);
  uint8_t *pixels = region + FRAMEBUFFER_HEADER_BYTES + (uint64_t)slot * FRAMEBUFFER_SLOT_BYTES;
  for (uint32_t y = 0; y < DEMO_FRAME_HEIGHT; ++y) {
    for (uint32_t x = 0; x < DEMO_FRAME_WIDTH; ++x) {
      const uint32_t i = (y * DEMO_FRAME_WIDTH + x) * 4u;
      const uint32_t tile = ((x / 80u) ^ (y / 60u)) & 1u;
      const int32_t dx = (int32_t)x - 360, dy = (int32_t)y - 360;
      const int32_t ex = (int32_t)x - 900, ey = (int32_t)y - 300;
      const uint32_t a = (uint32_t)(dx * dx + dy * dy) < 22500u;
      const uint32_t b = (uint32_t)(ex * ex + ey * ey) < 11025u;
      pixels[i] = a ? 24u : (b ? 190u : (uint8_t)(18u + x * 35u / DEMO_FRAME_WIDTH));
      pixels[i+1u] = a ? 164u : (b ? 64u : (uint8_t)(24u + y * 65u / DEMO_FRAME_HEIGHT));
      pixels[i+2u] = a ? 245u : (b ? 230u : (uint8_t)(tile ? 70u : 32u));
      pixels[i+3u] = 255u;
    }
  }
  metadata[0] = DEMO_FRAME_WIDTH; metadata[1] = DEMO_FRAME_HEIGHT;
  metadata[2] = 1u; /* RGBA8 */
  metadata[3] = DEMO_FRAME_WIDTH * DEMO_FRAME_HEIGHT * 4u;
  atomic_store_explicit(publish, next, memory_order_release);
  return 1;
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
