#define CHECK_NAME "interpreter_test"
#include "check.h"
#include "common/layout.h"
#include "common/vmm.h"
#include "cpu/cpu.h"

#include <stdint.h>

#define CODE_GVA UINT64_C(0x10000)
#define DATA_GVA (CODE_GVA + UINT64_C(0x200))

typedef struct Test_Callbacks {
  uint32_t svc_number;
  uint32_t svc_calls;
  uint32_t undefined_instruction;
  uint32_t undefined_calls;
} Test_Callbacks;

static void on_svc(CPU_State *state, uint32_t swi, void *userdata) {
  (void)state;
  Test_Callbacks *callbacks = (Test_Callbacks *)userdata;
  callbacks->svc_number = swi;
  callbacks->svc_calls++;
}

static void on_undefined(CPU_State *state, uint32_t instruction, void *userdata) {
  (void)state;
  Test_Callbacks *callbacks = (Test_Callbacks *)userdata;
  callbacks->undefined_instruction = instruction;
  callbacks->undefined_calls++;
}

int main(void) {
  CHECK_OK(layout_create());
  CHECK_OK(layout_guest_ram_commit(0, VMM_PAGE_SIZE));

  VMM_Context *vmm = vmm_create();
  CHECK(vmm != NULL);
  CHECK_OK(vmm_map(vmm, CODE_GVA, 0, VMM_PAGE_SIZE, VMM_PERM_RWX));

  /* MOVZ X0,#42; ADD X1,X0,#8; STR X1,[X2,#8]; LDR X3,[X2,#8];
   * SVC #0x1234; B . */
  const uint32_t program[] = {
    UINT32_C(0xd2800540),
    UINT32_C(0x91002001),
    UINT32_C(0xf9000441),
    UINT32_C(0xf9400443),
    UINT32_C(0xd4024681),
    UINT32_C(0x14000000),
  };
  CHECK_OK(vmm_write_block(vmm, CODE_GVA, program, sizeof(program)));

  Test_Callbacks callbacks = {0};
  const CPU_Backend *backend = &CPU_BACKEND_INTERPRETER;
  CPU_State *state = backend->create(vmm, &callbacks);
  CHECK(state != NULL);
  backend->set_svc_handler(state, on_svc);
  backend->set_undefined_handler(state, on_undefined);
  backend->set_pc(state, CODE_GVA);
  backend->set_reg(state, 2, DATA_GVA);

  CHECK(backend->run(state, 4) == CPU_EXIT_CYCLES_ELAPSED);
  CHECK(backend->get_cycles_consumed(state) == 4);
  CHECK(backend->get_reg(state, 0) == 42);
  CHECK(backend->get_reg(state, 1) == 50);
  CHECK(backend->get_reg(state, 3) == 50);
  uint64_t stored = 0;
  CHECK_OK(vmm_read64(vmm, DATA_GVA + 8, &stored));
  CHECK(stored == 50);

  CHECK(backend->run(state, 1) == CPU_EXIT_SVC);
  CHECK(callbacks.svc_calls == 1);
  CHECK(callbacks.svc_number == 0x1234);
  CHECK(backend->get_pc(state) == CODE_GVA + 20);

  CHECK(backend->run(state, 1) == CPU_EXIT_CYCLES_ELAPSED);
  CHECK(backend->get_pc(state) == CODE_GVA + 20);
  CHECK(backend->get_cycles_consumed(state) == 1);

  /* Unsupported encodings must report the exact instruction and fault
   * instead of silently advancing the guest PC. */
  const uint32_t invalid = UINT32_C(0xffffffff);
  CHECK_OK(vmm_write32(vmm, CODE_GVA + 0x100, invalid));
  backend->set_pc(state, CODE_GVA + 0x100);
  CHECK(backend->run(state, 1) == CPU_EXIT_FAULT);
  CHECK(callbacks.undefined_calls == 1);
  CHECK(callbacks.undefined_instruction == invalid);
  CHECK(backend->get_pc(state) == CODE_GVA + 0x100);

  /* Instruction fetches through an unmapped guest VA fault through vmm. */
  backend->set_pc(state, CODE_GVA + VMM_PAGE_SIZE);
  CHECK(backend->run(state, 1) == CPU_EXIT_FAULT);
  CHECK(backend->get_fault_address(state) == CODE_GVA + VMM_PAGE_SIZE);

  backend->destroy(state);
  CHECK_OK(vmm_unmap(vmm, CODE_GVA, VMM_PAGE_SIZE));
  vmm_destroy(vmm);
  layout_destroy();

  printf("[" CHECK_NAME "] OK\n");
  return 0;
}
