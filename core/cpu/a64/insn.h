#ifndef VOLAND_CPU_A64_INSN_H
#define VOLAND_CPU_A64_INSN_H

#include <stdint.h>

typedef enum A64_Op {
  A64_OP_INVALID = 0,
  A64_OP_UNDEFINED,
  A64_OP_ADDS_ADDSUB_IMM = 2,
  A64_OP_ADDS_ADDSUB_SHIFT = 3,
  A64_OP_ADD_ADDSUB_IMM = 4,
  A64_OP_ADD_ADDSUB_SHIFT = 5,
  A64_OP_ADR = 6,
  A64_OP_ADRP = 7,
  A64_OP_ANDS_LOG_SHIFT = 8,
  A64_OP_AND_LOG_SHIFT = 9,
  A64_OP_BIC_LOG_SHIFT = 10,
  A64_OP_BR = 11,
  A64_OP_B_COND = 12,
  A64_OP_B_UNCOND = 13,
  A64_OP_CBNZ = 14,
  A64_OP_CBZ = 15,
  A64_OP_CCMN_REG = 16,
  A64_OP_CSEL = 17,
  A64_OP_CSINC = 18,
  A64_OP_CSINV = 19,
  A64_OP_CSNEG = 20,
  A64_OP_EON = 21,
  A64_OP_EOR_LOG_SHIFT = 22,
  A64_OP_HINT = 23,
  A64_OP_LDR_IMM_GEN = 24,
  A64_OP_MADD = 25,
  A64_OP_MOVK = 26,
  A64_OP_MOVN = 27,
  A64_OP_MOVZ = 28,
  A64_OP_MRS = 29,
  A64_OP_MSUB = 30,
  A64_OP_ORN_LOG_SHIFT = 31,
  A64_OP_ORR_LOG_SHIFT = 32,
  A64_OP_RET = 33,
  A64_OP_SBFM = 34,
  A64_OP_SDIV = 35,
  A64_OP_STR_IMM_GEN = 36,
  A64_OP_SUBS_ADDSUB_IMM = 37,
  A64_OP_SUBS_ADDSUB_SHIFT = 38,
  A64_OP_SUB_ADDSUB_IMM = 39,
  A64_OP_SUB_ADDSUB_SHIFT = 40,
  A64_OP_SVC = 41,
  A64_OP_UBFM = 42,
  A64_OP_UDIV = 43,
  A64_OP_COUNT = 44
} A64_Op;

typedef struct A64_Insn {
    uint16_t op;
    uint8_t rd, rn;
    uint8_t rm, ra;
    uint16_t aux;
    /* Normalized immediate: branch displacements are signed byte offsets;
     * ADR/ADRP are signed, move-wide/add immediates are pre-shifted,
     * load/store offsets are byte-scaled, and bitfields pack immr:imms. */
    uint64_t imm;
} A64_Insn;

_Static_assert(sizeof(A64_Insn) == 16,
               "A64 instruction records must be 16 bytes");

enum {
    A64_FIELD_RD = 1u << 0,
    A64_FIELD_RN = 1u << 1,
    A64_FIELD_RM = 1u << 2,
    A64_FIELD_RA = 1u << 3,
    A64_REG31_NONE = 0,
    A64_REG31_SP = 1,
    A64_REG31_ZERO = 2,
    A64_MEM_NONE = 0,
    A64_MEM_LOAD = 1,
    A64_MEM_STORE = 2,
    A64_MEM_LOAD_STORE = 3,
    A64_KIND_SEQUENTIAL = 0,
    A64_KIND_DIRECT_BRANCH = 1,
    A64_KIND_CONDITIONAL_BRANCH = 2,
    A64_KIND_CALL = 3,
    A64_KIND_INDIRECT_BRANCH = 4,
    A64_KIND_SVC = 5,
    A64_KIND_EXCEPTION = 6,
    A64_KIND_SYSTEM = 7,
    A64_KIND_BARRIER = 8,
    A64_AUX_IS_64 = 1u << 0,
    A64_AUX_SIZE_SHIFT = 1,
    A64_AUX_SIZE_MASK = 3u << A64_AUX_SIZE_SHIFT,
    A64_AUX_LINK = 1u << 3,
    A64_AUX_SHIFT_SHIFT = 4,
    A64_AUX_COND_SHIFT = 6,
    A64_AUX_HW_SHIFT = 10,
    A64_AUX_NZCV_SHIFT = 12
};

/* mem_size=0 means per-form size is in A64_Insn.aux; alignment is bytes. */
typedef struct A64_Op_Info {
    uint8_t gpr_reads;
    uint8_t gpr_writes;
    uint8_t rd31_use;
    uint8_t rn31_use;
    uint8_t rm31_use;
    uint8_t ra31_use;
    uint8_t flags_read;
    uint8_t flags_written;
    uint8_t vreg_reads;
    uint8_t vreg_writes;
    uint8_t mem_kind;
    uint8_t mem_size;
    uint8_t mem_count;
    uint8_t mem_alignment;
    uint8_t kind;
    uint8_t fp_mode_dependent;
    uint8_t jit_tier;
} A64_Op_Info;

extern const A64_Op_Info A64_OP_INFO[];
extern const uint32_t A64_OP_INFO_COUNT;

#endif
