#define CHECK_NAME "a64_decoder_test"
#include "check.h"
#include "cpu/a64/decoder.h"

#include <stdio.h>
#include <string.h>

#include "a64_decoder_vectors_generated.h"

int main(void) {
  CHECK(sizeof(A64_Insn) == 16u);
  CHECK(A64_OP_INFO_COUNT == A64_OP_COUNT);

  for (size_t i = 0; i < sizeof(k_a64_decoder_vectors) / sizeof(k_a64_decoder_vectors[0]); ++i) {
    const struct A64_Decoder_Vector *v = &k_a64_decoder_vectors[i];
    A64_Insn decoded = {0};
    CHECK(a64_decode(v->encoding, &decoded));
    CHECK(decoded.op == v->op);
    CHECK(decoded.rd == v->rd);
    CHECK(decoded.rn == v->rn);
    CHECK(decoded.rm == v->rm);
    CHECK(decoded.ra == v->ra);
    CHECK(decoded.aux == v->aux);
    CHECK(decoded.imm == v->imm);
  }

  /* Known architectural encodings, independently fixed from interpreter tests. */
  A64_Insn decoded = {0};
  CHECK(a64_decode(UINT32_C(0xd2800540), &decoded)); /* MOVZ X0,#42 */
  CHECK(decoded.op == A64_OP_MOVZ && decoded.rd == 0u && decoded.imm == 42u);
  CHECK((decoded.aux & A64_AUX_IS_64) != 0u);

  CHECK(a64_decode(UINT32_C(0x91002001), &decoded)); /* ADD X1,X0,#8 */
  CHECK(decoded.op == A64_OP_ADD_ADDSUB_IMM);
  CHECK(decoded.rd == 1u && decoded.rn == 0u && decoded.imm == 8u);

  CHECK(a64_decode(UINT32_C(0xf9000441), &decoded)); /* STR X1,[X2,#8] */
  CHECK(decoded.op == A64_OP_STR_IMM_GEN);
  CHECK(decoded.rd == 1u && decoded.rn == 2u && decoded.imm == 8u);
  CHECK(((decoded.aux & A64_AUX_SIZE_MASK) >> A64_AUX_SIZE_SHIFT) == 3u);

  CHECK(a64_decode(UINT32_C(0xf9400443), &decoded)); /* LDR X3,[X2,#8] */
  CHECK(decoded.op == A64_OP_LDR_IMM_GEN);
  CHECK(decoded.rd == 3u && decoded.rn == 2u && decoded.imm == 8u);

  CHECK(a64_decode(UINT32_C(0xd4024681), &decoded)); /* SVC #0x1234 */
  CHECK(decoded.op == A64_OP_SVC && decoded.imm == UINT64_C(0x1234));

  CHECK(a64_decode(UINT32_C(0x14000000), &decoded)); /* B . */
  CHECK(decoded.op == A64_OP_B_UNCOND && decoded.imm == 0u);
  CHECK((decoded.aux & A64_AUX_LINK) == 0u);
  CHECK(a64_decode(UINT32_C(0x17ffffff), &decoded)); /* B #-4 */
  CHECK(decoded.op == A64_OP_B_UNCOND && (int64_t)decoded.imm == -4);
  CHECK(a64_decode(UINT32_C(0x94000001), &decoded)); /* BL #4 */
  CHECK(decoded.op == A64_OP_B_UNCOND && decoded.imm == 4u);
  CHECK((decoded.aux & A64_AUX_LINK) != 0u);
  CHECK(a64_decode(UINT32_C(0x54ffffe0), &decoded)); /* B.EQ #-4 */
  CHECK(decoded.op == A64_OP_B_COND && (int64_t)decoded.imm == -4);
  CHECK(a64_decode(UINT32_C(0xb5ffffe0), &decoded)); /* CBNZ X0,#-4 */
  CHECK(decoded.op == A64_OP_CBNZ && (int64_t)decoded.imm == -4);
  CHECK(a64_decode(UINT32_C(0x70ffffe0), &decoded)); /* ADR X0,#-1 */
  CHECK(decoded.op == A64_OP_ADR && (int64_t)decoded.imm == -1);

  /* CCMN register form has bit 11 clear; bit 11 set selects the
   * immediate form, which is not part of this interpreter subset. */
  CHECK(a64_decode(UINT32_C(0xba411024), &decoded)); /* CCMN X1,X1,#4,NE */
  CHECK(decoded.op == A64_OP_CCMN_REG);
  CHECK(decoded.rn == 1u && decoded.rm == 1u);
  CHECK(!a64_decode(UINT32_C(0xba411824), &decoded)); /* CCMN immediate */

  CHECK(a64_decode(UINT32_C(0xd503201f), &decoded)); /* NOP */
  CHECK(decoded.op == A64_OP_HINT);

  CHECK(a64_decode(UINT32_C(0x9b020c20), &decoded)); /* MADD X0,X1,X2,X3 */
  CHECK(decoded.op == A64_OP_MADD);
  CHECK(decoded.rd == 0u && decoded.rn == 1u && decoded.rm == 2u && decoded.ra == 3u);
  CHECK((decoded.aux & A64_AUX_IS_64) != 0u);

  CHECK(a64_decode(UINT32_C(0x9a820020), &decoded)); /* CSEL X0,X1,X2,EQ */
  CHECK(decoded.op == A64_OP_CSEL);
  CHECK(decoded.rd == 0u && decoded.rn == 1u && decoded.rm == 2u);
  CHECK(((decoded.aux >> A64_AUX_COND_SHIFT) & 15u) == 0u);

  CHECK(a64_decode(UINT32_C(0x9ac20820), &decoded)); /* UDIV X0,X1,X2 */
  CHECK(decoded.op == A64_OP_UDIV);
  CHECK(decoded.rd == 0u && decoded.rn == 1u && decoded.rm == 2u);

  CHECK(a64_decode(UINT32_C(0x93401c62), &decoded)); /* SBFM X2,X3,#0,#7 */
  CHECK(decoded.op == A64_OP_SBFM);
  CHECK(decoded.rd == 2u && decoded.rn == 3u && decoded.imm == 7u);

  CHECK(a64_decode(UINT32_C(0xd53bd069), &decoded)); /* MRS X9,TPIDRRO_EL0 */
  CHECK(decoded.op == A64_OP_MRS && decoded.rd == 9u);

  CHECK(A64_OP_INFO[A64_OP_B_UNCOND].kind == A64_KIND_DIRECT_BRANCH);
  CHECK(A64_OP_INFO[A64_OP_B_COND].flags_read == 0x0fu);
  CHECK(A64_OP_INFO[A64_OP_LDR_IMM_GEN].mem_kind == A64_MEM_LOAD);
  CHECK(A64_OP_INFO[A64_OP_STR_IMM_GEN].mem_kind == A64_MEM_STORE);
  CHECK(A64_OP_INFO[A64_OP_ADD_ADDSUB_IMM].rn31_use == A64_REG31_SP);
  CHECK(A64_OP_INFO[A64_OP_ADD_ADDSUB_IMM].rd31_use == A64_REG31_SP);
  CHECK(A64_OP_INFO[A64_OP_ADDS_ADDSUB_IMM].rd31_use == A64_REG31_ZERO);
  CHECK(A64_OP_INFO[A64_OP_LDR_IMM_GEN].rd31_use == A64_REG31_ZERO);
  CHECK(A64_OP_INFO[A64_OP_LDR_IMM_GEN].rn31_use == A64_REG31_SP);
  CHECK(A64_OP_INFO[A64_OP_STR_IMM_GEN].rd31_use == A64_REG31_ZERO);
  CHECK(A64_OP_INFO[A64_OP_CCMN_REG].flags_read == 0x0fu);
  CHECK(A64_OP_INFO[A64_OP_CCMN_REG].flags_written == 0x0fu);
  CHECK(A64_OP_INFO[A64_OP_ADR].rd31_use == A64_REG31_ZERO);
  CHECK(A64_OP_INFO[A64_OP_MOVZ].rd31_use == A64_REG31_ZERO);
  CHECK(A64_OP_INFO[A64_OP_MRS].rd31_use == A64_REG31_ZERO);
  CHECK(A64_OP_INFO[A64_OP_MOVZ].jit_tier == 0u);
  CHECK(A64_OP_INFO[A64_OP_BR].rn31_use == A64_REG31_ZERO);
  CHECK(A64_OP_INFO[A64_OP_RET].rn31_use == A64_REG31_ZERO);
  CHECK(A64_OP_INFO[A64_OP_CBZ].rd31_use == A64_REG31_ZERO);
  CHECK(A64_OP_INFO[A64_OP_MOVK].rd31_use == A64_REG31_ZERO);
  CHECK(A64_OP_INFO[A64_OP_MADD].rd31_use == A64_REG31_ZERO);
  CHECK(A64_OP_INFO[A64_OP_MADD].rn31_use == A64_REG31_ZERO);
  CHECK(A64_OP_INFO[A64_OP_MADD].rm31_use == A64_REG31_ZERO);
  CHECK(A64_OP_INFO[A64_OP_MADD].ra31_use == A64_REG31_ZERO);
  CHECK(A64_OP_INFO[A64_OP_ADD_ADDSUB_SHIFT].rd31_use == A64_REG31_ZERO);
  CHECK(A64_OP_INFO[A64_OP_ADDS_ADDSUB_SHIFT].flags_written == 0x0fu);
  CHECK(A64_OP_INFO[A64_OP_ANDS_LOG_SHIFT].flags_written == 0x0fu);
  CHECK(A64_OP_INFO[A64_OP_CSEL].flags_read == 0x0fu);
  CHECK(A64_OP_INFO[A64_OP_LDR_IMM_GEN].mem_alignment == 1u);
  CHECK(A64_OP_INFO[A64_OP_STR_IMM_GEN].mem_alignment == 1u);
  CHECK(A64_OP_INFO[A64_OP_SVC].kind == A64_KIND_SVC);
  CHECK(A64_OP_INFO[A64_OP_MRS].kind == A64_KIND_SYSTEM);

  /* Failed decodes leave the caller's record untouched. */
  A64_Insn sentinel = {0x1234u, 1u, 2u, 3u, 4u, 0x5678u, UINT64_C(0xabcdef)};
  decoded = sentinel;
  CHECK(!a64_decode(UINT32_C(0xffffffff), &decoded));
  CHECK(memcmp(&decoded, &sentinel, sizeof(decoded)) == 0);
  CHECK(!a64_decode(UINT32_C(0x00000000), &decoded));
  CHECK(memcmp(&decoded, &sentinel, sizeof(decoded)) == 0);
  CHECK(!a64_decode(UINT32_C(0), NULL));

  printf("[" CHECK_NAME "] OK (%zu generated forms)\n",
         sizeof(k_a64_decoder_vectors) / sizeof(k_a64_decoder_vectors[0]));
  return 0;
}
