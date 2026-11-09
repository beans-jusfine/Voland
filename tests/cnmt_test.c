/** Unit tests for the bounded CNMT parser. */
#define CHECK_NAME "cnmt_test"
#include "check.h"

#include "hle/loader/cnmt.h"

#include <string.h>

#define FIXTURE_EXT_SIZE 4u
#define FIXTURE_CONTENT_OFFSET (CNMT_HEADER_SIZE + FIXTURE_EXT_SIZE)
#define FIXTURE_CONTENT_SIZE 0x38u
#define FIXTURE_META_OFFSET (FIXTURE_CONTENT_OFFSET + FIXTURE_CONTENT_SIZE)
#define FIXTURE_TOTAL_SIZE (FIXTURE_META_OFFSET + CNMT_META_INFO_SIZE + 0x20u)

static uint8_t fixture[FIXTURE_TOTAL_SIZE];
static uint8_t original[FIXTURE_TOTAL_SIZE];

typedef union Test_Arena_Memory {
  max_align_t alignment;
  uint8_t bytes[2048];
} Test_Arena_Memory;

static void put_le16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}
static void put_le32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
}
static void put_le48(uint8_t *p, uint64_t v) {
  for (uint32_t i = 0; i < 6; i++) p[i] = (uint8_t)(v >> (i * 8));
}
static void put_le64(uint8_t *p, uint64_t v) {
  put_le32(p, (uint32_t)v);
  put_le32(p + 4, (uint32_t)(v >> 32));
}

static void build_fixture(CNMT_Content_Layout layout) {
  memset(fixture, 0, sizeof(fixture));
  put_le64(fixture + 0x00, 0x0100000000001000ull);
  put_le32(fixture + 0x08, 0x000A0000u);
  fixture[0x0C] = 0x80; /* Application */
  fixture[0x0D] = 1;
  put_le16(fixture + 0x0E, FIXTURE_EXT_SIZE);
  put_le16(fixture + 0x10, 1);
  put_le16(fixture + 0x12, 1);
  fixture[0x14] = 0x03;
  put_le32(fixture + 0x18, 0x00090000u);
  put_le32(fixture + CNMT_HEADER_SIZE, 0xAABBCCDDu);

  uint8_t *content = fixture + FIXTURE_CONTENT_OFFSET;
  for (uint32_t i = 0; i < CNMT_CONTENT_HASH_SIZE; i++) content[i] = (uint8_t)i;
  for (uint32_t i = 0; i < CNMT_CONTENT_ID_SIZE; i++) {
    content[CNMT_CONTENT_HASH_SIZE + i] = (uint8_t)(0x80u + i);
  }
  put_le48(content + 0x30, 0x010203040506ull);
  if (layout == CNMT_CONTENT_LAYOUT_MODERN) content[0x35] = 0x5A;
  content[0x36] = 2; /* Control */
  content[0x37] = 3;

  uint8_t *meta = fixture + FIXTURE_META_OFFSET;
  put_le64(meta + 0x00, 0x0100000000002000ull);
  put_le32(meta + 0x08, 0x000B0000u);
  meta[0x0C] = 2; /* SystemUpdate */
  meta[0x0D] = 0x07;
  memcpy(original, fixture, sizeof(fixture));
}

static void test_valid_layout(CNMT_Content_Layout layout) {
  build_fixture(layout);
  Byte_Source source = byte_source_from_memory(fixture, sizeof(fixture));
  Test_Arena_Memory memory;
  Arena arena;
  arena_create_in_place(&arena, memory.bytes, sizeof(memory.bytes));
  CNMT cnmt;
  memset(&cnmt, 0xCC, sizeof(cnmt));

  CHECK_OK(cnmt_open(&source, &arena, layout, &cnmt));
  CHECK(cnmt.source == &source);
  CHECK(cnmt.header.title_id == 0x0100000000001000ull);
  CHECK(cnmt.header.version == 0x000A0000u);
  CHECK(cnmt.header.meta_type == 0x80);
  CHECK(cnmt.header.platform == 1);
  CHECK(cnmt.header.extended_header_size == FIXTURE_EXT_SIZE);
  CHECK(cnmt.header.content_count == 1);
  CHECK(cnmt.header.meta_count == 1);
  CHECK(cnmt.header.attributes == 3);
  CHECK(cnmt.header.required_download_system_version == 0x00090000u);
  CHECK(cnmt.extended_header_offset == CNMT_HEADER_SIZE);

  CHECK(cnmt.contents != NULL);
  CHECK(memcmp(cnmt.contents[0].hash, fixture + FIXTURE_CONTENT_OFFSET, 32) == 0);
  CHECK(memcmp(cnmt.contents[0].content_id,
               fixture + FIXTURE_CONTENT_OFFSET + 0x20, 16) == 0);
  CHECK(cnmt.contents[0].size == 0x010203040506ull);
  CHECK(cnmt.contents[0].attributes ==
        (layout == CNMT_CONTENT_LAYOUT_MODERN ? 0x5A : 0));
  CHECK(cnmt.contents[0].content_type == 2);
  CHECK(cnmt.contents[0].id_offset == 3);

  CHECK(cnmt.meta_entries != NULL);
  CHECK(cnmt.meta_entries[0].id == 0x0100000000002000ull);
  CHECK(cnmt.meta_entries[0].version == 0x000B0000u);
  CHECK(cnmt.meta_entries[0].meta_type == 2);
  CHECK(cnmt.meta_entries[0].attributes == 7);
  CHECK(memcmp(fixture, original, sizeof(fixture)) == 0);
}

