/**
 * Meta NCA reader: locate and parse the CNMT file in a plaintext Meta NCA.
 *
 * Input must already be pre-decrypted. This composes the existing bounded
 * NCA and PFS0 readers with cnmt_open(); it performs no decryption,
 * extraction, key handling, hashing, or signature verification.
 *
 * All sources and parsed values borrow caller-owned input and arena memory.
 * The input Byte_Source and Arena must outlive the returned Meta_NCA.
 */
#ifndef SWITCH_HLE_LOADER_META_NCA_H
#define SWITCH_HLE_LOADER_META_NCA_H

#include "common/arena.h"
#include "common/result.h"
#include "hle/loader/cnmt.h"
#include "hle/loader/exefs.h"
#include "hle/loader/nca_parse.h"

typedef struct Meta_NCA {
  NCA_File nca;
  Byte_Source_Slice pfs0_source;
  ExeFS pfs0;
  Byte_Source_Slice cnmt_source;
  CNMT cnmt;
} Meta_NCA;

/**
 * Open a pre-decrypted Meta NCA, locate its unique .cnmt file in its PFS0
 * section, and parse it using the caller-selected content record layout.
 *
 * RESULT_INVALID_ARGUMENT: NULL arguments, non-Meta NCA, or multiple PFS0
 *                          sections
 * RESULT_NOT_FOUND: no PFS0 section or no .cnmt file
 * RESULT_ALREADY_EXISTS: more than one .cnmt file in the PFS0
 * Other NCA/PFS0/CNMT errors (including encrypted input and OOM) propagate.
 * On error, out is partially populated and the arena may have advanced.
 */
Error meta_nca_open(const Byte_Source *source, Arena *arena,
                    CNMT_Content_Layout layout, Meta_NCA *out);

#endif /* SWITCH_HLE_LOADER_META_NCA_H */
