/**
 * Bounded parser for Nintendo Switch PackagedContentMeta records.
 * See cnmt.h and DESIGN.md §1.6/§12. Input must already be plaintext.
 */
#include "hle/loader/cnmt.h"

#include <string.h>

#define CNMT_CONTENT_INFO_SIZE 0x38u
#define CNMT_CONTENT_SIZE_OFFSET 0x30u
#define CNMT_CONTENT_TYPE_OFFSET 0x36u
#define CNMT_CONTENT_ID_OFFSET 0x37u
#define CNMT_CONTENT_ATTRIBUTES_OFFSET 0x35u

static uint64_t read_le48(const uint8_t *p) {
  return (uint64_t)p[0] | ((uint64_t)p[1] << 8) |
         ((uint64_t)p[2] << 16) | ((uint64_t)p[3] << 24) |
         ((uint64_t)p[4] << 32) | ((uint64_t)p[5] << 40);
}

static Error cnmt_read(const Byte_Source *source, uint64_t offset,
                       void *out, uint64_t size) {
  return byte_source_read(source, offset, out, size);
}

Error cnmt_open(const Byte_Source *source, Arena *arena,
                CNMT_Content_Layout layout, CNMT *out) {
  if (!source || !source->read || !arena || !arena->base || !out) {
    return ERR(RESULT_INVALID_ARGUMENT, "cnmt_open: NULL or invalid argument");
  }
  if (layout != CNMT_CONTENT_LAYOUT_LEGACY &&
      layout != CNMT_CONTENT_LAYOUT_MODERN) {
    return ERR(RESULT_INVALID_ARGUMENT, "cnmt: unknown content record layout");
  }
  if (source->size < CNMT_HEADER_SIZE) {
    return ERR(RESULT_INVALID_ARGUMENT, "cnmt: source smaller than header");
  }

  uint8_t raw_header[CNMT_HEADER_SIZE];
  Error err = cnmt_read(source, 0, raw_header, sizeof(raw_header));
  if (!error_is_ok(err)) return err;

  CNMT parsed;
  memset(&parsed, 0, sizeof(parsed));
  parsed.source = source;
  parsed.header.title_id = byte_source_le64(raw_header + 0x00);
  parsed.header.version = byte_source_le32(raw_header + 0x08);
  parsed.header.meta_type = raw_header[0x0C];
  parsed.header.platform = raw_header[0x0D];
  parsed.header.extended_header_size = byte_source_le16(raw_header + 0x0E);
  parsed.header.content_count = byte_source_le16(raw_header + 0x10);
  parsed.header.meta_count = byte_source_le16(raw_header + 0x12);
  parsed.header.attributes = raw_header[0x14];
  parsed.header.required_download_system_version =
      byte_source_le32(raw_header + 0x18);
  parsed.extended_header_offset = CNMT_HEADER_SIZE;

  uint64_t cursor = CNMT_HEADER_SIZE;
  if (parsed.header.extended_header_size > source->size - cursor) {
    return ERR(RESULT_INVALID_ARGUMENT, "cnmt: extended header exceeds source");
  }
  cursor += parsed.header.extended_header_size;

  const uint64_t content_bytes =
      (uint64_t)parsed.header.content_count * CNMT_CONTENT_INFO_SIZE;
  if (content_bytes > source->size - cursor) {
    return ERR(RESULT_INVALID_ARGUMENT, "cnmt: content records exceed source");
  }
  const uint64_t content_offset = cursor;
  cursor += content_bytes;

  const uint64_t meta_bytes =
      (uint64_t)parsed.header.meta_count * CNMT_META_INFO_SIZE;
  if (meta_bytes > source->size - cursor) {
    return ERR(RESULT_INVALID_ARGUMENT, "cnmt: content-meta records exceed source");
  }
  const uint64_t meta_offset = cursor;

  if (parsed.header.content_count != 0) {
    parsed.contents = (CNMT_Content_Info *)arena_allocate(
        arena, (size_t)parsed.header.content_count * sizeof(CNMT_Content_Info),
        _Alignof(CNMT_Content_Info));
    if (!parsed.contents) {
      return ERR(RESULT_OUT_OF_MEMORY, "cnmt: arena exhausted for content records");
    }
  }
  if (parsed.header.meta_count != 0) {
    parsed.meta_entries = (CNMT_Content_Meta_Info *)arena_allocate(
        arena, (size_t)parsed.header.meta_count * sizeof(CNMT_Content_Meta_Info),
        _Alignof(CNMT_Content_Meta_Info));
    if (!parsed.meta_entries) {
      return ERR(RESULT_OUT_OF_MEMORY, "cnmt: arena exhausted for meta records");
    }
  }

  for (uint32_t i = 0; i < parsed.header.content_count; i++) {
    uint8_t raw[CNMT_CONTENT_INFO_SIZE];
    err = cnmt_read(source, content_offset + (uint64_t)i * sizeof(raw),
                    raw, sizeof(raw));
    if (!error_is_ok(err)) return err;

    CNMT_Content_Info *entry = &parsed.contents[i];
    memcpy(entry->hash, raw, CNMT_CONTENT_HASH_SIZE);
    memcpy(entry->content_id, raw + CNMT_CONTENT_HASH_SIZE,
           CNMT_CONTENT_ID_SIZE);
    entry->size = read_le48(raw + CNMT_CONTENT_SIZE_OFFSET);
    entry->attributes = layout == CNMT_CONTENT_LAYOUT_MODERN
                            ? raw[CNMT_CONTENT_ATTRIBUTES_OFFSET]
                            : 0;
    entry->content_type = raw[CNMT_CONTENT_TYPE_OFFSET];
    entry->id_offset = raw[CNMT_CONTENT_ID_OFFSET];
  }

  for (uint32_t i = 0; i < parsed.header.meta_count; i++) {
    uint8_t raw[CNMT_META_INFO_SIZE];
    err = cnmt_read(source, meta_offset + (uint64_t)i * sizeof(raw),
                    raw, sizeof(raw));
    if (!error_is_ok(err)) return err;

    CNMT_Content_Meta_Info *entry = &parsed.meta_entries[i];
    entry->id = byte_source_le64(raw + 0x00);
    entry->version = byte_source_le32(raw + 0x08);
    entry->meta_type = raw[0x0C];
    entry->attributes = raw[0x0D];
  }

  *out = parsed;
  return OK;
}
