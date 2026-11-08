#define CHECK_NAME "nro_test"
#include "check.h"
#include "hle/loader/nro.h"
#include "hle/loader/byte_source.h"
#include <string.h>
static void put32(uint8_t*p,uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);}
int main(void){
 uint8_t image[0x300]={0};put32(image+0x10,NRO_MAGIC);put32(image+0x18,sizeof(image));
 put32(image+0x20,0x100);put32(image+0x24,0x20);
 put32(image+0x28,0x120);put32(image+0x2C,0x10);
 put32(image+0x30,0x130);put32(image+0x34,0x20);
 put32(image+0x38,0x1000);for(unsigned i=0;i<0x20;i++)image[0x100+i]=(uint8_t)i;
 Byte_Source src=byte_source_from_memory(image,sizeof(image));NRO nro;CHECK_OK(nro_open(&src,&nro));
 CHECK(nro.image_size==0x2000);CHECK(nro.segments[0].size==0x20);CHECK(nro.bss_size==0x1000);
 uint8_t out[0x20]={0};CHECK_OK(nro_read_segment(&nro,NRO_SEGMENT_TEXT,out,sizeof(out)));CHECK(memcmp(out,image+0x100,sizeof(out))==0);
 CHECK_CODE(nro_read_segment(&nro,NRO_SEGMENT_TEXT,out,1),RESULT_INVALID_ARGUMENT);
 image[0x10]='X';CHECK_CODE(nro_open(&src,&nro),RESULT_INVALID_ARGUMENT);
 image[0x10]='N';image[0x11]='R';image[0x12]='O';image[0x13]='0';put32(image+0x20,0x2F0);put32(image+0x24,0x40);
 CHECK_CODE(nro_open(&src,&nro),RESULT_INVALID_ARGUMENT);
 printf("[" CHECK_NAME "] OK\n");return 0;
}
