/**
 * User-supplied system-software inventory.
 *
 * This is a structural preflight only. All input is already-decrypted NCA;
 * cryptography, key handling, network access, and input mutation are out of
 * scope by design.
 */
#include "hle/loader/system_software.h"

#include <string.h>

static bool same_identity(const SystemSoftware_Entry *left,
                          const NCA_Header_Info *right) {
  return left->program_id == right->program_id &&
         left->content_index == right->content_index &&
         left->content_type == right->content_type;
}

Error system_software_validate(const SystemSoftware_Input *inputs,
                               uint32_t input_count,
                               SystemSoftware_Inventory *out) {
  if (!inputs || !out || input_count == 0 ||
      input_count > SYSTEM_SOFTWARE_MAX_FILES) {
    return ERR(RESULT_INVALID_ARGUMENT,
               "system_software_validate: invalid collection arguments");
  }

  memset(out, 0, sizeof(*out));
  out->count = input_count;

  for (uint32_t i = 0; i < input_count; i++) {
    SystemSoftware_Entry *entry = &out->entries[i];
    entry->name = inputs[i].name;
    entry->duplicate_of = SYSTEM_SOFTWARE_NO_DUPLICATE;
    entry->validation_error = OK;

    if (!inputs[i].source) {
      entry->validation_error =
          ERR(RESULT_INVALID_ARGUMENT, "system software entry has no byte source");
    } else {
      NCA_File nca;
      Error err = nca_open(inputs[i].source, &nca);
      if (!error_is_ok(err)) {
        entry->validation_error = err;
      } else {
        entry->program_id = nca.header.program_id;
        entry->content_index = nca.header.content_index;
        entry->content_type = nca.header.content_type;

        for (uint32_t section = 0; section < NCA_SECTION_COUNT; section++) {
          if (!nca.header.sections[section].present) continue;
          entry->present_sections =
              (uint8_t)(entry->present_sections | (uint8_t)(1u << section));
          err = nca_probe_section(&nca, section);
          if (!error_is_ok(err)) {
            entry->validation_error = err;
            break;
          }
        }

        if (error_is_ok(entry->validation_error)) {
          for (uint32_t previous = 0; previous < i; previous++) {
            const SystemSoftware_Entry *candidate = &out->entries[previous];
            if (candidate->validated && same_identity(candidate, &nca.header)) {
              entry->duplicate_of = previous;
              entry->validation_error =
                  ERR(RESULT_ALREADY_EXISTS,
                      "duplicate system software identity (program ID, content type, content index)");
              break;
            }
          }
        }
      }
    }

    entry->validated = error_is_ok(entry->validation_error);
    if (entry->validated) {
      out->valid_count++;
    } else {
      out->invalid_count++;
    }
  }

  return OK;
}
