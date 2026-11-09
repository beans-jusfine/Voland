/**
 * Meta NCA -> PFS0 -> CNMT composition layer.
 */
#include "hle/loader/meta_nca.h"

#include <string.h>

static bool has_cnmt_extension(const char *name) {
  size_t length = strlen(name);
  if (length < 5) return false;
  const char *extension = name + length - 5;
  return extension[0] == '.' &&
         (extension[1] == 'c' || extension[1] == 'C') &&
         (extension[2] == 'n' || extension[2] == 'N') &&
         (extension[3] == 'm' || extension[3] == 'M') &&
         (extension[4] == 't' || extension[4] == 'T');
}

Error meta_nca_open(const Byte_Source *source, Arena *arena,
                    CNMT_Content_Layout layout, Meta_NCA *out) {
  if (!source || !arena || !out) {
    return ERR(RESULT_INVALID_ARGUMENT, "meta_nca_open: NULL argument");
  }
  memset(out, 0, sizeof(*out));

  Error err = nca_open(source, &out->nca);
  if (!error_is_ok(err)) return err;
  if (out->nca.header.content_type != NCA_CONTENT_META) {
    return ERR(RESULT_INVALID_ARGUMENT, "meta_nca: NCA content type is not Meta");
  }

  int pfs0_index = -1;
  for (uint32_t i = 0; i < NCA_SECTION_COUNT; i++) {
    const NCA_Section_Info *section = &out->nca.header.sections[i];
    if (!section->present || section->fs_type != NCA_FS_PARTITION_FS) continue;
    if (pfs0_index >= 0) {
      return ERR(RESULT_INVALID_ARGUMENT, "meta_nca: multiple PFS0 sections are ambiguous");
    }
    pfs0_index = (int)i;
  }
  if (pfs0_index < 0) {
    return ERR(RESULT_NOT_FOUND, "meta_nca: no PFS0 section found");
  }

  err = nca_probe_section(&out->nca, (uint32_t)pfs0_index);
  if (!error_is_ok(err)) return err;
  const Byte_Source *pfs0 = nca_section_source(&out->nca, (uint32_t)pfs0_index);
  if (!pfs0) {
    return ERR(RESULT_NOT_IMPLEMENTED, "meta_nca: PFS0 section layout is unsupported");
  }
  err = byte_source_slice(pfs0, 0, pfs0->size, &out->pfs0_source);
  if (!error_is_ok(err)) return err;
  err = exefs_open(&out->pfs0_source.source, arena, &out->pfs0);
  if (!error_is_ok(err)) return err;

  const ExeFS_Entry *cnmt_entry = NULL;
  for (uint32_t i = 0; i < out->pfs0.file_count; i++) {
    const ExeFS_Entry *entry = &out->pfs0.entries[i];
    if (!has_cnmt_extension(entry->name)) continue;
    if (cnmt_entry) {
      return ERR(RESULT_ALREADY_EXISTS, "meta_nca: multiple .cnmt files found");
    }
    cnmt_entry = entry;
  }
  if (!cnmt_entry) {
    return ERR(RESULT_NOT_FOUND, "meta_nca: no .cnmt file found in PFS0");
  }

  err = exefs_entry_source(&out->pfs0, cnmt_entry, &out->cnmt_source);
  if (!error_is_ok(err)) return err;
  return cnmt_open(&out->cnmt_source.source, arena, layout, &out->cnmt);
}
