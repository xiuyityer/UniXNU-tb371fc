/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef TB_JOURNAL32_H
#define TB_JOURNAL32_H
typedef __UINT8_TYPE__ tb32_u8;
typedef __UINT32_TYPE__ tb32_u32;
typedef __UINT64_TYPE__ tb32_u64;
typedef __SIZE_TYPE__ tb32_size;
#define TB32_BLOCK 4096U
#define TB32_FILE 131072U
#define TB32_SLOT 65536U
typedef struct {void*context;int(*read)(void*,tb32_u32,void*);int(*write)(void*,tb32_u32,const void*);int(*sync)(void*);} TB32_IO;
typedef struct {tb32_u8 old[TB32_FILE],next[TB32_SLOT],verify[TB32_SLOT];} TB32_WORK;
int Tb32Layout(const void*partition,tb32_size bytes);
tb32_u32 Tb32OutsideCrc(const void*partition,tb32_size bytes);
int Tb32CommitAfter(TB32_IO*,TB32_WORK*,const char*,tb32_size,tb32_u64,const char*,tb32_size);
#endif
