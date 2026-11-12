#include "hle/hle.h"
#include "common/assert.h"
#include "common/log.h"
#include "hle/kernel/svc_ipc.h"
#include "hle/kernel/svc_memory.h"
#include "hle/kernel/thread.h"

#include <stddef.h>

void hle_context_init(HLE_Context *context, const CPU_Backend *backend,
                      VMM_Context *vmm, Process *process, Page_Allocator *pages)
{
  SWITCH_ASSERT_ALWAYS(context != NULL, "hle_context_init: context is NULL");
  SWITCH_ASSERT_ALWAYS(backend != NULL, "hle_context_init: backend is NULL");
  context->cpu_backend = backend;
  context->vmm = vmm;
  context->process = process;
  context->pages = pages;
  context->scheduler = NULL;
  context->svc_call_count = 0;
  /* Unbound until the owner binds it (emulator.c binds sm's object);
   * ConnectToNamedPort on an unbound object answers NOT_IMPLEMENTED
   * through ipc_dispatch rather than crashing. */
  context->sm_object = (Service_Object){.interface = NULL, .instance = NULL};
}

static Guest_Thread *thread_for_state(Process *process, CPU_State *state) {
  if (!process || !state) return NULL;
  if (process->main_thread.cpu_state == state) return &process->main_thread;
  for (uint32_t i = 0; i < PROCESS_MAX_CHILD_THREADS; i++) {
    Guest_Thread *thread = &process->child_threads[i];
    if (thread->cpu_state == state) return thread;
  }
  return NULL;
}

static void hle_svc_exit_thread(HLE_Context *context, CPU_State *cpu_state) {
  if (!context->process || !context->scheduler ||
      !npdm_svc_allowed(&context->process->npdm, 0x0A)) return;
  Guest_Thread *thread = thread_for_state(context->process, cpu_state);
  if (!thread || !thread->scheduler_id) return;
  (void)scheduler_set_thread_state(context->scheduler, thread->scheduler_id,
                                   THREAD_RUN_DEAD);
}

/* Virtual time is one retired instruction per cycle. The design's exact
 * nanosecond conversion is cycles = ns * 51 / 50; round up so a positive
 * sleep never wakes before its requested deadline. */
static uint64_t sleep_ns_to_cycles(uint64_t ns) {
  const uint64_t whole = ns / 50;
  const uint64_t remainder = ns % 50;
  if (whole > UINT64_MAX / 51) return UINT64_MAX;
  const uint64_t base = whole * 51;
  const uint64_t tail = (remainder * 51 + 49) / 50;
  return UINT64_MAX - base < tail ? UINT64_MAX : base + tail;
}

static void hle_svc_sleep_thread(HLE_Context *context, CPU_State *cpu_state) {
  if (!context->process || !context->scheduler ||
      !npdm_svc_allowed(&context->process->npdm, 0x0B)) return;
  Guest_Thread *thread = thread_for_state(context->process, cpu_state);
  if (!thread || !thread->scheduler_id) return;
  const int64_t timeout_ns =
      (int64_t)context->cpu_backend->get_register_file(cpu_state)->x[0];
  if (timeout_ns == 0 || timeout_ns == -1 || timeout_ns == -2) {
    const Scheduler_Yield_Mode mode = timeout_ns == -1
        ? SCHEDULER_YIELD_MIGRATE
        : (timeout_ns == -2 ? SCHEDULER_YIELD_ANY : SCHEDULER_YIELD_SAME_CORE);
    (void)scheduler_yield_thread(context->scheduler, thread->scheduler_id, mode);
    return;
  }
  if (timeout_ns < 0) return;
  uint64_t cycles = sleep_ns_to_cycles((uint64_t)timeout_ns);
  const uint64_t available = UINT64_MAX - context->scheduler->virtual_time_cycles;
  if (cycles > available) cycles = available;
  if (!cycles) return;
  (void)scheduler_sleep_thread(context->scheduler, thread->scheduler_id, cycles);
}

static uint32_t thread_create_result(Error err) {
  switch (err.code) {
  case RESULT_OUT_OF_MEMORY: return HLE_RESULT_OUT_OF_MEMORY;
  case RESULT_INVALID_ARGUMENT: return HLE_RESULT_OUT_OF_RANGE;
  default: return HLE_RESULT_INVALID_HANDLE;
  }
}

