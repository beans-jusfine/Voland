/**
 * CNMT (PackagedContentMeta) parser for user-supplied, pre-decrypted data.
 *
 * The source is the extracted .cnmt file payload, not an encrypted package
 * or whole NCA. The caller owns the Byte_Source and Arena; both must outlive
 * the CNMT value and its arrays. No source bytes are modified.
 *
 * This parser only decodes the fixed CNMT header, content records, and
 * content-meta records. Extended-header bytes are bounded and exposed by
 * offset, but type-specific extended data is not interpreted here.
 */
#ifndef SWITCH_HLE_LOADER_CNMT_H
#define SWITCH_HLE_LOADER_CNMT_H

#include <stdint.h>

#include "common/arena.h"
#include "common/result.h"
#include "hle/loader/byte_source.h"

#define CNMT_HEADER_SIZE 0x20u
#define CNMT_CONTENT_HASH_SIZE 32u
#define CNMT_CONTENT_ID_SIZE 16u
#define CNMT_META_INFO_SIZE 0x10u

typedef enum CNMT_Content_Layout {
  CNMT_CONTENT_LAYOUT_LEGACY = 0,
  CNMT_CONTENT_LAYOUT_MODERN = 1
} CNMT_Content_Layout;

typedef struct CNMT_Content_Info {
  uint8_t hash[CNMT_CONTENT_HASH_SIZE];
  uint8_t content_id[CNMT_CONTENT_ID_SIZE];
  uint64_t size;
  uint8_t attributes;
  uint8_t content_type;
  uint8_t id_offset;
} CNMT_Content_Info;

typedef struct CNMT_Content_Meta_Info {
  uint64_t id;
  uint32_t version;
  uint8_t meta_type;
  uint8_t attributes;
} CNMT_Content_Meta_Info;

typedef struct CNMT_Header_Info {
  uint64_t title_id;
  uint32_t version;
  uint8_t meta_type;
  uint8_t platform;
  uint16_t extended_header_size;
  uint16_t content_count;
  uint16_t meta_count;
  uint8_t attributes;
  uint32_t required_download_system_version;
} CNMT_Header_Info;

typedef struct CNMT {
  const Byte_Source *source;
  CNMT_Header_Info header;
  uint64_t extended_header_offset;
  CNMT_Content_Info *contents;
  CNMT_Content_Meta_Info *meta_entries;
} CNMT;

Error cnmt_open(const Byte_Source *source,
                Arena *arena,
                CNMT_Content_Layout layout,
                CNMT *out);

#endif
