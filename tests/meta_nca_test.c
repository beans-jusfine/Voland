/** Integration tests for Meta NCA -> PFS0 -> CNMT. */
#define CHECK_NAME "meta_nca_test"
#include "check.h"

#include "hle/loader/meta_nca.h"
#include "loader_fixtures.h"

#include <string.h>

#define TEST_TITLE_ID 0x0100000000001000ull

typedef union Test_Arena_Memory {
  uint64_t alignment;
  uint8_t bytes[16384];
} Test_Arena_Memory;

typedef struct Meta_Fixture {
  Fixture_Buffer pfs0;
  Fixture_Buffer nca;
} Meta_Fixture;

static void build_cnmt(uint8_t bytes[0x20]) {
  memset(bytes, 0, 0x20);
  fixture_put_le64(bytes, TEST_TITLE_ID);
  fixture_put_le32(bytes + 0x08, 0x00010000u);
  bytes[0x0C] = 0x80; /* Application */
}

static void build_meta_fixture(const Fixture_File *files, uint32_t file_count,
                               uint8_t content_type, Meta_Fixture *fixture) {
  fixture_build_pfs0(files, file_count, &fixture->pfs0);
  Fixture_NCA_Section sections[4];
  memset(sections, 0, sizeof(sections));
  sections[0] = (Fixture_NCA_Section){
      true, FIXTURE_NCA_FS_TYPE_PFS0, FIXTURE_NCA_HASH_NONE, 1,
      &fixture->pfs0, 0};
  fixture_build_nca(NCA_MAGIC_NCA3, content_type, TEST_TITLE_ID,
                    sections, &fixture->nca);
}

static void free_fixture(Meta_Fixture *fixture) {
  fixture_buffer_free(&fixture->pfs0);
  fixture_buffer_free(&fixture->nca);
}

static void test_valid_meta_nca(void) {
  uint8_t cnmt_bytes[0x20];
  build_cnmt(cnmt_bytes);
  const Fixture_File files[] = {
      {"0100000000001000.cnmt", cnmt_bytes, sizeof(cnmt_bytes)},
      {"other.bin", "data", 4},
  };
  Meta_Fixture fixture;
  build_meta_fixture(files, 2, NCA_CONTENT_META, &fixture);

  Byte_Source source = byte_source_from_memory(fixture.nca.bytes, fixture.nca.size);
  Test_Arena_Memory memory;
  Arena arena;
  arena_create_in_place(&arena, memory.bytes, sizeof(memory.bytes));
  Meta_NCA meta;
  memset(&meta, 0, sizeof(meta));
  CHECK_OK(meta_nca_open(&source, &arena, CNMT_CONTENT_LAYOUT_MODERN, &meta));
  CHECK(meta.nca.header.content_type == NCA_CONTENT_META);
  CHECK(meta.pfs0.file_count == 2);
  CHECK(meta.cnmt.header.title_id == TEST_TITLE_ID);
  CHECK(meta.cnmt.header.version == 0x00010000u);
  CHECK(meta.cnmt.header.content_count == 0);
  CHECK(meta.cnmt.source == &meta.cnmt_source.source);
  CHECK(meta.pfs0.source == &meta.pfs0_source.source);

  free_fixture(&fixture);
}