static void hle_svc_create_thread(HLE_Context *context, CPU_State *cpu_state) {
  CPU_Register_File *regs = context->cpu_backend->get_register_file(cpu_state);
  if (!context->process || !context->scheduler ||
      !npdm_svc_allowed(&context->process->npdm, 0x08)) {
    regs->x[0] = HLE_RESULT_NOT_IMPLEMENTED;
    return;
  }
  if (regs->x[4] > THREAD_PRIORITY_LOWEST ||
      regs->x[5] >= SCHEDULER_VIRTUAL_CORE_COUNT) {
    regs->x[0] = HLE_RESULT_OUT_OF_RANGE;
    return;
  }
  const uint64_t entry = regs->x[1];
  const uint64_t argument = regs->x[2];
  const uint64_t stack_top = regs->x[3];
  const uint32_t priority = (uint32_t)regs->x[4];
  const uint32_t core = (uint32_t)regs->x[5];
  const NPDM_Kernel_Capabilities *caps = &context->process->npdm.capabilities;
  if (caps->has_thread_info &&
      (priority < caps->highest_priority || priority > caps->lowest_priority ||
       core < caps->min_core || core > caps->max_core)) {
    regs->x[0] = HLE_RESULT_OUT_OF_RANGE;
    return;
  }
  Guest_Thread *slot = NULL;
  for (uint32_t i = 0; i < PROCESS_MAX_CHILD_THREADS; i++) {
    if (!context->process->child_threads[i].cpu_state) {
      slot = &context->process->child_threads[i];
      break;
    }
  }
  if (!slot) {
    regs->x[0] = HLE_RESULT_OUT_OF_MEMORY;
    return;
  }
  const Thread_Env env = {
      .backend = context->cpu_backend, .vmm = context->vmm,
      .tls = &context->process->tls, .hle = context,
  };
  const Thread_Create_Params params = {
      .entry_point = entry, .argument = argument, .stack_top = stack_top,
      .priority = priority, .preferred_core = core,
  };
  Error err = thread_create(&env, &params, slot);
  if (!error_is_ok(err)) {
    regs->x[0] = thread_create_result(err);
    return;
  }
  uint32_t handle = 0;
  err = handle_table_insert(&context->process->handles, KERNEL_OBJECT_THREAD,
                            slot, &handle);
  if (!error_is_ok(err)) {
    thread_destroy(&env, slot);
    regs->x[0] = HLE_RESULT_OUT_OF_MEMORY;
    return;
  }
  regs->x[0] = HLE_RESULT_SUCCESS;
  regs->x[1] = handle;
}

static void hle_svc_start_thread(HLE_Context *context, CPU_State *cpu_state) {
  CPU_Register_File *regs = context->cpu_backend->get_register_file(cpu_state);
  if (!context->process || !context->scheduler ||
      !npdm_svc_allowed(&context->process->npdm, 0x09)) {
    regs->x[0] = HLE_RESULT_NOT_IMPLEMENTED;
    return;
  }
  void *object = NULL;
  const Error lookup = handle_table_get(&context->process->handles,
      (uint32_t)regs->x[0], KERNEL_OBJECT_THREAD, &object);
  if (!error_is_ok(lookup) || !object) {
    regs->x[0] = HLE_RESULT_INVALID_HANDLE;
    return;
  }
  Guest_Thread *thread = (Guest_Thread *)object;
  if (!thread->owns_cpu_state || !thread->cpu_state || thread->scheduler_id) {
    regs->x[0] = HLE_RESULT_INVALID_HANDLE;
    return;
  }
  uint64_t scheduler_id = 0;
  const Error add = scheduler_add_thread(context->scheduler, thread->cpu_state,
      thread->priority, thread->preferred_core, SCHEDULER_ALL_CORE_AFFINITY,
      &scheduler_id);
  if (!error_is_ok(add)) {
    regs->x[0] = add.code == RESULT_OUT_OF_MEMORY
                     ? HLE_RESULT_OUT_OF_MEMORY : HLE_RESULT_INVALID_HANDLE;
    return;
  }
  thread->scheduler_id = scheduler_id;
  regs->x[0] = HLE_RESULT_SUCCESS;
}

