/**
 * Unit tests for user-supplied system-software inventory.
 * All NCA files are synthesized plaintext fixtures; no Nintendo data,
 * keys, cryptography, or network access are involved.
 */
#define CHECK_NAME "system_software_test"
#include "check.h"

#include "hle/loader/system_software.h"
#include "loader_fixtures.h"

#include <stdlib.h>
#include <string.h>

#define PROGRAM_ID 0x0100ABCDEF000000ull
#define HASH_LAYER_BYTES 0x400u

static const char k_main[] = "main-nso";
static const Fixture_File k_exefs_files[] = {
    {"main", k_main, sizeof(k_main) - 1}};

typedef struct Built {
  Fixture_Buffer exefs;
  Fixture_Buffer nca;
} Built;

static void build_program_nca(Built *built, uint64_t program_id,
                              uint32_t content_index) {
  fixture_build_pfs0(k_exefs_files, 1, &built->exefs);
  Fixture_NCA_Section sections[4];
  memset(sections, 0, sizeof(sections));
  sections[0] = (Fixture_NCA_Section){
      true, FIXTURE_NCA_FS_TYPE_PFS0, FIXTURE_NCA_HASH_SHA256, 1,
      &built->exefs, HASH_LAYER_BYTES};
  fixture_build_nca(NCA_MAGIC_NCA3, NCA_CONTENT_PROGRAM, program_id,
                    sections, &built->nca);
  fixture_put_le32(built->nca.bytes + FIXTURE_NCA_OFFSET_CONTENT_INDEX,
                   content_index);
}

static void free_built(Built *built) {
  fixture_buffer_free(&built->exefs);
  fixture_buffer_free(&built->nca);
}

static void test_valid_inventory_and_read_only_inputs(void) {
  Built built;
  build_program_nca(&built, PROGRAM_ID, 7);
  uint8_t *original = (uint8_t *)malloc(built.nca.size);
  CHECK(original != NULL);
  memcpy(original, built.nca.bytes, built.nca.size);

  Byte_Source source = byte_source_from_memory(built.nca.bytes, built.nca.size);
  const SystemSoftware_Input inputs[] = {{"system-program.nca", &source}};
  SystemSoftware_Inventory inventory;
  CHECK_OK(system_software_validate(inputs, 1, &inventory));

  CHECK(inventory.count == 1);
  CHECK(inventory.valid_count == 1);
  CHECK(inventory.invalid_count == 0);
  CHECK(inventory.entries[0].validated);
  CHECK(inventory.entries[0].program_id == PROGRAM_ID);
  CHECK(inventory.entries[0].content_index == 7);
  CHECK(inventory.entries[0].content_type == NCA_CONTENT_PROGRAM);
  CHECK(inventory.entries[0].present_sections == 1u);
  CHECK(inventory.entries[0].duplicate_of == SYSTEM_SOFTWARE_NO_DUPLICATE);
  CHECK(strcmp(inventory.entries[0].name, "system-program.nca") == 0);
  CHECK(memcmp(original, built.nca.bytes, built.nca.size) == 0);

  free(original);
  free_built(&built);
}

static void test_invalid_entries_do_not_abort_collection(void) {
  Built valid;
  Built encrypted;
  build_program_nca(&valid, PROGRAM_ID, 1);
  build_program_nca(&encrypted, PROGRAM_ID + 1, 1);
  memcpy(encrypted.nca.bytes + FIXTURE_NCA_OFFSET_MAGIC, "NOPE", 4);

  Byte_Source valid_source = byte_source_from_memory(valid.nca.bytes, valid.nca.size);
  Byte_Source encrypted_source =
      byte_source_from_memory(encrypted.nca.bytes, encrypted.nca.size);
  Byte_Source truncated_source =
      byte_source_from_memory(valid.nca.bytes, FIXTURE_NCA_HEADER_SIZE - 1);
  const SystemSoftware_Input inputs[] = {
      {"valid.nca", &valid_source},
      {"encrypted.nca", &encrypted_source},
      {"truncated.nca", &truncated_source},
      {"missing-source.nca", NULL},
  };
  SystemSoftware_Inventory inventory;
  CHECK_OK(system_software_validate(inputs, 4, &inventory));

  CHECK(inventory.count == 4);
  CHECK(inventory.valid_count == 1);
  CHECK(inventory.invalid_count == 3);
  CHECK(inventory.entries[0].validated);
  CHECK(inventory.entries[1].validation_error.code == RESULT_ENCRYPTED_INPUT);
  CHECK(inventory.entries[2].validation_error.code == RESULT_INVALID_ARGUMENT);
  CHECK(inventory.entries[3].validation_error.code == RESULT_INVALID_ARGUMENT);

  free_built(&valid);
  free_built(&encrypted);
}

