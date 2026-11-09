/**
 * CNMT-to-NCA content resolver tests using synthetic plaintext fixtures.
 */
#define CHECK_NAME "system_content_test"
#include "check.h"

#include "hle/loader/system_content.h"
#include "loader_fixtures.h"

#include <stdio.h>
#include <string.h>

#define HASH_LAYER_BYTES 0x400u
static const char k_main[] = "main-nso";
static const Fixture_File k_exefs_files[] = {
    {"main", k_main, sizeof(k_main) - 1}};

typedef struct Built {
  Fixture_Buffer exefs;
  Fixture_Buffer nca;
  uint8_t cnmt_bytes[CNMT_HEADER_SIZE + 0x38];
  Byte_Source cnmt_source;
  Arena arena;
  CNMT cnmt;
} Built;

static void build(Built *b, const char *unused) {
  (void)unused;
  memset(b, 0, sizeof(*b));
  fixture_build_pfs0(k_exefs_files, 1, &b->exefs);
  Fixture_NCA_Section sections[4];
  memset(sections, 0, sizeof(sections));
  sections[0] = (Fixture_NCA_Section){
      true, FIXTURE_NCA_FS_TYPE_PFS0, FIXTURE_NCA_HASH_SHA256, 1,
      &b->exefs, HASH_LAYER_BYTES};
  fixture_build_nca(NCA_MAGIC_NCA3, NCA_CONTENT_PROGRAM, 0x0100000000001000ull,
                    sections, &b->nca);
  uint8_t *raw = b->cnmt_bytes;
  fixture_put_le16(raw + 0x10, 1);
  for (uint32_t i = 0; i < 16; i++) raw[CNMT_HEADER_SIZE + 0x20 + i] = (uint8_t)(i + 1);
  uint64_t size = b->nca.size;
  for (uint32_t i = 0; i < 5; i++) raw[CNMT_HEADER_SIZE + 0x30 + i] = (uint8_t)(size >> (i * 8));
  raw[CNMT_HEADER_SIZE + 0x36] = NCA_CONTENT_PROGRAM;
  b->cnmt_source = byte_source_from_memory(raw, sizeof(b->cnmt_bytes));
  CHECK(arena_create(&b->arena, 4096));
  CHECK_OK(cnmt_open(&b->cnmt_source, &b->arena, CNMT_CONTENT_LAYOUT_MODERN, &b->cnmt));
}

static void cleanup(Built *b) {
  arena_destroy(&b->arena);
  fixture_buffer_free(&b->exefs);
  fixture_buffer_free(&b->nca);
}

static void test_matching_and_missing(void) {
  Built b;
  build(&b, NULL);
  Byte_Source nca_source = byte_source_from_memory(b.nca.bytes, b.nca.size);
  const SystemSoftware_Input inputs[] = {
      {"0102030405060708090a0b0c0d0e0f10.nca", &nca_source}};
  SystemContent_Inventory inventory;
  CHECK_OK(system_content_resolve(&b.cnmt, inputs, 1, &b.arena, &inventory));
  CHECK(inventory.count == 1);
  CHECK(inventory.matched_count == 1);
  CHECK(inventory.entries[0].status == SYSTEM_CONTENT_MATCHED);
  CHECK(inventory.entries[0].input_index == 0);

  arena_reset(&b.arena);
  CHECK_OK(cnmt_open(&b.cnmt_source, &b.arena, CNMT_CONTENT_LAYOUT_MODERN, &b.cnmt));
  CHECK_OK(system_content_resolve(&b.cnmt, NULL, 0, &b.arena, &inventory));
  CHECK(inventory.missing_count == 1);
  CHECK(inventory.entries[0].status == SYSTEM_CONTENT_MISSING);
  cleanup(&b);
}

static void test_ambiguous_and_invalid(void) {
  Built b;
  build(&b, NULL);
  Byte_Source source = byte_source_from_memory(b.nca.bytes, b.nca.size);
  const SystemSoftware_Input duplicates[] = {
      {"0102030405060708090a0b0c0d0e0f10.nca", &source},
      {"0102030405060708090a0b0c0d0e0f10", &source}};
  SystemContent_Inventory inventory;
  CHECK_OK(system_content_resolve(&b.cnmt, duplicates, 2, &b.arena, &inventory));
  CHECK(inventory.ambiguous_count == 1);

  arena_reset(&b.arena);
  CHECK_OK(cnmt_open(&b.cnmt_source, &b.arena, CNMT_CONTENT_LAYOUT_MODERN, &b.cnmt));
  Byte_Source bad = byte_source_from_memory("bad", 3);
  const SystemSoftware_Input invalid[] = {
      {"0102030405060708090a0b0c0d0e0f10.nca", &bad}};
  CHECK_OK(system_content_resolve(&b.cnmt, invalid, 1, &b.arena, &inventory));
  CHECK(inventory.invalid_count == 1);
  CHECK(inventory.entries[0].status == SYSTEM_CONTENT_INVALID);
  cleanup(&b);
}

static void test_mismatch_and_arguments(void) {
  Built b;
  build(&b, NULL);
  Byte_Source source = byte_source_from_memory(b.nca.bytes, b.nca.size);
  const SystemSoftware_Input inputs[] = {
      {"0102030405060708090a0b0c0d0e0f10.nca", &source}};
  /* Deliberately alter the expected size after parsing. */
  b.cnmt.contents[0].size++;
  SystemContent_Inventory inventory;
  CHECK_OK(system_content_resolve(&b.cnmt, inputs, 1, &b.arena, &inventory));
  CHECK(inventory.mismatch_count == 1);
  CHECK(inventory.entries[0].status == SYSTEM_CONTENT_MISMATCH);
  CHECK_CODE(system_content_resolve(NULL, inputs, 1, &b.arena, &inventory),
             RESULT_INVALID_ARGUMENT);
  cleanup(&b);
}

int main(void) {
  test_matching_and_missing();
  test_ambiguous_and_invalid();
  test_mismatch_and_arguments();
  printf("[system_content_test] all tests passed\n");
  return 0;
}
