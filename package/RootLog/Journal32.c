/* SPDX-License-Identifier: BSD-2-Clause
 * Fixed-file block journal with explicit exact predecessor per checkpoint. IO may ONLY address the immutable
 * file-block allowlist. No filesystem/GPT/firmware commands, allocation or reset.
 * Caller verifies target identity and the live FAT allocation before enabling IO.
 */
#include "Journal32.h"
#include "NativeTarget32.h"
static tb32_u32 U32(const tb32_u8*p){return (tb32_u32)p[0]|((tb32_u32)p[1]<<8)|((tb32_u32)p[2]<<16)|((tb32_u32)p[3]<<24);}
static tb32_u64 U64(const tb32_u8*p){return U32(p)|((tb32_u64)U32(p+4)<<32);}
static void P32(tb32_u8*p,tb32_u32 n){unsigned i;for(i=0;i<4;i++)p[i]=(tb32_u8)(n>>(8*i));}
static void P64(tb32_u8*p,tb32_u64 n){P32(p,(tb32_u32)n);P32(p+4,(tb32_u32)(n>>32));}
static int Same(const void*a,const void*b,tb32_size n){const tb32_u8*x=(const tb32_u8*)a,*y=(const tb32_u8*)b;while(n--)if(*x++!=*y++)return 0;return 1;}
static void Copy(void*a,const void*b,tb32_size n){tb32_u8*x=(tb32_u8*)a;const tb32_u8*y=(const tb32_u8*)b;while(n--)*x++=*y++;}
static tb32_u32 Step(tb32_u32 c,tb32_u8 v){unsigned b;c^=v;for(b=0;b<8;b++)c=(c>>1)^((c&1)?0xedb88320U:0);return c;}
static tb32_u32 Crc(const tb32_u8*p,tb32_size n,int header){tb32_u32 c=~0U;tb32_size i;for(i=0;i<n;i++)c=Step(c,(header&&i>=52&&i<56)?0:p[i]);return ~c;}
static int Allowed(tb32_u32 lba){unsigned i;for(i=0;i<32;i++)if(Tb32FileLbas[i]==lba)return 1;return 0;}
static int Plan(void){unsigned i,j;for(i=0;i<32;i++){if(Tb32FileLbas[i]<TB32_DATA_START||Tb32FileLbas[i]>=TB32_VOLUME_BLOCKS)return 0;for(j=0;j<i;j++)if(Tb32FileLbas[i]==Tb32FileLbas[j])return 0;}return 1;}
int Tb32Layout(const void*partition,tb32_size bytes){
 const tb32_u8*p=(const tb32_u8*)partition;tb32_u32 c=~0U;tb32_size i,k;
 if(!p||bytes!=TB32_PARTITION_BLOCKS*TB32_BLOCK||!Plan())return 0;
 for(i=0;i<TB32_META_COUNT;i++){
  tb32_u32 lba=Tb32MetaLbas[i];if(lba>=TB32_VOLUME_BLOCKS||Allowed(lba))return 0;
  for(k=0;k<TB32_BLOCK;k++){
   tb32_u8 v=p[(tb32_size)lba*TB32_BLOCK+k];unsigned o=k%32;
   if(Tb32MetaDirectory[i]&&(o==13||o==14||o==15||o==16||o==17||o==18||o==19||o==22||o==23||o==24||o==25))v=0;
   c=Step(c,v);
  }
 }
 return ~c==TB32_META_CRC;
}
tb32_u32 Tb32OutsideCrc(const void*partition,tb32_size bytes){
 const tb32_u8*p=(const tb32_u8*)partition;tb32_u32 c=~0U;tb32_size i;unsigned lba;
 if(!p||bytes!=TB32_PARTITION_BLOCKS*TB32_BLOCK)return 0;
 for(lba=0;lba<TB32_PARTITION_BLOCKS;lba++)if(!Allowed(lba))for(i=0;i<TB32_BLOCK;i++)c=Step(c,p[(tb32_size)lba*TB32_BLOCK+i]);
 return ~c;
}
static int Envelope(const tb32_u8*p){return Same(p,"TBLOG27\0",8)&&U32(p+8)==1&&U32(p+12)==4096&&U32(p+16)==65536&&U32(p+20)<=61440&&Same(p+56,Tb32Token,32)&&U32(p+88)<=1;}
static int Valid(const tb32_u8*p){tb32_size i;if(!Envelope(p)||U32(p+92)!=0x434f4d54)return 0;for(i=96;i<4096;i++)if(p[i])return 0;return U32(p+52)==Crc(p,4096,1)&&U32(p+48)==Crc(p+4096,U32(p+20),0);}
static int ReadFileBlock(TB32_IO*io,unsigned index,void*p){return index<32&&Allowed(Tb32FileLbas[index])&&io->read(io->context,Tb32FileLbas[index],p);}
static int WriteFileBlock(TB32_IO*io,unsigned index,const void*p){return index<32&&Allowed(Tb32FileLbas[index])&&io->write(io->context,Tb32FileLbas[index],p);}
int Tb32CommitAfter(TB32_IO*io,TB32_WORK*w,const char*text,tb32_size bytes,tb32_u64 expected,const char*previous,tb32_size previousBytes){
 unsigned i,index,latest;int a,b;tb32_u64 seq;
 if(!io||!io->read||!io->write||!io->sync||!w||!text||bytes>61440||!Plan())return 1;
 for(i=0;i<32;i++)if(!ReadFileBlock(io,i,w->old+i*4096))return 2;
 if(!Envelope(w->old)||!Envelope(w->old+65536))return 3;
 a=Valid(w->old);b=Valid(w->old+65536);if(!a&&!b)return 4;
 latest=(!a||(b&&U64(w->old+65536+24)>U64(w->old+24)))?1:0;index=1-latest;seq=U64(w->old+latest*65536+24);
 if(seq!=expected||!previous||previousBytes>61440||U32(w->old+latest*65536+20)!=previousBytes||!Same(w->old+latest*65536+4096,previous,previousBytes)||seq==~(tb32_u64)0)return 5;
 for(i=0;i<65536;i++)w->next[i]=0;
 Copy(w->next,"TBLOG27\0",8);P32(w->next+8,1);P32(w->next+12,4096);P32(w->next+16,65536);P32(w->next+20,(tb32_u32)bytes);P64(w->next+24,seq+1);P64(w->next+32,seq+1);P32(w->next+48,Crc((const tb32_u8*)text,bytes,0));Copy(w->next+56,Tb32Token,32);P32(w->next+88,1);Copy(w->next+4096,text,bytes);P32(w->next+52,Crc(w->next,4096,1));
 for(i=0;i<16;i++)if(!WriteFileBlock(io,index*16+i,w->next+i*4096))return 6;
 if(!io->sync(io->context))return 7;
 for(i=0;i<16;i++)if(!ReadFileBlock(io,index*16+i,w->verify+i*4096))return 8;
 if(!Same(w->next,w->verify,65536))return 9;
 P32(w->next+92,0x434f4d54);P32(w->next+52,Crc(w->next,4096,1));
 if(!WriteFileBlock(io,index*16,w->next))return 10;
 if(!io->sync(io->context))return 11;
 for(i=0;i<16;i++)if(!ReadFileBlock(io,index*16+i,w->verify+i*4096))return 12;
 if(!Same(w->next,w->verify,65536)||!Valid(w->verify))return 13;
 return 0;
}
