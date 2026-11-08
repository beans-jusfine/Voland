#include "hle/kernel/scheduler.h"
#include <string.h>
static Scheduler_Thread *find_mutable(Scheduler *s,uint64_t id){if(!s||!id)return NULL;for(uint32_t i=0;i<SCHEDULER_MAX_THREADS;i++)if(s->threads[i].cpu_state&&s->threads[i].id==id)return &s->threads[i];return NULL;}
const Scheduler_Thread *scheduler_find_thread(const Scheduler *s,uint64_t id){if(!s||!id)return NULL;for(uint32_t i=0;i<SCHEDULER_MAX_THREADS;i++)if(s->threads[i].cpu_state&&s->threads[i].id==id)return &s->threads[i];return NULL;}
Error scheduler_init(Scheduler *out,const CPU_Backend *backend){if(!out||!backend||!backend->run||!backend->get_cycles_consumed)return ERR(RESULT_INVALID_ARGUMENT,"scheduler_init: invalid argument or backend");memset(out,0,sizeof(*out));out->backend=backend;out->next_id=1;return OK;}
Error scheduler_add_thread(Scheduler *s,CPU_State *state,uint32_t priority,uint32_t ideal,uint64_t mask,uint64_t *out_id){
 if(!s||!s->backend||!state||!out_id||priority>63||ideal>=3||!mask||(mask&~SCHEDULER_ALL_CORE_AFFINITY)||(mask&(UINT64_C(1)<<ideal))==0)return ERR(RESULT_INVALID_ARGUMENT,"scheduler_add_thread: invalid argument");
 for(uint32_t i=0;i<SCHEDULER_MAX_THREADS;i++)if(s->threads[i].cpu_state==state)return ERR(RESULT_ALREADY_EXISTS,"scheduler_add_thread: CPU state already registered");
 uint32_t slot=SCHEDULER_MAX_THREADS;for(uint32_t n=0;n<SCHEDULER_MAX_THREADS;n++){uint32_t k=(s->next_slot+n)%SCHEDULER_MAX_THREADS;if(!s->threads[k].cpu_state){slot=k;break;}}
 if(slot==SCHEDULER_MAX_THREADS)return ERR(RESULT_OUT_OF_MEMORY,"scheduler_add_thread: thread table full");
 Scheduler_Thread *t=&s->threads[slot];memset(t,0,sizeof(*t));t->cpu_state=state;t->id=s->next_id++;if(!t->id)t->id=s->next_id++;t->priority=priority;t->ideal_core=(int32_t)ideal;t->affinity_mask=mask;t->current_core=ideal;t->run_state=THREAD_RUN_RUNNABLE;t->last_exit_reason=CPU_EXIT_CYCLES_ELAPSED;s->thread_count++;s->next_slot=(slot+1)%SCHEDULER_MAX_THREADS;*out_id=t->id;return OK;
}
Error scheduler_set_thread_state(Scheduler *s,uint64_t id,Thread_Run_State state){Scheduler_Thread *t=find_mutable(s,id);if(!t||state<THREAD_RUN_CREATED||state>THREAD_RUN_DEAD)return ERR(RESULT_INVALID_ARGUMENT,"scheduler_set_thread_state: unknown thread or state");t->run_state=state;return OK;}
Error scheduler_sleep_thread(Scheduler *s,uint64_t id,uint64_t duration){Scheduler_Thread *t=find_mutable(s,id);if(!t||!duration||UINT64_MAX-s->virtual_time_cycles<duration)return ERR(RESULT_INVALID_ARGUMENT,"scheduler_sleep_thread: invalid thread or duration");t->wake_at_vt=s->virtual_time_cycles+duration;t->run_state=THREAD_RUN_SLEEPING;return OK;}
Error scheduler_remove_thread(Scheduler *s,uint64_t id){Scheduler_Thread *t=find_mutable(s,id);if(!t)return ERR(RESULT_NOT_FOUND,"scheduler_remove_thread: thread not found");memset(t,0,sizeof(*t));s->thread_count--;return OK;}
Error scheduler_set_paused(Scheduler *s,bool paused){if(!s||!s->backend)return ERR(RESULT_INVALID_ARGUMENT,"scheduler_set_paused: invalid scheduler");s->paused=paused;return OK;}
bool scheduler_is_paused(const Scheduler *s){return s&&s->paused;}
static Scheduler_Thread *pick_thread(Scheduler *s,uint32_t core){Scheduler_Thread *best=NULL;uint32_t best_slot=0;for(uint32_t n=0;n<SCHEDULER_MAX_THREADS;n++){uint32_t slot=(s->tie_cursor[core]+n)%SCHEDULER_MAX_THREADS;Scheduler_Thread *t=&s->threads[slot];if(!t->cpu_state||t->run_state!=THREAD_RUN_RUNNABLE||t->last_scheduled_round==s->round||!(t->affinity_mask&(UINT64_C(1)<<core)))continue;bool candidate_ideal=t->ideal_core==(int32_t)core;bool best_ideal=best&&best->ideal_core==(int32_t)core;if(!best||(candidate_ideal&&!best_ideal)||(candidate_ideal==best_ideal&&t->priority<best->priority)){best=t;best_slot=slot;}}if(best)s->tie_cursor[core]=(best_slot+1)%SCHEDULER_MAX_THREADS;return best;}

