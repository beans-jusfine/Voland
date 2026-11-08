/**
 * Small, correctness-first ARMv8-A A64 interpreter.
 *
 * This is the first executable backend milestone, not a complete Switch
 * CPU: it implements common integer/control-flow and unsigned-immediate
 * load/store instructions. Unsupported encodings trap through the normal
 * undefined-instruction callback. SIMD/FP, atomics, system instructions,
 * exceptions beyond SVC, and many integer encodings remain TODO.
 */
#include "cpu/cpu.h"
#include "common/vmm.h"
#include "common/log.h"

#include <stdlib.h>
#include <string.h>

typedef struct InterpreterState {
  CPU_Register_File regs;
  VMM_Context *vmm;
  const uint64_t *l1;
  uint64_t fault_address;
  uint64_t cycles_consumed;
  uint64_t tpidrro_el0;
  CPU_SVC_Handler svc_handler;
  bool svc_seen;
  CPU_Undefined_Handler undefined_handler;
  CPU_Breakpoint_Handler breakpoint_handler;
  void *userdata;
} InterpreterState;

static uint64_t sign_extend(uint64_t value, unsigned bits) {
  const uint64_t sign = (uint64_t)1 << (bits - 1u);
  return (value ^ sign) - sign;
}
static uint64_t read_reg(InterpreterState *s, unsigned n, bool is64, bool sp) {
  uint64_t v = n == 31u ? (sp ? s->regs.sp : 0) : s->regs.x[n];
  return is64 ? v : (uint32_t)v;
}
static void write_reg(InterpreterState *s, unsigned n, uint64_t v,
                      bool is64, bool sp) {
  if (!is64) v = (uint32_t)v;
  if (n == 31u) { if (sp) s->regs.sp = v; }
  else s->regs.x[n] = v;
}
static void set_nz(InterpreterState *s, uint64_t value, bool is64) {
  const unsigned bits = is64 ? 64u : 32u;
  const uint64_t mask = is64 ? UINT64_MAX : UINT32_MAX;
  const bool n = ((value >> (bits - 1u)) & 1u) != 0;
  const bool z = (value & mask) == 0;
  s->regs.pstate = (s->regs.pstate & 0x3fffffffu) |
      (n ? 0x80000000u : 0u) | (z ? 0x40000000u : 0u);
}
static void set_nzcv_addsub(InterpreterState *s, uint64_t a,
                              uint64_t b, uint64_t result, bool is64,
                              bool subtract) {
  const unsigned sign_bit = is64 ? 63u : 31u;
  const uint64_t mask = is64 ? UINT64_MAX : UINT32_MAX;
  const uint64_t sign = UINT64_C(1) << sign_bit;
  a &= mask; b &= mask; result &= mask;
  const bool n = (result & sign) != 0;
  const bool z = result == 0;
  const bool c = subtract ? (a >= b) : (result < a);
  const bool v = subtract ? (((a ^ b) & (a ^ result) & sign) != 0)
                          : (((~(a ^ b)) & (a ^ result) & sign) != 0);
  s->regs.pstate = (s->regs.pstate & 0x0fffffffu) |
      (n ? 0x80000000u : 0u) | (z ? 0x40000000u : 0u) |
      (c ? 0x20000000u : 0u) | (v ? 0x10000000u : 0u);
}

