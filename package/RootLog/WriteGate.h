/* SPDX-License-Identifier: BSD-2-Clause
 * Outbound storage has one exact target-evidenced fixed-file capability.
 * Read-only root/store windows can never widen this capability.
 */
#include "ReservedLogTarget.h"
static uint8_t gTbLogLun=0xff;
static bool gTbLogWritesArmed=false;
static bool tbRootLogWriteAllowed(uint8_t lun,uint64_t lba,uint32_t blocks)
{
  return gTbLogWritesArmed && lun==gTbLogLun && lun<6 && blocks==1 &&
         lba>=TB89_FIRST+8 && lba<TB89_FIRST+40;
}
static bool tbRootLogDataOutAllowed(const uint8_t *ucd,uint32_t dataBytes)
{
  const uint8_t *cdb=ucd+16;
  uint32_t lba=(uint32_t(cdb[2])<<24)|(uint32_t(cdb[3])<<16)|(uint32_t(cdb[4])<<8)|cdb[5];
  return ucd[0]==1 && ucd[1]==0x20 && cdb[0]==0x2a && cdb[1]==0x08 && !cdb[6] &&
         !cdb[7] && cdb[8]==1 && !cdb[9] && dataBytes==4096 &&
         tbRootLogWriteAllowed(ucd[2],lba,1);
}