Scheduler_Tick_Result scheduler_tick_budget(Scheduler *s,uint64_t cycle_budget){
 Scheduler_Tick_Result r;memset(&r,0,sizeof(r));r.last_exit_reason=CPU_EXIT_CYCLES_ELAPSED;
 if(!s||!s->backend||!cycle_budget)return r;
 r.virtual_time_cycles=s->virtual_time_cycles;
 if(s->paused){r.idle_cores=SCHEDULER_VIRTUAL_CORE_COUNT;return r;}
 s->round++;if(!s->round){s->round=1;for(uint32_t i=0;i<SCHEDULER_MAX_THREADS;i++)s->threads[i].last_scheduled_round=0;}
 for(uint32_t i=0;i<SCHEDULER_MAX_THREADS;i++){
  Scheduler_Thread *t=&s->threads[i];
  if(t->cpu_state&&t->run_state==THREAD_RUN_SLEEPING&&t->wake_at_vt<=s->virtual_time_cycles){t->run_state=THREAD_RUN_RUNNABLE;t->wake_at_vt=0;}
 }
 for(uint32_t core=0;core<SCHEDULER_VIRTUAL_CORE_COUNT;core++){
  Scheduler_Thread *t=pick_thread(s,core);
  if(!t){r.idle_cores++;continue;}
  r.cores_run++;t->last_scheduled_round=s->round;t->current_core=core;
  uint64_t remaining=cycle_budget;
  while(remaining&&t->run_state==THREAD_RUN_RUNNABLE){
   CPU_ExitReason why=s->backend->run(t->cpu_state,remaining);
   uint64_t used=s->backend->get_cycles_consumed(t->cpu_state);
   t->last_exit_reason=why;r.last_exit_reason=why;r.did_work=true;
   t->cycles_executed+=used;r.guest_cycles_executed+=used;
   if(used>=remaining)remaining=0;else remaining-=used;
   if(why==CPU_EXIT_FAULT||why==CPU_EXIT_HALT){
    t->run_state=THREAD_RUN_DEAD;if(why==CPU_EXIT_FAULT)r.faulted_threads++;break;
   }
   if(why==CPU_EXIT_BREAKPOINT||why==CPU_EXIT_CYCLES_ELAPSED||!used||
      (why==CPU_EXIT_SVC&&t->run_state!=THREAD_RUN_RUNNABLE))break;
  }
 }
 if(UINT64_MAX-s->virtual_time_cycles>=cycle_budget)s->virtual_time_cycles+=cycle_budget;
 r.virtual_time_cycles=s->virtual_time_cycles;
 if(r.idle_cores==SCHEDULER_VIRTUAL_CORE_COUNT)r.did_work=false;
 return r;
}
Scheduler_Tick_Result scheduler_tick(Scheduler *s){return scheduler_tick_budget(s,SCHEDULER_QUANTUM_CYCLES);}