static bool condition_passed(uint32_t flags, unsigned cond) {
  const bool n=(flags>>31)&1u, z=(flags>>30)&1u;
  const bool c=(flags>>29)&1u, v=(flags>>28)&1u;
  switch (cond) {
    case 0: return z; case 1: return !z; case 2: return c;
    case 3: return !c; case 4: return n; case 5: return !n;
    case 6: return v; case 7: return !v; case 8: return c && !z;
    case 9: return !c || z; case 10: return n == v;
    case 11: return n != v; case 12: return !z && n == v;
    case 13: return z || n != v; case 14: return true;
    default: return false;
  }
}
static bool memory_fault(InterpreterState *s, VMM_Fault *fault) {
  s->fault_address = fault->gva;
  return false;
}
static bool load_store(InterpreterState *s, uint32_t insn) {
  const unsigned size_log2 = insn >> 30;
  const unsigned bytes = 1u << size_log2;
  const bool load = ((insn >> 22) & 1u) != 0;
  const unsigned rn=(insn>>5)&31u, rt=insn&31u;
  const uint64_t address=read_reg(s,rn,true,true) +
      (((uint64_t)(insn>>10)&0xfffu) << size_log2);
  VMM_Fault fault={0};
  if (load) {
    uint64_t value=0;
    bool ok;
    if (bytes==1) { uint8_t x; ok=vmm_read8_inline(s->l1,address,&x,&fault); value=x; }
    else if (bytes==2) { uint16_t x; ok=vmm_read16_inline(s->l1,address,&x,&fault); value=x; }
    else if (bytes==4) { uint32_t x; ok=vmm_read32_inline(s->l1,address,&x,&fault); value=x; }
    else { uint64_t x; ok=vmm_read64_inline(s->l1,address,&x,&fault); value=x; }
    if (!ok) return memory_fault(s,&fault);
    write_reg(s,rt,value,bytes==8,false);
  } else {
    const uint64_t value=read_reg(s,rt,bytes==8,false);
    bool ok;
    if (bytes==1) ok=vmm_write8_inline(s->l1,address,(uint8_t)value,&fault);
    else if (bytes==2) ok=vmm_write16_inline(s->l1,address,(uint16_t)value,&fault);
    else if (bytes==4) ok=vmm_write32_inline(s->l1,address,(uint32_t)value,&fault);
    else ok=vmm_write64_inline(s->l1,address,value,&fault);
    if (!ok) return memory_fault(s,&fault);
  }
  return true;
}
static bool execute_one(InterpreterState *s) {
  VMM_Fault fault={0};
  const uint64_t pc=s->regs.pc;
  uint32_t insn=0;
  if (!vmm_read32_inline(s->l1,pc,&insn,&fault)) {
    s->fault_address=fault.gva; return false;
  }
  s->regs.pc=pc+4u;

  /* NOP */
  if (insn==0xd503201fu) return true;

  /* SVC #imm16 */
  if ((insn & 0xffe0001fu)==0xd4000001u) {
    s->svc_seen=true;
    if (s->svc_handler) s->svc_handler((CPU_State *)s,(insn>>5)&0xffffu,s->userdata);
    return true;
  }

  /* B / BL */
  if ((insn & 0x7c000000u)==0x14000000u) {
    if (insn & 0x80000000u) s->regs.x[30]=pc+4u;
    s->regs.pc=pc+sign_extend(insn&0x03ffffffu,26)*4u;
    return true;
  }

  /* B.cond */
  if ((insn & 0xff000010u)==0x54000000u) {
    if (condition_passed(s->regs.pstate,insn&15u))
      s->regs.pc=pc+sign_extend((insn>>5)&0x7ffffu,19)*4u;
    return true;
  }

  /* CBZ / CBNZ */
  if ((insn & 0x7e000000u)==0x34000000u) {
    const bool is64=(insn>>31)!=0, nonzero=((insn>>24)&1u)!=0;
    const uint64_t value=read_reg(s,insn&31u,is64,false);
    if ((value!=0)==nonzero)
      s->regs.pc=pc+sign_extend((insn>>5)&0x7ffffu,19)*4u;
    return true;
  }

  /* RET / BR */
  if ((insn & 0xfffffc1fu)==0xd65f0000u ||
      (insn & 0xfffffc1fu)==0xd61f0000u) {
    s->regs.pc=read_reg(s,(insn>>5)&31u,true,false);
    return true;
  }

  /* ADR / ADRP */
  if ((insn & 0x1f000000u)==0x10000000u) {
    const uint64_t imm=((uint64_t)((insn>>5)&0x7ffffu)<<2)|((insn>>29)&3u);
    const uint64_t offset=sign_extend(imm,21);
    const uint64_t base=(insn&(1u<<31)) ? (pc&~UINT64_C(0xfff)) : pc;
    write_reg(s,insn&31u,base+((insn&(1u<<31))?offset<<12:offset),true,false);
    return true;
  }

  /* MOVN / MOVZ / MOVK (wide immediate). */
  if ((insn & 0x1f800000u)==0x12800000u) {
    const bool is64=(insn>>31)!=0;
    const unsigned opc=(insn>>29)&3u, shift=((insn>>21)&3u)*16u, rd=insn&31u;
    if (opc==1u || shift>= (is64?64u:32u)) goto undefined;
    uint64_t imm=(uint64_t)((insn>>5)&0xffffu)<<shift;
    uint64_t old=read_reg(s,rd,is64,false), value;
    if (opc==0u) value=~imm;
    else if (opc==2u) value=imm;
    else value=(old & ~(UINT64_C(0xffff)<<shift)) | imm;
    write_reg(s,rd,value,is64,false); return true;
  }

  /* ADD / SUB (immediate), including flag-setting CMP alias. */
  if ((insn & 0x1f000000u)==0x11000000u) {
    const bool is64=(insn>>31)!=0, sub=((insn>>30)&1u)!=0, set=((insn>>29)&1u)!=0;
    const unsigned rn=(insn>>5)&31u, rd=insn&31u;
    const uint64_t imm=((uint64_t)(insn>>10)&0xfffu)<<(((insn>>22)&1u)?12u:0u);
    const uint64_t a=read_reg(s,rn,is64,true), b=sub?~imm:imm;
    const uint64_t result=sub?a-imm:a+imm;
    write_reg(s,rd,result,is64,!set);
    if (set) {
      set_nz(s,result,is64);
      const uint64_t mask=is64?UINT64_MAX:UINT32_MAX;
      const bool carry=sub?(a>=imm):((result&mask)<(a&mask));
      const bool overflow=sub?((((a^imm)&(a^result))>>(is64?63:31))&1u)!=0:
        (((~(a^imm)&(a^result))>>(is64?63:31))&1u)!=0;
      s->regs.pstate=(s->regs.pstate&0xcfffffffu)|(carry?0x20000000u:0u)|(overflow?0x10000000u:0u);
    }
    (void)b; return true;
  }

  /* ADD / SUB (shifted register), including ADDS/SUBS and CMP aliases. */
  if ((insn & 0x1f200000u)==0x0b000000u) {
    const bool is64=(insn>>31)!=0, sub=((insn>>30)&1u)!=0;
    const bool set=((insn>>29)&1u)!=0;
    const unsigned shift=(insn>>22)&3u, rm=(insn>>16)&31u;
    const unsigned amount=(insn>>10)&63u, rn=(insn>>5)&31u, rd=insn&31u;
    const unsigned width=is64?64u:32u;
    if (shift==3u || amount>=width) goto undefined;
    uint64_t right=read_reg(s,rm,is64,false);
    if (shift==0u) right <<= amount;
    else if (shift==1u) right >>= amount;
    else if (amount!=0u) {
      if (is64) right=(uint64_t)((int64_t)right >> amount);
      else right=(uint32_t)((int32_t)right >> amount);
    }
    const uint64_t left=read_reg(s,rn,is64,false);
    const uint64_t result=sub?left-right:left+right;
    write_reg(s,rd,result,is64,false);
    if (set) set_nzcv_addsub(s,left,right,result,is64,sub);
    return true;
  }

  /* Logical shifted-register: AND/ORR/EOR/ANDS and inverted forms. */
  if ((insn & 0x1f000000u)==0x0a000000u) {
    const bool is64=(insn>>31)!=0;
    const unsigned opc=(insn>>29)&3u, shift=(insn>>22)&3u;
    const bool invert=((insn>>21)&1u)!=0;
    const unsigned rm=(insn>>16)&31u, amount=(insn>>10)&63u;
    const unsigned rn=(insn>>5)&31u, rd=insn&31u;
    const unsigned width=is64?64u:32u;
    if (shift==3u || (!is64 && amount>=32u)) goto undefined;
    uint64_t right=read_reg(s,rm,is64,false);
    if (invert) right=~right;
    if (shift==0u) right <<= amount;
    else if (shift==1u) right >>= amount;
    else if (amount!=0u) {
      if (is64) right=(uint64_t)((int64_t)right >> amount);
      else right=(uint32_t)((int32_t)right >> amount);
    }
    const uint64_t left=read_reg(s,rn,is64,false);
    uint64_t result;
    if (opc==0u || opc==3u) result=left & right;
    else if (opc==1u) result=left | right;
    else result=left ^ right;
    write_reg(s,rd,result,is64,false);
    if (opc==3u) set_nz(s,result,is64);
    (void)width;
    return true;
  }

  /* LDR/STR unsigned immediate. */
  if ((insn & 0x3b000000u)==0x39000000u && ((insn>>22)&3u)<=1u)
    return load_store(s,insn);

  /* Unsupported instruction: report it instead of silently advancing. */
undefined:
  s->regs.pc=pc;
  if (s->undefined_handler) s->undefined_handler((CPU_State *)s,insn,s->userdata);
  log_warn("[cpu/interpreter] unsupported instruction 0x%08x at 0x%llx",
           insn,(unsigned long long)pc);
  return false;
}

