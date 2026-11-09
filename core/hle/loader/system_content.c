#include "hle/loader/system_content.h"

#include <string.h>

static int hex_value(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static const char *basename_of(const char *name) {
  const char *base = name;
  if (!name) return NULL;
  for (const char *p = name; *p; p++) {
    if (*p == '/' || *p == '\\') base = p + 1;
  }
  return base;
}

static bool name_has_content_id(const char *name, const uint8_t id[16]) {
  const char *base = basename_of(name);
  if (!base) return false;
  size_t length = strlen(base);
  if (length != 32u && length != 36u) return false;
  if (length == 36u &&
      !(base[32] == '.' && base[33] == 'n' && base[34] == 'c' &&
        base[35] == 'a')) return false;
  for (uint32_t i = 0; i < 16; i++) {
    int hi = hex_value(base[i * 2]);
    int lo = hex_value(base[i * 2 + 1]);
    if (hi < 0 || lo < 0 || (uint8_t)((hi << 4) | lo) != id[i]) {
      return false;
    }
  }
  return true;
}

Error system_content_resolve(const CNMT *metadata,
                             const SystemSoftware_Input *inputs,
                             uint32_t input_count,
                             Arena *arena,
                             SystemContent_Inventory *out) {
  if (!metadata || !arena || !arena->base || !out ||
      (input_count != 0 && !inputs) ||
      input_count > SYSTEM_SOFTWARE_MAX_FILES ||
      (metadata->header.content_count != 0 && !metadata->contents)) {
    return ERR(RESULT_INVALID_ARGUMENT,
               "system_content_resolve: invalid arguments or input count");
  }

  SystemSoftware_Inventory validated;
  if (input_count != 0) {
    Error err = system_software_validate(inputs, input_count, &validated);
    if (!error_is_ok(err)) return err;
  } else {
    memset(&validated, 0, sizeof(validated));
  }

  SystemContent_Entry *entries = NULL;
  if (metadata->header.content_count != 0) {
    entries = (SystemContent_Entry *)arena_allocate(
        arena, (size_t)metadata->header.content_count * sizeof(*entries),
        _Alignof(SystemContent_Entry));
    if (!entries) {
      return ERR(RESULT_OUT_OF_MEMORY,
                 "system_content_resolve: arena exhausted");
    }
  }

  memset(out, 0, sizeof(*out));
  out->count = metadata->header.content_count;
  out->entries = entries;

  for (uint32_t i = 0; i < out->count; i++) {
    const CNMT_Content_Info *expected = &metadata->contents[i];
    SystemContent_Entry *entry = &entries[i];
    entry->content = expected;
    entry->input_index = SYSTEM_CONTENT_NO_INPUT;
    entry->error = OK;

    uint32_t candidates = 0;
    uint32_t candidate_index = SYSTEM_CONTENT_NO_INPUT;
    for (uint32_t j = 0; j < input_count; j++) {
      if (name_has_content_id(inputs[j].name, expected->content_id)) {
        candidates++;
        candidate_index = j;
      }
    }

    if (candidates == 0) {
      entry->status = SYSTEM_CONTENT_MISSING;
      entry->error = ERR(RESULT_NOT_FOUND, "CNMT content ID was not supplied");
      out->missing_count++;
      continue;
    }
    if (candidates > 1) {
      entry->status = SYSTEM_CONTENT_AMBIGUOUS;
      entry->error = ERR(RESULT_ALREADY_EXISTS,
                         "multiple inputs have the same CNMT content ID");
      out->ambiguous_count++;
      continue;
    }

    entry->input_index = candidate_index;
    const SystemSoftware_Entry *actual = &validated.entries[candidate_index];
    if (!actual->validated) {
      entry->status = SYSTEM_CONTENT_INVALID;
      entry->error = actual->validation_error;
      out->invalid_count++;
      continue;
    }
    if ((uint8_t)actual->content_type != expected->content_type ||
        inputs[candidate_index].source->size != expected->size) {
      entry->status = SYSTEM_CONTENT_MISMATCH;
      entry->error = ERR(RESULT_INVALID_ARGUMENT,
                         "NCA content type or size does not match CNMT");
      out->mismatch_count++;
      continue;
    }

    entry->status = SYSTEM_CONTENT_MATCHED;
    out->matched_count++;
  }

  return OK;
}
