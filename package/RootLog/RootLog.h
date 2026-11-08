/* SPDX-License-Identifier: BSD-2-Clause
 * Finite root-mode native journal. Only this worker writes; every command uses
 * the production gHost.lock. No panic/interrupt I/O, retries, reset or media publish.
 */
#include "Journal32.c"
#include "RecorderPolicy.h"
extern "C" {
unsigned tb371fc_log_snapshot(char *, unsigned, uint64_t *);
extern volatile int native_owner_data_stage, native_owner_data_status, native_owner_data_readonly;
extern volatile int native_owner_cache_stage;
}
struct TbLogContext { uint8_t lun; uint64_t first; };
static int tbLogRead(void *opaque, tb32_u32 rel, void *dst)
{
  auto *ctx = static_cast<TbLogContext *>(opaque);
  return hostRead4k(ctx->lun, ctx->first + rel, 1, dst) == kIOReturnSuccess;
}
static int tbLogWrite(void *opaque, tb32_u32 rel, const void *src)
{
  auto *ctx = static_cast<TbLogContext *>(opaque);
  return hostWrite4k(ctx->lun, ctx->first + rel, 1, src, true) == kIOReturnSuccess;
}
static int tbLogSync(void *) { return hostSyncCache() == kIOReturnSuccess; }
struct TbRootLog {
  TbLogContext ctx;
  TB32_WORK work;
  uint64_t sequence, started, consoleCount;
  size_t previousBytes;
  char previous[61440], report[61440], snapshot[57344];
  uint8_t header[4096], table[16384];
  uint8_t partition[TB32_PARTITION_BLOCKS * 4096U];
};
static int tbRootLogBind(TbRootLog *log)
{
  auto *header=log->header; auto *table=log->table;
  unsigned matches=0; uint8_t selected=0xff;
  for(uint8_t lun=0;lun<6;++lun) {
    if(hostRead4k(lun,1,1,header)!=kIOReturnSuccess) return 1;
    if(memcmp(header+56,Tb89DiskGuid,16)) continue;
    uint32_t hs=le32(header+12),count=le32(header+80);
    uint64_t entries=le64(header+72),first=le64(header+40),last=le64(header+48),backup=le64(header+32);
    if(memcmp(header,"EFI PART",8)||le32(header+8)!=0x10000||hs<92||hs>4096||le32(header+20)||
       le64(header+24)!=1||rootCrc32(header,hs,true)!=le32(header+16)||entries<2||entries>64||
       !count||count>128||le32(header+84)!=128) return 2;
    uint32_t blocks=(count*128+4095)/4096;
    if(first<entries+blocks||last<first||backup<=last||backup>UINT32_MAX||backup-last<=blocks||
       TB89_FIRST<first||TB89_LAST>last) return 3;
    for(unsigned i=0;i<blocks;++i)
      if(hostRead4k(lun,entries+i,1,table+i*4096)!=kIOReturnSuccess) return 4;
    if(rootCrc32(table,count*128)!=le32(header+88)) return 5;
    unsigned found=0;
    for(unsigned i=0;i<count;++i) {
      const uint8_t *entry=table+i*128;
      if(!rootEntryUsed(entry)) continue;
      uint64_t a=le64(entry+32),z=le64(entry+40);
      if(a<first||z<a||z>last) return 6;
      if(!memcmp(entry,Tb89Entry,128)) ++found;
      else if(a<=TB89_LAST&&z>=TB89_FIRST) return 7;
    }
    if(found!=1) return 8;
    ++matches; selected=lun;
  }
  if(matches!=1) return 9;
  log->ctx={selected,TB89_FIRST};
  for(unsigned i=0;i<TB32_PARTITION_BLOCKS;++i)
    if(hostRead4k(selected,TB89_FIRST+i,1,log->partition+i*4096)!=kIOReturnSuccess) return 10;
  if(!Tb32Layout(log->partition,sizeof(log->partition))||
     Tb32OutsideCrc(log->partition,sizeof(log->partition))!=TB99_OUTSIDE_CRC) return 11;
  if(!hostLogOpen(TB89_FIRST+Tb32FileLbas[0],32)) return 12;
  gTbLogLun=selected;
  log->sequence=TB32_EXPECTED_SEQUENCE;
  log->previousBytes=sizeof(Tb32PreviousReport)-1;
  memcpy(log->previous,Tb32PreviousReport,log->previousBytes);
  return 0;
}
static int tbRootLogCheckpoint(TbRootLog *log,unsigned checkpoint)
{
  uint64_t count=0;
  unsigned bytes=tb371fc_log_snapshot(log->snapshot,sizeof(log->snapshot),&count);
  if(!bytes) return 20;
  // Snapshot before any UFS I/O, with no transport lock held. No recursive logger.
  uint64_t elapsed=0;
  absolutetime_to_nanoseconds(mach_absolute_time()-log->started,&elapsed);
  int n=snprintf(log->report,sizeof(log->report),
    "TB99 ROOT XNU LOG\nRun: %s\nProducer: XNU_NATIVE_UFS\n"
    "Sequence: %llu\nCheckpoint: %u\nElapsed ms: %llu\nLUN: %u\nPartition first: %llu\n"
    "Native DMA read/write: YES\nMetadata CRC32: %08x\nOutside file CRC32 before: %08x\n"
    "Data stage: %d\nData status: %d\nData readonly: %d\nSystemOS stage: %d\n"
    "XNU memory bytes: %llu\n"
    "State: PERIODIC_CONSOLE_SNAPSHOT\nConsole count: %llu\nConsole tail bytes: %u\n"
    "--- XNU console tail ---\n",TB89_RUN,log->sequence+1,checkpoint,elapsed/1000000,
    log->ctx.lun,TB89_FIRST,TB32_META_CRC,TB99_OUTSIDE_CRC,
    native_owner_data_stage,native_owner_data_status,native_owner_data_readonly,native_owner_cache_stage,
    (unsigned long long)max_mem,count,bytes);
  if(n<0||size_t(n)+bytes>sizeof(log->report)) return 21;
  memcpy(log->report+n,log->snapshot,bytes);
  TB32_IO io={&log->ctx,tbLogRead,tbLogWrite,tbLogSync};
  gTbLogWritesArmed=true;
  int result=Tb32CommitAfter(&io,&log->work,log->report,size_t(n)+bytes,log->sequence,log->previous,log->previousBytes);
  gTbLogWritesArmed=false;
  if(result) return 100+result; // Stop forever on failure; never re-arm/retry this run.
  ++log->sequence; log->consoleCount=count;
  log->previousBytes=size_t(n)+bytes;
  memcpy(log->previous,log->report,log->previousBytes);
  return 0;
}
static void tbRootLogWorker(void *argument,wait_result_t)
{
  auto *log=static_cast<TbRootLog *>(argument);
  int result=0;
  // Initial checkpoint is synchronous before root publication, then 90 finite rounds.
  for(unsigned checkpoint=1;checkpoint<=TB_ROOT_LOG_ROUNDS;++checkpoint) {
    IOSleep(tbRootLogInterval(checkpoint));
    result=tbRootLogCheckpoint(log,checkpoint);
    if(result==20) {result=0;continue;} // Busy RAM console: skip, with no storage attempt.
    if(result) break;
  }
  // Independent exact comparison of every outside-file block; never changes metadata.
  if(!result) for(unsigned i=0;i<TB32_PARTITION_BLOCKS;++i) {
    bool file=false;
    for(unsigned j=0;j<32;++j) file|=i==Tb32FileLbas[j];
    if(file) continue;
    if(hostRead4k(log->ctx.lun,TB89_FIRST+i,1,log->header)!=kIOReturnSuccess||
       memcmp(log->header,log->partition+i*4096,4096)) {result=30;break;}
  }
  IOLog("TB LOG: finite recorder stopped result=%d sequence=%llu; later console is NOT saved; return to TWRP for readback\n",result,log->sequence);
  IOFree(log,sizeof(*log));
  thread_terminate(current_thread());
}
static void TbNativeRootLogStart(void)
{
  static unsigned started;
  if(__atomic_exchange_n(&started,1,__ATOMIC_ACQ_REL)) return;
  auto *log=static_cast<TbRootLog *>(IOMalloc(sizeof(TbRootLog)));
  if(!log) {IOLog("TB LOG: allocation failed; recorder disabled\n");return;}
  log->started=mach_absolute_time();
  int result=tbRootLogBind(log);
  if(!result) result=tbRootLogCheckpoint(log,0);
  if(result) {
    gTbLogWritesArmed=false;
    IOLog("TB LOG: refused stage=%d; recorder disabled without retry\n",result);
    IOFree(log,sizeof(*log));return;
  }
  thread_t thread=nullptr;
  if(kernel_thread_start(tbRootLogWorker,log,&thread)!=KERN_SUCCESS) {
    IOLog("TB LOG: worker creation failed; initial checkpoint retained\n");
    IOFree(log,sizeof(*log));return;
  }
  thread_deallocate(thread);
  IOLog("TB LOG: run=%s initial sequence=%llu committed; recording ~%u seconds (early 2s, late 9s; console tail only)\n",TB89_RUN,log->sequence,TB_ROOT_LOG_WAIT_SECONDS);
}
