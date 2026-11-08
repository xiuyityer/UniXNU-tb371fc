/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef TB_UFS_PRODUCER_GATE_H
#define TB_UFS_PRODUCER_GATE_H
#include <Uefi.h>

/* Project-local protocol. CPU admission only; no DMA/ownership authority. */
#define TB_UFS_PRODUCER_GATE_GUID \
 {0x84bb7fe1,0xcfc9,0x4ef0,{0xa3,0x56,0xf8,0x61,0x79,0x2d,0x37,0x8b}}
typedef struct TB_UFS_PRODUCER_GATE TB_UFS_PRODUCER_GATE;
typedef EFI_STATUS (EFIAPI *TB_UFS_GATE_OPERATION)(TB_UFS_PRODUCER_GATE *, UINT32 *);
struct TB_UFS_PRODUCER_GATE {
 UINT64 Revision;
 UINT64 ModuleBase;
 UINT64 CoveredEntries;
 TB_UFS_GATE_OPERATION Close;
 TB_UFS_GATE_OPERATION CpuSeal;
 TB_UFS_GATE_OPERATION Snapshot;
};
#endif