static void test_half_decrypted_section_is_rejected(void) {
  Built built;
  build_program_nca(&built, PROGRAM_ID, 1);
  Byte_Source source = byte_source_from_memory(built.nca.bytes, built.nca.size);
  NCA_File nca;
  CHECK_OK(nca_open(&source, &nca));
  memset(built.nca.bytes + nca.header.sections[0].data_offset, 0xA5, 16);

  const SystemSoftware_Input inputs[] = {{"half-decrypted.nca", &source}};
  SystemSoftware_Inventory inventory;
  CHECK_OK(system_software_validate(inputs, 1, &inventory));
  CHECK(inventory.invalid_count == 1);
  CHECK(inventory.entries[0].validation_error.code == RESULT_ENCRYPTED_INPUT);

  free_built(&built);
}

static void test_duplicate_identity(void) {
  Built first;
  Built duplicate;
  build_program_nca(&first, PROGRAM_ID, 4);
  build_program_nca(&duplicate, PROGRAM_ID, 4);
  Byte_Source first_source = byte_source_from_memory(first.nca.bytes, first.nca.size);
  Byte_Source duplicate_source =
      byte_source_from_memory(duplicate.nca.bytes, duplicate.nca.size);
  const SystemSoftware_Input inputs[] = {
      {"first.nca", &first_source}, {"duplicate.nca", &duplicate_source}};
  SystemSoftware_Inventory inventory;
  CHECK_OK(system_software_validate(inputs, 2, &inventory));

  CHECK(inventory.valid_count == 1);
  CHECK(inventory.invalid_count == 1);
  CHECK(inventory.entries[0].validated);
  CHECK(inventory.entries[1].validation_error.code == RESULT_ALREADY_EXISTS);
  CHECK(inventory.entries[1].duplicate_of == 0);

  free_built(&first);
  free_built(&duplicate);
}

static void test_collection_arguments_and_limit(void) {
  Built built;
  build_program_nca(&built, PROGRAM_ID, 1);
  Byte_Source source = byte_source_from_memory(built.nca.bytes, built.nca.size);
  SystemSoftware_Input inputs[SYSTEM_SOFTWARE_MAX_FILES];
  for (uint32_t i = 0; i < SYSTEM_SOFTWARE_MAX_FILES; i++) {
    inputs[i] = (SystemSoftware_Input){"same.nca", &source};
  }

  SystemSoftware_Inventory inventory;
  CHECK_CODE(system_software_validate(NULL, 1, &inventory),
             RESULT_INVALID_ARGUMENT);
  CHECK_CODE(system_software_validate(inputs, 0, &inventory),
             RESULT_INVALID_ARGUMENT);
  CHECK_CODE(system_software_validate(inputs, SYSTEM_SOFTWARE_MAX_FILES + 1u,
                                      &inventory),
             RESULT_INVALID_ARGUMENT);
  CHECK_CODE(system_software_validate(inputs, 1, NULL),
             RESULT_INVALID_ARGUMENT);
  CHECK_OK(system_software_validate(inputs, SYSTEM_SOFTWARE_MAX_FILES,
                                    &inventory));
  CHECK(inventory.count == SYSTEM_SOFTWARE_MAX_FILES);
  CHECK(inventory.valid_count == 1);
  CHECK(inventory.invalid_count == SYSTEM_SOFTWARE_MAX_FILES - 1u);

  free_built(&built);
}

int main(void) {
  test_valid_inventory_and_read_only_inputs();
  test_invalid_entries_do_not_abort_collection();
  test_half_decrypted_section_is_rejected();
  test_duplicate_identity();
  test_collection_arguments_and_limit();
  printf("[system_software_test] all tests passed\n");
  return 0;
}