static void test_zero_records(void) {
  memset(fixture, 0, sizeof(fixture));
  put_le64(fixture, 0x0100000000000001ull);
  Byte_Source source = byte_source_from_memory(fixture, CNMT_HEADER_SIZE);
  Test_Arena_Memory memory;
  Arena arena;
  arena_create_in_place(&arena, memory.bytes, sizeof(memory.bytes));
  CNMT cnmt;
  CHECK_OK(cnmt_open(&source, &arena, CNMT_CONTENT_LAYOUT_MODERN, &cnmt));
  CHECK(cnmt.contents == NULL);
  CHECK(cnmt.meta_entries == NULL);
  CHECK(cnmt.header.content_count == 0);
  CHECK(cnmt.header.meta_count == 0);
}

static void test_rejections(void) {
  build_fixture(CNMT_CONTENT_LAYOUT_MODERN);
  Byte_Source source = byte_source_from_memory(fixture, sizeof(fixture));
  Test_Arena_Memory memory;
  Arena arena;
  arena_create_in_place(&arena, memory.bytes, sizeof(memory.bytes));
  CNMT cnmt;

  CHECK_CODE(cnmt_open(NULL, &arena, CNMT_CONTENT_LAYOUT_MODERN, &cnmt),
             RESULT_INVALID_ARGUMENT);
  CHECK_CODE(cnmt_open(&source, NULL, CNMT_CONTENT_LAYOUT_MODERN, &cnmt),
             RESULT_INVALID_ARGUMENT);
  CHECK_CODE(cnmt_open(&source, &arena, CNMT_CONTENT_LAYOUT_MODERN, NULL),
             RESULT_INVALID_ARGUMENT);
  CHECK_CODE(cnmt_open(&source, &arena, (CNMT_Content_Layout)9, &cnmt),
             RESULT_INVALID_ARGUMENT);

  Byte_Source short_header = byte_source_from_memory(fixture, CNMT_HEADER_SIZE - 1);
  CHECK_CODE(cnmt_open(&short_header, &arena, CNMT_CONTENT_LAYOUT_MODERN, &cnmt),
             RESULT_INVALID_ARGUMENT);
  Byte_Source short_content = byte_source_from_memory(
      fixture, FIXTURE_CONTENT_OFFSET + FIXTURE_CONTENT_SIZE - 1);
  CHECK_CODE(cnmt_open(&short_content, &arena, CNMT_CONTENT_LAYOUT_MODERN, &cnmt),
             RESULT_INVALID_ARGUMENT);
  Byte_Source short_meta = byte_source_from_memory(
      fixture, FIXTURE_META_OFFSET + CNMT_META_INFO_SIZE - 1);
  CHECK_CODE(cnmt_open(&short_meta, &arena, CNMT_CONTENT_LAYOUT_MODERN, &cnmt),
             RESULT_INVALID_ARGUMENT);

  put_le16(fixture + 0x0E, UINT16_MAX);
  CHECK_CODE(cnmt_open(&source, &arena, CNMT_CONTENT_LAYOUT_MODERN, &cnmt),
             RESULT_INVALID_ARGUMENT);

  build_fixture(CNMT_CONTENT_LAYOUT_MODERN);
  Test_Arena_Memory tiny_memory;
  Arena tiny_arena;
  arena_create_in_place(&tiny_arena, tiny_memory.bytes, 1);
  CHECK_CODE(cnmt_open(&source, &tiny_arena, CNMT_CONTENT_LAYOUT_MODERN, &cnmt),
             RESULT_OUT_OF_MEMORY);
}

int main(void) {
  test_valid_layout(CNMT_CONTENT_LAYOUT_LEGACY);
  test_valid_layout(CNMT_CONTENT_LAYOUT_MODERN);
  test_zero_records();
  test_rejections();
  printf("[cnmt_test] all tests passed\n");
  return 0;
}
