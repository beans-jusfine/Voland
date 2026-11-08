#define CHECK_NAME "scheduler_test"
#include "check.h"
#include "common/layout.h"
#include "common/vmm.h"
#include "cpu/cpu.h"
#include "hle/kernel/scheduler.h"
int main(void){
 CHECK_OK(layout_create());VMM_Context *vmm=vmm_create();CHECK(vmm);const CPU_Backend *b=cpu_get_active_backend();CHECK(b);
 CPU_State *a=b->create(vmm,NULL),*c=b->create(vmm,NULL),*d=b->create(vmm,NULL);CHECK(a&&c&&d);
 Scheduler s;CHECK_OK(scheduler_init(&s,b));uint64_t ai=0,ci=0,di=0;
 CHECK_OK(scheduler_add_thread(&s,a,20,0,7,&ai));CHECK_OK(scheduler_add_thread(&s,c,10,1,7,&ci));CHECK_OK(scheduler_add_thread(&s,d,30,2,7,&di));CHECK(s.thread_count==3);
 CHECK_OK(scheduler_set_paused(&s,true));CHECK(scheduler_is_paused(&s));Scheduler_Tick_Result paused=scheduler_tick(&s);CHECK(!paused.did_work&&paused.cores_run==0&&paused.idle_cores==3);CHECK(paused.virtual_time_cycles==0);CHECK_OK(scheduler_set_paused(&s,false));CHECK(!scheduler_is_paused(&s));Scheduler_Tick_Result r=scheduler_tick(&s);CHECK(r.did_work&&r.cores_run==3&&r.idle_cores==0);CHECK(r.guest_cycles_executed==3*SCHEDULER_QUANTUM_CYCLES);CHECK(r.virtual_time_cycles==SCHEDULER_QUANTUM_CYCLES);
 CHECK(scheduler_find_thread(&s,ai)->cycles_executed==SCHEDULER_QUANTUM_CYCLES);CHECK(scheduler_find_thread(&s,ci)->cycles_executed==SCHEDULER_QUANTUM_CYCLES);
 CHECK_OK(scheduler_sleep_thread(&s,di,SCHEDULER_QUANTUM_CYCLES));r=scheduler_tick(&s);CHECK(r.cores_run==2);CHECK(scheduler_find_thread(&s,di)->run_state==THREAD_RUN_SLEEPING);r=scheduler_tick(&s);CHECK(scheduler_find_thread(&s,di)->run_state==THREAD_RUN_RUNNABLE);
 CHECK_OK(scheduler_set_thread_state(&s,ai,THREAD_RUN_WAITING));CHECK_OK(scheduler_set_thread_state(&s,ci,THREAD_RUN_DEAD));r=scheduler_tick(&s);CHECK(r.cores_run==1&&r.idle_cores==2);
 CHECK_OK(scheduler_remove_thread(&s,ai));CHECK(s.thread_count==2);CHECK(!scheduler_find_thread(&s,ai));
 CHECK_CODE(scheduler_add_thread(&s,d,1,0,0,&ai),RESULT_INVALID_ARGUMENT);CHECK_CODE(scheduler_add_thread(&s,d,1,0,1,&ai),RESULT_ALREADY_EXISTS);CHECK_CODE(scheduler_remove_thread(&s,ai),RESULT_NOT_FOUND);
 b->destroy(a);b->destroy(c);b->destroy(d);vmm_destroy(vmm);layout_destroy();printf("[" CHECK_NAME "] ok\n");return 0;
}
