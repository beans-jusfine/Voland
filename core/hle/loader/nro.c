#include "hle/loader/nro.h"
#include "hle/loader/byte_source.h"
#include <string.h>
#define NRO_VERSION_SUPPORTED 0u
#define NRO_SEGMENT_ALIGNMENT 0x10u
static bool range_fits(uint64_t off,uint64_t size,uint64_t limit){return off<=limit&&size<=limit-off;}
Error nro_open(const Byte_Source *source,NRO *out){
 if(!source||!source->read||!out)return ERR(RESULT_INVALID_ARGUMENT,"nro_open: NULL argument");
 if(source->size<NRO_HEADER_OFFSET+NRO_HEADER_SIZE)return ERR(RESULT_INVALID_ARGUMENT,"nro: truncated header");
 uint8_t h[NRO_HEADER_SIZE];Error e=byte_source_read(source,NRO_HEADER_OFFSET,h,sizeof(h));if(!error_is_ok(e))return e;
 if(byte_source_le32(h)!=NRO_MAGIC)return ERR(RESULT_INVALID_ARGUMENT,"nro: NRO0 magic missing");
 uint32_t version=byte_source_le32(h+4),file_size=byte_source_le32(h+8),flags=byte_source_le32(h+12);
 if(version!=NRO_VERSION_SUPPORTED)return ERR(RESULT_NOT_IMPLEMENTED,"nro: unsupported version");
 if(file_size<NRO_HEADER_OFFSET+NRO_HEADER_SIZE||file_size>source->size)return ERR(RESULT_INVALID_ARGUMENT,"nro: declared file size outside source");
 NRO parsed;memset(&parsed,0,sizeof(parsed));parsed.source=source;parsed.version=version;parsed.flags=flags;
 uint64_t end=0;
 for(uint32_t i=0;i<3;i++){
  uint32_t off=byte_source_le32(h+16+i*8),size=byte_source_le32(h+20+i*8);
  if(!size){parsed.segments[i]=(NRO_Segment){off,off,0};continue;}
  if(off%NRO_SEGMENT_ALIGNMENT!=0||!range_fits(off,size,file_size))return ERR(RESULT_INVALID_ARGUMENT,"nro: segment range or alignment invalid");
  if(i&&off<end)return ERR(RESULT_INVALID_ARGUMENT,"nro: segments overlap or are out of order");
  parsed.segments[i]=(NRO_Segment){off,off,size};end=(uint64_t)off+size;
 }
 parsed.bss_size=byte_source_le32(h+40);
 if(end> NRO_MAX_IMAGE_BYTES || parsed.bss_size>NRO_MAX_IMAGE_BYTES-end)return ERR(RESULT_INVALID_ARGUMENT,"nro: image exceeds size cap");
 parsed.image_size=(end+parsed.bss_size+0xFFFu)&~UINT64_C(0xFFF);
 memcpy(parsed.module_id,h+48,NRO_MODULE_ID_SIZE);
 *out=parsed;return OK;
}
Error nro_read_segment(const NRO *nro,NRO_Segment_Kind kind,void *out,uint64_t out_size){
 if(!nro||!nro->source||kind<NRO_SEGMENT_TEXT||kind>NRO_SEGMENT_DATA||!out)return ERR(RESULT_INVALID_ARGUMENT,"nro_read_segment: invalid argument");
 const NRO_Segment *s=&nro->segments[(unsigned)kind];
 if(out_size<s->size)return ERR(RESULT_INVALID_ARGUMENT,"nro_read_segment: output buffer too small");
 if(!s->size)return OK;
 return byte_source_read(nro->source,s->file_offset,out,s->size);
}
