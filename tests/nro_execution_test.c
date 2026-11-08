#define CHECK_NAME "nro_execution_test"
#include "check.h"
#include "common/layout.h"
#include "common/vmm.h"
#include "cpu/cpu.h"
#include "hle/kernel/page_allocator.h"
#include "hle/kernel/scheduler.h"
#include "hle/loader/byte_source.h"
#include "hle/loader/nro.h"

#include <stdint.h>
#include <string.h>

#define TEST_CODE_GVA UINT64_C(0x08000000)
#define TEST_NRO_SIZE UINT32_C(0x10c)

static void put32(uint8_t *p, uint32_t value) {
  p[0] = (uint8_t)value;
  p[1] = (uint8_t)(value >> 8);
  p[2] = (uint8_t)(value >> 16);
  p[3] = (uint8_t)(value >> 24);
}

int main(void) {
  /* A minimal, generated NRO: MOVZ X0,#42; ADD X1,X0,#8; B . */
  uint8_t image[TEST_NRO_SIZE] = {0};
  put32(image + NRO_HEADER_OFFSET, NRO_MAGIC);
  put32(image + NRO_HEADER_OFFSET + 8, TEST_NRO_SIZE);
  put32(image + NRO_HEADER_OFFSET + 16, 0x100);
  put32(image + NRO_HEADER_OFFSET + 20, 12);
  const uint32_t instructions[] = {
      UINT32_C(0xd2800540),
      UINT32_C(0x91002001),
      UINT32_C(0x14000000),
  };
  memcpy(image + 0x100, instructions, sizeof(instructions));

  Byte_Source source = byte_source_from_memory(image, sizeof(image));
  NRO nro;
  CHECK_OK(nro_open(&source, &nro));
  CHECK(nro.image_size == VMM_PAGE_SIZE);
  CHECK(nro.segments[NRO_SEGMENT_TEXT].size == sizeof(instructions));

  CHECK_OK(layout_create());
  Page_Allocator pages;
  CHECK_OK(page_allocator_init_guest_ram(&pages));
  uint64_t text_pa = 0;
  CHECK_OK(page_allocator_allocate(&pages, nro.image_size / VMM_PAGE_SIZE, &text_pa));

  VMM_Context *vmm = vmm_create();
  CHECK(vmm != NULL);
  CHECK_OK(vmm_map(vmm, TEST_CODE_GVA, text_pa, nro.image_size, VMM_PERM_RW));

  uint8_t text_segment[sizeof(instructions)] = {0};
  CHECK_OK(nro_read_segment(&nro, NRO_SEGMENT_TEXT, text_segment,
                            sizeof(text_segment)));
  CHECK_OK(vmm_write_block(vmm,
                           TEST_CODE_GVA + nro.segments[NRO_SEGMENT_TEXT].memory_offset,
                           text_segment, sizeof(text_segment)));
  CHECK_OK(vmm_reprotect(vmm, TEST_CODE_GVA, nro.image_size, VMM_PERM_RX));

  const CPU_Backend *backend = &CPU_BACKEND_INTERPRETER;
  CPU_State *cpu = backend->create(vmm, NULL);
  CHECK(cpu != NULL);
  backend->set_pc(cpu, TEST_CODE_GVA + nro.segments[NRO_SEGMENT_TEXT].memory_offset);

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
  CHECK(backend->get_pc(cpu) == TEST_CODE_GVA + 0x108);

  CHECK_OK(scheduler_remove_thread(&scheduler, thread_id));
  backend->destroy(cpu);
  CHECK_OK(vmm_unmap(vmm, TEST_CODE_GVA, nro.image_size));
  vmm_destroy(vmm);
  page_allocator_reset(&pages);
  layout_destroy();

  printf("[" CHECK_NAME "] OK\n");
  return 0;
}
