/* SPDX-License-Identifier: BSD-2-Clause
 * Link into the exact vendor UFSDxe; no guessed firmware function ABI.
 * This component closes reviewed CPU entries. It NEVER claims DMA drainage,
 * queue ownership, power retention or native IO permission. No EBS callback.
 */
#include "ProducerGate.h"
#include "../UfsHandoff/AdmissionGate.h"

typedef UINT64 (EFIAPI *METHOD6)(UINT64,UINT64,UINT64,UINT64,UINT64,UINT64);
typedef VOID (EFIAPI *NOTIFY2)(UINT64,UINT64);
typedef EFI_STATUS (EFIAPI *ENTRY2)(EFI_HANDLE,EFI_SYSTEM_TABLE *);
typedef struct {
 UINT64 Magic;
 UINT32 Revision,EntryRva;
 UINT32 OriginalRva[8];
} TB_GUARD_CONFIG;
/* Filled in the isolated linked module before insertion, never at runtime.
 * Volatile prevents constant propagation of the pre-insertion placeholder.
 */
volatile const TB_GUARD_CONFIG TbUfsGuardConfig = {
 0x5442474154453031ULL,1,0,{0,0,0,0,0,0,0,0}
};
static TB_ADMISSION_GATE Gate;
static atomic_uint Initialized;
static UINTN Base;
static UINTN Original[8];
static TB_UFS_PRODUCER_GATE Api;
static EFI_GUID ApiGuid=TB_UFS_PRODUCER_GATE_GUID;

static UINT64 Call(unsigned n,UINT64 a,UINT64 b,UINT64 c,UINT64 d,UINT64 e,UINT64 f) {
 TB_ADMISSION_TICKET ticket={0};UINT64 result;
 if(atomic_load_explicit(&Initialized,memory_order_acquire)!=1 ||
    !TbAdmissionEnter(&Gate,&ticket))return EFI_NOT_READY;
 result=((METHOD6)Original[n])(a,b,c,d,e,f);
 if(!TbAdmissionLeave(&Gate,&ticket)) {
  (void)TbAdmissionQuarantine(&Gate);return EFI_DEVICE_ERROR;
 }
 return result;
}
#define REQUEST(N,Name) \
UINT64 EFIAPI Name(UINT64 a,UINT64 b,UINT64 c,UINT64 d,UINT64 e,UINT64 f) { \
 return Call(N,a,b,c,d,e,f); \
}
REQUEST(0,TbUfsGuardRead)
REQUEST(1,TbUfsGuardWrite)
REQUEST(2,TbUfsGuardRead2)
REQUEST(3,TbUfsGuardWrite2)
REQUEST(4,TbUfsGuardCommon)

static VOID Notify(unsigned n,UINT64 a,UINT64 b,int completing) {
 TB_ADMISSION_TICKET ticket={0};unsigned old,k;
 if(atomic_load_explicit(&Initialized,memory_order_acquire)!=1)return;
 /* Completion may drain existing work while CLOSING. It cannot enter after
  * CPU sealing. Close is one-way; no timer cancellation or flag shortcut.
  */
 if(!completing) {
  if(!TbAdmissionEnter(&Gate,&ticket))return;
 } else {
  old=TbAdmissionSnapshot(&Gate);
  for(k=0;k<TB_ADMISSION_ATTEMPTS;k++) {
   if((old&3)>TB_ADMISSION_CLOSING || (old>>2)==TB_ADMISSION_COUNT_MAX)return;
   if(atomic_compare_exchange_weak_explicit(&Gate.word,&old,old+4,
        memory_order_acq_rel,memory_order_acquire)) {
    ticket.owner=&Gate;ticket.held=1;break;
   }
  }
  if(!ticket.held)return;
 }
 ((NOTIFY2)Original[n])(a,b);
 if(!TbAdmissionLeave(&Gate,&ticket))(void)TbAdmissionQuarantine(&Gate);
}
VOID EFIAPI TbUfsGuardPlatform(UINT64 a,UINT64 b){Notify(5,a,b,0);}
VOID EFIAPI TbUfsGuardComplete(UINT64 a,UINT64 b){Notify(6,a,b,1);}

EFI_STATUS EFIAPI TbUfsGuardSnapshot(TB_UFS_PRODUCER_GATE *self,UINT32 *word) {
 if(self!=&Api || !word)return EFI_INVALID_PARAMETER;
 if(atomic_load_explicit(&Initialized,memory_order_acquire)!=1)return EFI_NOT_READY;
 *word=TbAdmissionSnapshot(&Gate);return EFI_SUCCESS;
}
EFI_STATUS EFIAPI TbUfsGuardClose(TB_UFS_PRODUCER_GATE *self,UINT32 *word) {
 if(self!=&Api || !word)return EFI_INVALID_PARAMETER;
 if(atomic_load_explicit(&Initialized,memory_order_acquire)!=1)return EFI_NOT_READY;
 int closed=TbAdmissionClose(&Gate);*word=TbAdmissionSnapshot(&Gate);
 return closed?EFI_SUCCESS:EFI_NOT_READY;
}
EFI_STATUS EFIAPI TbUfsGuardCpuSeal(TB_UFS_PRODUCER_GATE *self,UINT32 *word) {
 if(self!=&Api || !word)return EFI_INVALID_PARAMETER;
 if(atomic_load_explicit(&Initialized,memory_order_acquire)!=1)return EFI_NOT_READY;
 int sealed=TbAdmissionSeal(&Gate);*word=TbAdmissionSnapshot(&Gate);
 return sealed?EFI_SUCCESS:EFI_NOT_READY;
}

EFI_STATUS EFIAPI TbUfsGuardEntry(EFI_HANDLE image,EFI_SYSTEM_TABLE *system) {
 EFI_STATUS status;EFI_HANDLE gate_handle=NULL;unsigned n;
 if(!system || !system->BootServices || TbUfsGuardConfig.Magic!=0x5442474154453031ULL ||
    TbUfsGuardConfig.Revision!=1 || !TbUfsGuardConfig.EntryRva)return EFI_LOAD_ERROR;
 if(atomic_load_explicit(&Initialized,memory_order_acquire))return EFI_ALREADY_STARTED;
 Base=(UINTN)&TbUfsGuardEntry-TbUfsGuardConfig.EntryRva;
 for(n=0;n<8;n++) {
  if(!TbUfsGuardConfig.OriginalRva[n])return EFI_LOAD_ERROR;
  Original[n]=Base+TbUfsGuardConfig.OriginalRva[n];
 }
 TbAdmissionInit(&Gate);
 atomic_store_explicit(&Initialized,1,memory_order_release);
 status=((ENTRY2)Original[7])(image,system);
 if(EFI_ERROR(status))return status;
 Api.Revision=1;Api.ModuleBase=Base;Api.CoveredEntries=0x7f;
 Api.Close=TbUfsGuardClose;Api.CpuSeal=TbUfsGuardCpuSeal;Api.Snapshot=TbUfsGuardSnapshot;
 /* Publish only after original entry success. Publication failure must NOT
  * unload the now-active vendor driver and its protocol method code. No
  * published gate means consumers cannot obtain authority to close it.
  */
 (void)system->BootServices->InstallProtocolInterface(&gate_handle,&ApiGuid,
                                                     EFI_NATIVE_INTERFACE,&Api);
 return status;
}
