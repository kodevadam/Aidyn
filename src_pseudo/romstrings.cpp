#include "romstring.h"
#include "romcopy.h"
#include "decompress.h"
#include "endian_swap.h"
#include "heapN64.h"
#include "dialoug.h"
#define FILENAME "./src/romstrings.cpp"

namespace  RomString{
char ** Load(void *romAddr,size_t size){
  u8 bVar1;
  u16 *OutDat;
  u8 *dest;
  char **ret;
  u16 *puVar3;
  u32 uVar4;
  u8 header[8];
  u32 auStack_28;
  ROMCOPYS(header,romAddr,8,54);
  fprintf(stderr, "[romstr] header bytes: %02x %02x %02x %02x %02x %02x %02x %02x\n",
          header[0],header[1],header[2],header[3],header[4],header[5],header[6],header[7]);
  /* First 2 bytes = uncompressed output size (big-endian u16) */
  u16 uncompSize;
  memcpy(&uncompSize, header, 2);
  BE16S(uncompSize);
  fprintf(stderr, "[romstr] uncompSize(u16)=%u, as_u32=%u\n", uncompSize,
          (u32)((header[0]<<24)|(header[1]<<16)|(header[2]<<8)|header[3]));
  if (uncompSize == 0) {
    /* Maybe the header is a u32, not u16 */
    u32 uncompSize32;
    memcpy(&uncompSize32, header, 4);
    BE32(uncompSize32);
    fprintf(stderr, "[romstr] trying u32 uncompSize=%u\n", uncompSize32);
    uncompSize = (u16)uncompSize32;  /* fallback: use u32 if u16 was 0 */
    if (uncompSize32 > 0 && uncompSize32 < 0x100000) {
      ALLOCS(OutDat, uncompSize32, 60);
      ALLOCS(dest, size, 63);
      ROMCOPYS(dest, romAddr, size, 66);
      auStack_28 = 0;
      decompress_LZ01(dest + 4, size - 4, (u8 *)OutDat, &auStack_28);
      fprintf(stderr, "[romstr] LZ01 done (u32 header): outSize=%u\n", auStack_28);
      HFREE(dest, 0x4a);
      goto parse_strings;
    }
  }
  ALLOCS(OutDat,uncompSize,60);
  ALLOCS(dest,size,63);
  ROMCOPYS(dest,romAddr,size,66);
  auStack_28 = 0;
  decompress_LZ01(dest + 2, size - 2, (u8 *)OutDat, &auStack_28);
  fprintf(stderr, "[romstr] LZ01 done (u16 header): outSize=%u\n", auStack_28);
  HFREE(dest,0x4a);
parse_strings:
  fprintf(stderr, "[romstr] LZ01 done: outSize=%u\n", auStack_28);
  HFREE(dest,0x4a);
  puVar3 = OutDat + 1;
  auStack_28 = (u32)*OutDat;
  BE32(auStack_28);  /* string count from decompressed data (also BE) */
  ALLOCS(ret,auStack_28*sizeof(char*),85);
  if (auStack_28 != 0) {
    for(u16 i=0;i<auStack_28;i++) {
      bVar1 = *(u8 *)puVar3;
      ret[i] = (char *)((uintptr_t)puVar3 + 1);
      decrypt_string((char *)((uintptr_t)puVar3 + 1),0x10,0x103,(u16)bVar1);
      some_string_func(ret[i]);
      puVar3 = (u16 *)((uintptr_t)puVar3 + bVar1 + 1);
    }
  }
  return ret;
}
void Free(char **txt){
  HFREE(*txt + -3,121);
  HFREE(txt,122);
}

}