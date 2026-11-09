/**
 * Resolve CNMT content records against user-supplied, pre-decrypted NCA files.
 *
 * Matching uses the 32-hex-character content ID in each input basename
 * (optionally followed by ".nca"). A match is only accepted after the
 * existing NCA structural validation succeeds and its content type and
 * byte size agree with the CNMT record. This does not hash content or
 * verify authenticity.
 */
#ifndef SWITCH_HLE_LOADER_SYSTEM_CONTENT_H
#define SWITCH_HLE_LOADER_SYSTEM_CONTENT_H

#include <stdint.h>

#include "common/arena.h"
#include "common/result.h"
#include "hle/loader/cnmt.h"
#include "hle/loader/system_software.h"

typedef enum SystemContent_Status {
  SYSTEM_CONTENT_MATCHED = 0,
  SYSTEM_CONTENT_MISSING,
  SYSTEM_CONTENT_INVALID,
  SYSTEM_CONTENT_AMBIGUOUS,
  SYSTEM_CONTENT_MISMATCH
} SystemContent_Status;

#define SYSTEM_CONTENT_NO_INPUT UINT32_MAX

typedef struct SystemContent_Entry {
  const CNMT_Content_Info *content; /* borrowed from CNMT */
  SystemContent_Status status;
  uint32_t input_index;             /* index into caller's inputs */
  Error error;                      /* OK only for MATCHED */
} SystemContent_Entry;

typedef struct SystemContent_Inventory {
  uint32_t count;
  uint32_t matched_count;
  uint32_t missing_count;
  uint32_t invalid_count;
  uint32_t ambiguous_count;
  uint32_t mismatch_count;
  SystemContent_Entry *entries;     /* allocated from caller's arena */
} SystemContent_Inventory;

/**
 * Resolve every CNMT content record against the supplied NCA inputs.
 * Names must end in a 32-digit hexadecimal content ID, optionally ".nca".
 * The caller owns all names, sources, CNMT storage, and arena memory.
 *
 * Collection errors (bad arguments, input limit, arena exhaustion) return
 * an Error. Per-content problems are recorded in entries and do not abort
 * the inventory. Inputs are structurally checked as pre-decrypted NCAs.
 */
Error system_content_resolve(const CNMT *metadata,
                             const SystemSoftware_Input *inputs,
                             uint32_t input_count,
                             Arena *arena,
                             SystemContent_Inventory *out);

#endif
