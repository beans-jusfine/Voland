#define CHECK_NAME "nro_execution_test"
#include "check.h"
#include "common/layout.h"
#include "common/vmm.h"
#include "cpu/cpu.h"
#include "hle/kernel/page_allocator.h"
#include "hle/kernel/process.h"
#include "hle/kernel/scheduler.h"
#include "hle/loader/byte_source.h"
#include "hle/loader/nro.h"

#include <stdint.h>
#include <string.h>

#define TEST_NRO_SIZE UINT32_C(0x10c)

static void put32(uint8_t *p, uint32_t value) {
  p[0] = (uint8_t)value;
  p[1] = (uint8_t)(value >> 8);
  p[2] = (uint8_t)(value >> 16);
  p[3] = (uint8_t)(value >> 24);
}

int main(void) {
  /* Generate a tiny valid homebrew NRO: MOVZ X0,#42; ADD X1,X0,#8; B . */
  uint8_t image[TEST_NRO_SIZE] = {0};
  put32(image + NRO_HEADER_OFFSET, NRO_MAGIC);
  put32(image + NRO_HEADER_OFFSET + 8, TEST_NRO_SIZE);
  put32(image + NRO_HEADER_OFFSET + 16, 0x1000);
  put32(image + NRO_HEADER_OFFSET + 20, 12);
  /* Keep the segment's memory offset page-aligned while its file payload
   * follows the NRO header. */
  uint8_t *large_image = image;
  (void)large_image;
  uint8_t nro_bytes[0x100c] = {0};
  put32(nro_bytes + NRO_HEADER_OFFSET, NRO_MAGIC);
  put32(nro_bytes + NRO_HEADER_OFFSET + 8, sizeof(nro_bytes));
  put32(nro_bytes + NRO_HEADER_OFFSET + 16, 0x1000);
  put32(nro_bytes + NRO_HEADER_OFFSET + 20, 12);
  const uint32_t instructions[] = {
      UINT32_C(0xd2800540), UINT32_C(0x91002001), UINT32_C(0x14000000),
  };
  memcpy(nro_bytes + 0x1000, instructions, sizeof(instructions));

  Byte_Source source = byte_source_from_memory(nro_bytes, sizeof(nro_bytes));
  NRO nro;
  CHECK_OK(nro_open(&source, &nro));
  CHECK(nro.image_size == VMM_PAGE_SIZE * 2);

  CHECK_OK(layout_create());
  Page_Allocator pages;
  CHECK_OK(page_allocator_init_guest_ram(&pages));
  VMM_Context *vmm = vmm_create();
  CHECK(vmm != NULL);

  Process process;
  CHECK_OK(process_bootstrap_nro(&nro, vmm, &pages, 0, &process));
  CHECK(process.module_count == 1);
  CHECK(process.entry_point == process.address_space.code.base + 0x1000);
  CHECK(process.main_thread_stack.size == 0x40000);
  CHECK(tls_is_allocated(&process.tls, process.main_thread_tls_gva));

  const CPU_Backend *backend = &CPU_BACKEND_INTERPRETER;
  CPU_State *cpu = backend->create(vmm, NULL);
  CHECK(cpu != NULL);
  CHECK_OK(process_enter_main_thread(&process, backend, cpu));
  Scheduler scheduler;
  uint64_t thread_id = 0;
  CHECK_OK(scheduler_init(&scheduler, backend));
  CHECK_OK(scheduler_add_thread(&scheduler, cpu, 30, 0,
                                SCHEDULER_ALL_CORE_AFFINITY, &thread_id));
  Scheduler_Tick_Result tick = scheduler_tick_budget(&scheduler, 2);
  CHECK(tick.did_work);
  CHECK(tick.guest_cycles_executed == 2);
  CHECK(backend->get_reg(cpu, 0) == 42);
  CHECK(backend->get_reg(cpu, 1) == 50);
  CHECK(backend->get_pc(cpu) == process.entry_point + 8);

  CHECK_OK(scheduler_remove_thread(&scheduler, thread_id));
  backend->destroy(cpu);
  process_teardown(&process, vmm, &pages);
  vmm_destroy(vmm);
  page_allocator_reset(&pages);
  layout_destroy();

  printf("[" CHECK_NAME "] OK\n");
  return 0;
}