static const char *svc_name(uint32_t swi)
{
  switch (swi)
  {
  case 0x01:
    return "SetHeapSize";
  case 0x02:
    return "SetMemoryPermission";
  case 0x03:
    return "SetMemoryAttribute";
  case 0x04:
    return "MapMemory";
  case 0x05:
    return "UnmapMemory";
  case 0x06:
    return "QueryMemory";
  case 0x08:
    return "CreateThread";
  case 0x09:
    return "StartThread";
  case 0x0A:
    return "ExitThread";
  case 0x0B:
    return "SleepThread";
  case 0x0C:
    return "GetThreadPriority";
  case 0x11:
    return "SignalEvent";
  case 0x12:
    return "ClearEvent";
  case 0x13:
    return "MapSharedMemory";
  case 0x14:
    return "UnmapSharedMemory";
  case 0x15:
    return "CreateTransferMemory";
  case 0x16:
    return "CloseHandle";
  case 0x18:
    return "WaitSynchronization";
  case 0x19:
    return "CancelSynchronization";
  case 0x1A:
    return "ArbitrateLock";
  case 0x1B:
    return "ArbitrateUnlock";
  case 0x1C:
    return "WaitProcessWideKeyAtomic";
  case 0x1D:
    return "SignalProcessWideKey";
  case 0x1F:
    return "ConnectToNamedPort";
  case 0x21:
    return "SendSyncRequest";
  case 0x22:
    return "SendSyncRequestWithUserBuffer";
  case 0x26:
    return "Break";
  case 0x29:
    return "GetInfo";
  default:
    return "<unknown>";
  }
}

void hle_on_svc(CPU_State *cpu_state, uint32_t swi, void *userdata)
{
  HLE_Context *context = (HLE_Context *)userdata;
  SWITCH_ASSERT_ALWAYS(context != NULL, "hle_on_svc: context is NULL");
  SWITCH_ASSERT_ALWAYS(context->cpu_backend != NULL, "hle_on_svc: backend is NULL");

  const CPU_Backend *cpu = context->cpu_backend;
  CPU_Register_File *regs = cpu->get_register_file(cpu_state);

  context->svc_call_count++;

  /* Handlers below that call vmm_guest_to_host bracket each individual
   * call in its own vmm_borrow_scope_begin/end, right where it happens
   * (svc_memory.c) - NOT one scope around this whole dispatch. A
   * dispatch-wide scope was tried first and immediately hit vmm's debug
   * borrow cap (VMM_DEBUG_MAX_BORROWS=64, vmm.c): SetHeapSize's shrink
   * path can call vmm_guest_to_host once per released page, hundreds of
   * times in one call, and the cap exists to catch a handler that LEAKS
   * borrows, not to size a loop's borrow budget. Scoping tightly around
   * each call keeps at most one borrow live at a time regardless of how
   * many pages a handler walks. */

  /* §12's dispatch table, Phase 1 slice: the four memory SVCs
   * (hle/kernel/svc_memory.h) plus the three IPC SVCs this checkbox
   * implements (hle/kernel/svc_ipc.h), plus the §12 unimplemented-surface
   * policy default arm for everything else: log, then
   * HLE_RESULT_NOT_IMPLEMENTED in W0. */
  switch (swi)
  {
  case 0x01:
    hle_svc_set_heap_size(context, cpu_state);
    break;
  case 0x04:
    hle_svc_map_memory(context, cpu_state);
    break;
  case 0x05:
    hle_svc_unmap_memory(context, cpu_state);
    break;
  case 0x06:
    hle_svc_query_memory(context, cpu_state);
    break;
  case 0x08:
    hle_svc_create_thread(context, cpu_state);
    break;
  case 0x09:
    hle_svc_start_thread(context, cpu_state);
    break;
  case 0x0A:
    hle_svc_exit_thread(context, cpu_state);
    break;
  case 0x0B:
    hle_svc_sleep_thread(context, cpu_state);
    break;
  case 0x16:
    hle_svc_close_handle(context, cpu_state);
    break;
  case 0x1F:
    hle_svc_connect_to_named_port(context, cpu_state);
    break;
  case 0x21:
    hle_svc_send_sync_request(context, cpu_state);
    break;
  default:
    log_warn("[hle] SVC 0x%02x (%s) at PC 0x%016llx - unimplemented",
             swi,
             svc_name(swi),
             (unsigned long long)regs->pc);
    regs->x[0] = HLE_RESULT_NOT_IMPLEMENTED;
    break;
  }
}

void hle_on_undefined(CPU_State *cpu_state, uint32_t instruction, void *userdata)
{
  HLE_Context *context = (HLE_Context *)userdata;
  const uint64_t pc = context && context->cpu_backend
                          ? context->cpu_backend->get_pc(cpu_state)
                          : 0;
  log_error("[hle] undefined instruction 0x%08x at PC 0x%016llx",
            instruction,
            (unsigned long long)pc);
}