static void test_missing_and_duplicate_cnmt(void) {
  const Fixture_File no_cnmt[] = {{"readme.txt", "x", 1}};
  Meta_Fixture missing;
  build_meta_fixture(no_cnmt, 1, NCA_CONTENT_META, &missing);
  Byte_Source missing_source = byte_source_from_memory(missing.nca.bytes, missing.nca.size);
  Test_Arena_Memory missing_memory;
  Arena missing_arena;
  arena_create_in_place(&missing_arena, missing_memory.bytes, sizeof(missing_memory.bytes));
  Meta_NCA output;
  CHECK_CODE(meta_nca_open(&missing_source, &missing_arena,
                           CNMT_CONTENT_LAYOUT_MODERN, &output), RESULT_NOT_FOUND);
  free_fixture(&missing);

  uint8_t cnmt_bytes[0x20];
  build_cnmt(cnmt_bytes);
  const Fixture_File duplicates[] = {
      {"one.cnmt", cnmt_bytes, sizeof(cnmt_bytes)},
      {"two.CNMT", cnmt_bytes, sizeof(cnmt_bytes)},
  };
  Meta_Fixture duplicate;
  build_meta_fixture(duplicates, 2, NCA_CONTENT_META, &duplicate);
  Byte_Source duplicate_source = byte_source_from_memory(duplicate.nca.bytes, duplicate.nca.size);
  Test_Arena_Memory duplicate_memory;
  Arena duplicate_arena;
  arena_create_in_place(&duplicate_arena, duplicate_memory.bytes, sizeof(duplicate_memory.bytes));
  CHECK_CODE(meta_nca_open(&duplicate_source, &duplicate_arena,
                           CNMT_CONTENT_LAYOUT_MODERN, &output), RESULT_ALREADY_EXISTS);
  free_fixture(&duplicate);
}

static void test_reject_wrong_type_and_malformed_cnmt(void) {
  uint8_t cnmt_bytes[0x20];
  build_cnmt(cnmt_bytes);
  const Fixture_File files[] = {{"title.cnmt", cnmt_bytes, sizeof(cnmt_bytes)}};
  Meta_Fixture wrong_type;
  build_meta_fixture(files, 1, NCA_CONTENT_PROGRAM, &wrong_type);
  Byte_Source wrong_source = byte_source_from_memory(wrong_type.nca.bytes, wrong_type.nca.size);
  Test_Arena_Memory wrong_memory;
  Arena wrong_arena;
  arena_create_in_place(&wrong_arena, wrong_memory.bytes, sizeof(wrong_memory.bytes));
  Meta_NCA output;
  CHECK_CODE(meta_nca_open(&wrong_source, &wrong_arena,
                           CNMT_CONTENT_LAYOUT_MODERN, &output), RESULT_INVALID_ARGUMENT);
  free_fixture(&wrong_type);

  const Fixture_File malformed[] = {{"title.cnmt", cnmt_bytes, sizeof(cnmt_bytes) - 1}};
  Meta_Fixture truncated;
  build_meta_fixture(malformed, 1, NCA_CONTENT_META, &truncated);
  Byte_Source truncated_source = byte_source_from_memory(truncated.nca.bytes, truncated.nca.size);
  Test_Arena_Memory truncated_memory;
  Arena truncated_arena;
  arena_create_in_place(&truncated_arena, truncated_memory.bytes, sizeof(truncated_memory.bytes));
  CHECK_CODE(meta_nca_open(&truncated_source, &truncated_arena,
                           CNMT_CONTENT_LAYOUT_MODERN, &output), RESULT_INVALID_ARGUMENT);
  free_fixture(&truncated);
}

static void test_truncated_and_null_inputs(void) {
  uint8_t cnmt_bytes[0x20];
  build_cnmt(cnmt_bytes);
  const Fixture_File files[] = {{"title.cnmt", cnmt_bytes, sizeof(cnmt_bytes)}};
  Meta_Fixture fixture;
  build_meta_fixture(files, 1, NCA_CONTENT_META, &fixture);
  Byte_Source source = byte_source_from_memory(fixture.nca.bytes, FIXTURE_NCA_HEADER_SIZE - 1);
  Test_Arena_Memory memory;
  Arena arena;
  arena_create_in_place(&arena, memory.bytes, sizeof(memory.bytes));
  Meta_NCA output;
  CHECK_CODE(meta_nca_open(NULL, &arena, CNMT_CONTENT_LAYOUT_MODERN, &output),
             RESULT_INVALID_ARGUMENT);
  CHECK_CODE(meta_nca_open(&source, &arena, CNMT_CONTENT_LAYOUT_MODERN, &output),
             RESULT_INVALID_ARGUMENT);
  free_fixture(&fixture);
}

int main(void) {
  test_valid_meta_nca();
  test_missing_and_duplicate_cnmt();
  test_reject_wrong_type_and_malformed_cnmt();
  test_truncated_and_null_inputs();
  printf("[meta_nca_test] all tests passed\n");
  return 0;
}
