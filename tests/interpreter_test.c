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
  CHECK_OK(vmm_map(vmm, CODE_GVA, 0, VMM_PAGE_SIZE, VMM_PERM_ALL));

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

  /* Logical shifted-register and arithmetic shift coverage. */
  const uint32_t logic_program[] = {
    UINT32_C(0xaa020020), /* ORR X0, X1, X2 */
    UINT32_C(0xca020023), /* EOR X3, X1, X2 */
    UINT32_C(0xea020024), /* ANDS X4, X1, X2 */
    UINT32_C(0xaa020425), /* ORR X5, X1, X2, LSL #1 */
    UINT32_C(0xaa420826), /* ORR X6, X1, X2, LSR #2 */
    UINT32_C(0xaa820c27), /* ORR X7, X1, X2, ASR #3 */
  };
  CHECK_OK(vmm_write_block(vmm, CODE_GVA + 0x40, logic_program, sizeof(logic_program)));
  backend->set_reg(state, 1, UINT64_C(0x800000000000000f));
  backend->set_reg(state, 2, UINT64_C(0x0000000000000003));
  backend->set_pc(state, CODE_GVA + 0x40);
  CHECK(backend->run(state, 6) == CPU_EXIT_CYCLES_ELAPSED);
  CHECK(backend->get_reg(state, 0) == UINT64_C(0x800000000000000f));
  CHECK(backend->get_reg(state, 3) == UINT64_C(0x800000000000000c));
  CHECK(backend->get_reg(state, 4) == UINT64_C(0x3));
  CHECK(backend->get_reg(state, 5) == UINT64_C(0x800000000000000f));
  CHECK(backend->get_reg(state, 6) == UINT64_C(0x800000000000000f));
  CHECK(backend->get_reg(state, 7) == UINT64_C(0x800000000000000f));

  /* ADD/SUB shifted-register forms and NZCV flag behavior. */
  const uint32_t arithmetic_program[] = {
    UINT32_C(0x8b020828), /* ADD X8, X1, X2, LSL #2 */
    UINT32_C(0xcb420429), /* SUB X9, X1, X2, LSR #1 */
    UINT32_C(0xab02002a), /* ADDS X10, X1, X2 */
    UINT32_C(0xeb02002b), /* SUBS X11, X1, X2 */
  };
  CHECK_OK(vmm_write_block(vmm, CODE_GVA + 0x80, arithmetic_program, sizeof(arithmetic_program)));
  backend->set_reg(state, 1, 100);
  backend->set_reg(state, 2, 7);
  backend->set_pc(state, CODE_GVA + 0x80);
  CHECK(backend->run(state, 4) == CPU_EXIT_CYCLES_ELAPSED);
  CHECK(backend->get_reg(state, 8) == 128);
  CHECK(backend->get_reg(state, 9) == 97);
  CHECK(backend->get_reg(state, 10) == 107);
  CHECK(backend->get_reg(state, 11) == 93);
  CHECK((backend->get_pstate(state) & 0xf0000000u) == 0x20000000u);

  /* MADD/MSUB: 64-bit multiply-accumulate and 32-bit result truncation. */
  const uint32_t multiply_program[] = {
    UINT32_C(0x9b020c20), /* MADD X0, X1, X2, X3 */
    UINT32_C(0x9b028c24), /* MSUB X4, X1, X2, X3 */
    UINT32_C(0x1b020c25), /* MADD W5, W1, W2, W3 */
  };
  CHECK_OK(vmm_write_block(vmm, CODE_GVA + 0xa0, multiply_program, sizeof(multiply_program)));
  backend->set_reg(state, 1, UINT64_C(0x100000002));
  backend->set_reg(state, 2, 3);
  backend->set_reg(state, 3, 10);
  backend->set_pc(state, CODE_GVA + 0xa0);
  CHECK(backend->run(state, 3) == CPU_EXIT_CYCLES_ELAPSED);
  CHECK(backend->get_reg(state, 0) == UINT64_C(0x300000010));
  CHECK(backend->get_reg(state, 4) == UINT64_C(0xfffffffd00000004));
  CHECK(backend->get_reg(state, 5) == 16);

  /* UBFM extracts unsigned fields; SBFM sign-extends the selected field. */
  const uint32_t bitfield_program[] = {
    UINT32_C(0xd348fc20), /* UBFM X0, X1, #8, #63 (LSR #8) */
    UINT32_C(0x93401c62), /* SBFM X2, X3, #0, #7 (sign-extend byte) */
    UINT32_C(0x53001ca4), /* UBFM W4, W5, #0, #7 */
  };
  CHECK_OK(vmm_write_block(vmm, CODE_GVA + 0xb0, bitfield_program, sizeof(bitfield_program)));
  backend->set_reg(state, 1, UINT64_C(0x123456789abcdef0));
  backend->set_reg(state, 3, UINT64_C(0xf0));
  backend->set_reg(state, 5, UINT64_C(0x1234));
  backend->set_pc(state, CODE_GVA + 0xb0);
  CHECK(backend->run(state, 3) == CPU_EXIT_CYCLES_ELAPSED);
  CHECK(backend->get_reg(state, 0) == UINT64_C(0x00123456789abcde));
  CHECK(backend->get_reg(state, 2) == UINT64_C(0xfffffffffffffff0));
  CHECK(backend->get_reg(state, 4) == UINT64_C(0x34));

  /* A subtraction underflow sets N and clears C. */
  const uint32_t underflow_program[] = { UINT32_C(0xeb02002b) };
  CHECK_OK(vmm_write_block(vmm, CODE_GVA + 0xc0, underflow_program, sizeof(underflow_program)));
  backend->set_reg(state, 1, 0);
  backend->set_reg(state, 2, 1);
  backend->set_pc(state, CODE_GVA + 0xc0);
  CHECK(backend->step(state) == CPU_EXIT_CYCLES_ELAPSED);
  CHECK((backend->get_pstate(state) & 0xa0000000u) == 0x80000000u);

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
