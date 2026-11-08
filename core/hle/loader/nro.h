/** Minimal, bounds-checked parser for Nintendo Switch NRO homebrew images.
 * NRO payloads are user-provided executable data; this parser does not
 * fetch, decrypt, or authenticate them. Segment mapping/permissions are
 * the caller's responsibility.
 */
#ifndef SWITCH_HLE_LOADER_NRO_H
#define SWITCH_HLE_LOADER_NRO_H
#include <stdint.h>
#include "common/result.h"
#include "hle/loader/byte_source.h"
#define NRO_MAGIC ((uint32_t)0x304F524E)
#define NRO_HEADER_OFFSET ((uint64_t)0x10)
#define NRO_HEADER_SIZE ((uint64_t)0x70)
#define NRO_MODULE_ID_SIZE 0x20u
#define NRO_MAX_IMAGE_BYTES ((uint64_t)0x40000000)
typedef enum NRO_Segment_Kind { NRO_SEGMENT_TEXT=0,NRO_SEGMENT_RODATA=1,NRO_SEGMENT_DATA=2 } NRO_Segment_Kind;
typedef struct NRO_Segment { uint32_t file_offset; uint32_t memory_offset; uint32_t size; } NRO_Segment;
typedef struct NRO { const Byte_Source *source; uint32_t version,flags,bss_size; uint64_t image_size; NRO_Segment segments[3]; uint8_t module_id[NRO_MODULE_ID_SIZE]; } NRO;
/* Parses the NRO0 header at file offset 0x10. Rejects truncated headers,
 * unsupported versions, out-of-file segments, overlapping/out-of-order
 * memory ranges, and images exceeding NRO_MAX_IMAGE_BYTES. */
Error nro_open(const Byte_Source *source,NRO *out);
/* Reads one segment. out_size must cover the segment's declared size. */
Error nro_read_segment(const NRO *nro,NRO_Segment_Kind kind,void *out,uint64_t out_size);
#endif