static CPU_State *interp_create(VMM_Context *vmm, void *userdata) {
  if (!vmm) return NULL;
  InterpreterState *s=(InterpreterState *)calloc(1,sizeof(*s));
  if (!s) return NULL;
  s->vmm=vmm; s->l1=vmm_page_table_l1(vmm); s->userdata=userdata;
  return (CPU_State *)s;
}
static void interp_destroy(CPU_State *state) { free(state); }
static CPU_ExitReason interp_run(CPU_State *state,uint64_t budget) {
  InterpreterState *s=(InterpreterState *)state;
  s->cycles_consumed=0;
  while (s->cycles_consumed<budget) {
    s->svc_seen=false;
    if (!execute_one(s)) return CPU_EXIT_FAULT;
    s->cycles_consumed++;
    if (s->svc_seen) return CPU_EXIT_SVC;
  }
  return CPU_EXIT_CYCLES_ELAPSED;
}
static CPU_ExitReason interp_step(CPU_State *state) {
  InterpreterState *s=(InterpreterState *)state;
  s->cycles_consumed=0;
  s->svc_seen=false;
  if (!execute_one(s)) return CPU_EXIT_FAULT;
  s->cycles_consumed=1;
  return s->svc_seen?CPU_EXIT_SVC:CPU_EXIT_CYCLES_ELAPSED;
}
static uint64_t interp_fault(CPU_State *state) { return ((InterpreterState *)state)->fault_address; }
static uint64_t interp_cycles(CPU_State *state) { return ((InterpreterState *)state)->cycles_consumed; }
static uint64_t interp_get_reg(CPU_State *state,uint8_t i) { InterpreterState *s=(InterpreterState *)state; return i<=30?s->regs.x[i]:0; }
static void interp_set_reg(CPU_State *state,uint8_t i,uint64_t v) { InterpreterState *s=(InterpreterState *)state; if(i<=30)s->regs.x[i]=v; }
static uint64_t interp_get_pc(CPU_State *state) { return ((InterpreterState *)state)->regs.pc; }
static void interp_set_pc(CPU_State *state,uint64_t v) { ((InterpreterState *)state)->regs.pc=v; }
static uint64_t interp_get_sp(CPU_State *state) { return ((InterpreterState *)state)->regs.sp; }
static void interp_set_sp(CPU_State *state,uint64_t v) { ((InterpreterState *)state)->regs.sp=v; }
static uint32_t interp_get_pstate(CPU_State *state) { return ((InterpreterState *)state)->regs.pstate; }
static void interp_set_pstate(CPU_State *state,uint32_t v) { ((InterpreterState *)state)->regs.pstate=v; }
static CPU_Register_File *interp_get_file(CPU_State *state) { return &((InterpreterState *)state)->regs; }
static uint64_t interp_get_sys(CPU_State *state,uint32_t r) { InterpreterState *s=(InterpreterState *)state; return r==CPU_SYSREG_TPIDRRO_EL0?s->tpidrro_el0:0; }
static void interp_set_sys(CPU_State *state,uint32_t r,uint64_t v) { InterpreterState *s=(InterpreterState *)state; if(r==CPU_SYSREG_TPIDRRO_EL0)s->tpidrro_el0=v; }
static void interp_invalidate(CPU_State *state,uint64_t a,uint64_t n) { (void)state;(void)a;(void)n; }
static void interp_clear(CPU_State *state) { (void)state; }
static void interp_svc(CPU_State *state,CPU_SVC_Handler h) { ((InterpreterState *)state)->svc_handler=h; }
static void interp_undefined(CPU_State *state,CPU_Undefined_Handler h) { ((InterpreterState *)state)->undefined_handler=h; }
static void interp_breakpoint(CPU_State *state,CPU_Breakpoint_Handler h) { ((InterpreterState *)state)->breakpoint_handler=h; }

const CPU_Backend CPU_BACKEND_INTERPRETER = {
  .create=interp_create,.destroy=interp_destroy,.run=interp_run,.step=interp_step,
  .get_fault_address=interp_fault,.get_cycles_consumed=interp_cycles,
  .get_reg=interp_get_reg,.set_reg=interp_set_reg,.get_pc=interp_get_pc,.set_pc=interp_set_pc,
  .get_sp=interp_get_sp,.set_sp=interp_set_sp,.get_pstate=interp_get_pstate,.set_pstate=interp_set_pstate,
  .get_register_file=interp_get_file,.get_sys_reg=interp_get_sys,.set_sys_reg=interp_set_sys,
  .invalidate_cache=interp_invalidate,.clear_cache=interp_clear,
  .set_svc_handler=interp_svc,.set_undefined_handler=interp_undefined,
  .set_breakpoint_handler=interp_breakpoint,
  .name="interpreter",.version="0.1.0-dev",.supports_jit=false
};
