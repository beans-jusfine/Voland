#ifndef SWITCH_HLE_KERNEL_SCHEDULER_H
#define SWITCH_HLE_KERNEL_SCHEDULER_H
#include <stdbool.h>
#include <stdint.h>
#include "common/result.h"
#include "cpu/cpu.h"
#define SCHEDULER_VIRTUAL_CORE_COUNT ((uint32_t)3)
#define SCHEDULER_MAX_THREADS ((uint32_t)512)
#define SCHEDULER_QUANTUM_CYCLES UINT64_C(10000)
#define SCHEDULER_ALL_CORE_AFFINITY UINT64_C(0x7)
typedef enum Thread_Run_State { THREAD_RUN_CREATED=0, THREAD_RUN_RUNNABLE, THREAD_RUN_WAITING, THREAD_RUN_SLEEPING, THREAD_RUN_DEAD } Thread_Run_State;
typedef struct Scheduler_Thread {
 CPU_State *cpu_state; uint64_t id, affinity_mask, wake_at_vt, last_scheduled_round, cycles_executed;
 uint32_t priority; int32_t ideal_core; uint32_t current_core;
 Thread_Run_State run_state; CPU_ExitReason last_exit_reason;
} Scheduler_Thread;
typedef struct Scheduler {
 const CPU_Backend *backend; Scheduler_Thread threads[SCHEDULER_MAX_THREADS];
 uint32_t thread_count, next_slot, tie_cursor[SCHEDULER_VIRTUAL_CORE_COUNT];
 uint64_t next_id, round, virtual_time_cycles;
 bool paused;
} Scheduler;
typedef struct Scheduler_Tick_Result {
 uint32_t cores_run, idle_cores, faulted_threads; uint64_t guest_cycles_executed, virtual_time_cycles;
 CPU_ExitReason last_exit_reason; bool did_work;
} Scheduler_Tick_Result;
Error scheduler_init(Scheduler *out,const CPU_Backend *backend);
Error scheduler_add_thread(Scheduler *scheduler,CPU_State *state,uint32_t priority,uint32_t ideal_core,uint64_t affinity_mask,uint64_t *out_id);
Error scheduler_set_thread_state(Scheduler *scheduler,uint64_t id,Thread_Run_State state);
Error scheduler_sleep_thread(Scheduler *scheduler,uint64_t id,uint64_t duration_cycles);
Error scheduler_remove_thread(Scheduler *scheduler,uint64_t id);
/* Pausing freezes guest execution and virtual time; thread state is preserved. */
Error scheduler_set_paused(Scheduler *scheduler,bool paused);
bool scheduler_is_paused(const Scheduler *scheduler);
Scheduler_Tick_Result scheduler_tick(Scheduler *scheduler);
Scheduler_Tick_Result scheduler_tick_budget(Scheduler *scheduler,uint64_t cycle_budget);
const Scheduler_Thread *scheduler_find_thread(const Scheduler *scheduler,uint64_t id);
#endif
