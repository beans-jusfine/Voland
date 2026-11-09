/**
 * User-supplied system-software inventory.
 *
 * This subsystem accepts only user-provided, pre-decrypted NCA sources.
 * It performs structural validation through nca_open() and
 * nca_probe_section(); it does not decrypt content, inspect key material,
 * contact Nintendo services, or modify input sources.
 *
 * The caller owns every input name and Byte_Source. Their storage must
 * remain alive while this inventory is used. The inventory owns no file
 * data and performs no allocation.
 */
#ifndef SWITCH_HLE_LOADER_SYSTEM_SOFTWARE_H
#define SWITCH_HLE_LOADER_SYSTEM_SOFTWARE_H

#include <stdbool.h>
#include <stdint.h>

#include "common/result.h"
#include "hle/loader/byte_source.h"
#include "hle/loader/nca_parse.h"

#define SYSTEM_SOFTWARE_MAX_FILES 128u
#define SYSTEM_SOFTWARE_NO_DUPLICATE UINT32_MAX

typedef struct SystemSoftware_Input {
  const char *name;             /* caller-owned display name; may be NULL */
  const Byte_Source *source;    /* caller-owned, pre-decrypted NCA */
} SystemSoftware_Input;

typedef struct SystemSoftware_Entry {
  const char *name;             /* same lifetime as the input name */
  uint64_t program_id;
  uint32_t content_index;
  NCA_Content_Type content_type;
  uint8_t present_sections;     /* bit N set when NCA section N is present */
  uint32_t duplicate_of;        /* earlier entry index, or NO_DUPLICATE */
  bool validated;
  Error validation_error;       /* OK on success; per-file failure otherwise */
} SystemSoftware_Entry;

typedef struct SystemSoftware_Inventory {
  uint32_t count;               /* number of supplied inputs */
  uint32_t valid_count;
  uint32_t invalid_count;
  SystemSoftware_Entry entries[SYSTEM_SOFTWARE_MAX_FILES];
} SystemSoftware_Inventory;

/**
 * Validates and inventories a collection of user-supplied NCA files.
 *
 * Collection-level errors:
 *   RESULT_INVALID_ARGUMENT  NULL inputs/out, zero input_count, or more
 *                            than SYSTEM_SOFTWARE_MAX_FILES inputs.
 *
 * Per-file errors are stored in entries[i].validation_error; they do not
 * abort processing of later files. Each supported present section is
 * probed for its filesystem magic, so a plaintext header with encrypted
 * section data is rejected by the existing NCA parser. Duplicate identity
 * (program_id, content_type, content_index) is reported as
 * RESULT_ALREADY_EXISTS and duplicate_of points to the first occurrence.
 *
 * Returns OK when the collection itself was processed, even if one or more
 * entries failed validation. No input bytes are written or copied into
 * inventory-owned storage.
 */
Error system_software_validate(const SystemSoftware_Input *inputs,
                               uint32_t input_count,
                               SystemSoftware_Inventory *out);

#endif /* SWITCH_HLE_LOADER_SYSTEM_SOFTWARE_H */
